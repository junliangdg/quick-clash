#include "application/desktop_runtime.h"
#include "infra/config/geo_resources.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QRandomGenerator>
#include <QSet>
#include <QSysInfo>
#include <QUrl>
#include <utility>

namespace quickclash::application {
namespace {
constexpr qsizetype kSourceLimit = 16 * 1024 * 1024;
QString hash(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray newSecret() {
    QByteArray bytes(32, Qt::Uninitialized);
    for (qsizetype i = 0; i < bytes.size(); i += 4) {
        const auto value = QRandomGenerator::system()->generate();
        for (int j = 0; j < 4; ++j) {
            bytes[i + j] = static_cast<char>((value >> (j * 8)) & 255);
        }
    }
    return bytes.toHex();
}
} // namespace
DesktopRuntime::DesktopRuntime(infra::JsonStore& store, QObject* parent,
                               platform::ProxySettings* proxySettings,
                               std::function<QDateTime()> clock)
    : QObject(parent), store_(store), repository_(store), credentials_(store),
      proxyLease_(store, proxySettings ? *proxySettings : defaultProxySettings_),
      clock_(clock ? std::move(clock) : [] { return QDateTime::currentDateTimeUtc(); }),
      runtimePreferences_(infra::SettingsRepository::runtimeDefaults()) {
    directNetwork_.setProxy(QNetworkProxy::NoProxy);
    featureTimer_.setInterval(1000);
    connect(&featureTimer_, &QTimer::timeout, this, &DesktopRuntime::pollFeatures);
    connect(&engine_, &infra::EngineSession::streamData, this,
            [this](const QString& stream, const QJsonObject& data) {
                if (stream == QStringLiteral("logs")) {
                    appendLog(QStringLiteral("Core"), data.value(QStringLiteral("type")).toString(),
                              data.value(QStringLiteral("payload")).toString());
                } else {
                    auto sample = data;
                    sample.insert(QStringLiteral("time"),
                                  QString::number(QDateTime::currentMSecsSinceEpoch()));
                    featureState_.insert(QStringLiteral("traffic"), sample);
                }
            });
    subscriptionTimer_.setSingleShot(true);
    connect(&subscriptionTimer_, &QTimer::timeout, this, [this] {
        if (shuttingDown_ || store_.readOnly())
            return;
        if (busy_ || recoveryTimer_.isActive() || repository_.recoveryRequired()) {
            subscriptionTimer_.start(5000);
            return;
        }
        const auto now = clock_();
        for (const auto& value : profiles_) {
            const auto item = value.toObject();
            const auto due = QDateTime::fromString(
                item.value(QStringLiteral("nextUpdateAt")).toString(), Qt::ISODate);
            if (!item.value(QStringLiteral("secretRef")).toString().isEmpty() &&
                item.value(QStringLiteral("updateIntervalMinutes")).toInt() > 0 &&
                (!due.isValid() || due <= now)) {
                updateProfile(item.value(QStringLiteral("id")).toString());
                return;
            }
        }
        scheduleSubscriptions();
    });
    validationDeadline_.setSingleShot(true);
    validationDeadline_.setInterval(10000);
    recoveryTimer_.setSingleShot(true);
    connect(&recoveryTimer_, &QTimer::timeout, this, [this] {
        if (shuttingDown_ || backgroundSuppressed_ || store_.readOnly() ||
            repository_.recoveryRequired() || selectedId_.isEmpty() || engine_.ready())
            return;
        if (busy_) {
            recoveryTimer_.start(1000);
            return;
        }
        restartAttempt_ = true;
        start(backgroundManaged_ ? persistedProxyIntent_ && !proxyLease_.conflict()
                                 : proxyDesired_);
    });
    reconcileTimer_.setSingleShot(true);
    reconcileTimer_.setInterval(1500);
    connect(&reconcileTimer_, &QTimer::timeout, this, [this] {
        if (shuttingDown_)
            return;
        ++reconcileCount_;
        if (proxyLease_.active()) {
            const auto result = proxyLease_.matches();
            if (!std::holds_alternative<bool>(result) || !std::get<bool>(result)) {
                proxyDesired_ = false;
                error(QStringLiteral("SystemProxyConflict"));
            }
        }
        if (engine_.ready() && !busy_)
            engine_.refreshGroups();
        scheduleSubscriptions();
        publish();
    });
    connect(&validationDeadline_, &QTimer::timeout, this, [this] {
        validator_.stop();
        if (validationCompletion_) {
            auto completion = std::move(validationCompletion_);
            staging_.reset();
            completion(false);
        }
    });
    connect(&validator_, &platform::OwnedProcess::finished, this, [this](int code) {
        validationDeadline_.stop();
        if (validationCompletion_) {
            auto completion = std::move(validationCompletion_);
            staging_.reset();
            completion(code == 0);
        }
    });
    connect(&engine_, &infra::EngineSession::becameReady, this, [this](const QString&) {
        coreUptime_.start();
        status_ = QStringLiteral("RestoringSelections");
        publish();
    });
    connect(&engine_, &infra::EngineSession::failed, this, [this](const QString& code) {
        if (applying_) {
            failedApply(code);
        } else {
            error(code);
        }
    });
    connect(&engine_, &infra::EngineSession::stopped, this, [this](bool unexpected) {
        featureRequest_ = false;
        cancelDelayTests();
        connections_ = {};
        featureState_.remove(QStringLiteral("traffic"));
        if (!applying_)
            status_ = QStringLiteral("Stopped");
        if (unexpected) {
            if (applying_) {
                failedApply(QStringLiteral("CoreExitedDuringApply"));
                return;
            }
            cleanupSession();
            loadOfflineGroups();
            busy_ = false;
            pendingGroup_.clear();
            pendingMember_.clear();
            const auto released = proxyLease_.release();
            if (const auto* failure = std::get_if<domain::Error>(&released))
                error(failure->code);
            error(QStringLiteral("CoreExited"));
            if (backgroundManaged_) {
                if (coreUptime_.isValid() && coreUptime_.elapsed() >= 60000)
                    recoveryFailures_ = 0;
                scheduleBackgroundStart(true);
                publish();
                return;
            }
            const auto now = QDateTime::currentMSecsSinceEpoch();
            while (!crashTimes_.isEmpty() && now - crashTimes_.first() > 60000)
                crashTimes_.removeFirst();
            if (!busy_ && crashTimes_.size() < 3 && !store_.readOnly()) {
                crashTimes_.append(now);
                status_ = QStringLiteral("RecoveryBackoff");
                recoveryTimer_.start(1000 << (crashTimes_.size() - 1));
            } else
                error(QStringLiteral("CoreRecoveryExhausted"));
        }
        publish();
    });
    connect(
        &engine_, &infra::EngineSession::groupsChanged, this, [this](const QJsonObject& objects) {
            QJsonArray groups;
            for (auto it = objects.begin(); it != objects.end(); ++it) {
                const auto group = it.value().toObject();
                const auto type = group.value(QStringLiteral("type")).toString();
                if (type != QStringLiteral("Selector") && type != QStringLiteral("URLTest") &&
                    type != QStringLiteral("Fallback") && type != QStringLiteral("LoadBalance")) {
                    continue;
                }
                groups.append(
                    QJsonObject{{QStringLiteral("name"), it.key()},
                                {QStringLiteral("current"), group.value(QStringLiteral("now"))},
                                {QStringLiteral("members"), group.value(QStringLiteral("all"))},
                                {QStringLiteral("type"), type},
                                {QStringLiteral("canSelect"), type == QStringLiteral("Selector")}});
            }
            groups_ = groups;
            if (restoringNodes_) {
                restoringNodes_ = false;
                for (auto it = selections_.begin(); it != selections_.end(); ++it) {
                    const auto saved = it.value().toObject();
                    const auto member = saved.value(QStringLiteral("member")).toString();
                    bool selectable = false;
                    for (const auto& entry : groups_) {
                        const auto currentGroup = entry.toObject();
                        if (currentGroup.value(QStringLiteral("name")) == it.key() &&
                            currentGroup.value(QStringLiteral("canSelect")).toBool() &&
                            currentGroup.value(QStringLiteral("members"))
                                .toArray()
                                .contains(member))
                            selectable = true;
                    }
                    if (!selectable)
                        continue;
                    if (identities_.value(it.key()) ==
                            saved.value(QStringLiteral("groupIdentity")) &&
                        saved.value(QStringLiteral("profileId")) ==
                            candidate_.value(QStringLiteral("selection"))
                                .toObject()
                                .value(QStringLiteral("profileId")) &&
                        identities_.contains(member) &&
                        identities_.value(member) == saved.value(QStringLiteral("identity"))) {
                        if (restoredGroups_.contains(it.key()))
                            continue;
                        restoredGroups_.insert(it.key());
                        restoringNodes_ = true;
                        pendingGroup_ = it.key();
                        pendingMember_ = member;
                        engine_.select(pendingGroup_, pendingMember_);
                        break;
                    }
                }
                if (!restoringNodes_)
                    confirmed();
            }
            publish();
        });
    connect(&engine_, &infra::EngineSession::mutationFinished, this,
            [this](const QString& kind, bool success) {
                if (kind != QStringLiteral("select") || pendingGroup_.isEmpty())
                    return;
                if (restoringNodes_) {
                    pendingGroup_.clear();
                    pendingMember_.clear();
                    if (!success) {
                        restoringNodes_ = false;
                        failedApply(QStringLiteral("ProxySelectionRestoreFailed"));
                    }
                    return;
                }
                busy_ = false;
                if (success) {
                    selections_.insert(
                        pendingGroup_,
                        QJsonObject{
                            {QStringLiteral("member"), pendingMember_},
                            {QStringLiteral("profileId"), selectedId_},
                            {QStringLiteral("identity"), identities_.value(pendingMember_)},
                            {QStringLiteral("groupIdentity"), identities_.value(pendingGroup_)}});
                    const auto saved = store_.write(QStringLiteral("data/node_selections.json"),
                                                    {{QStringLiteral("groups"), selections_}});
                    if (const auto* failure = std::get_if<domain::Error>(&saved))
                        error(failure->code);
                } else {
                    restoringNodes_ = false;
                    error(QStringLiteral("ProxySelectionReadbackFailed"));
                }
                pendingGroup_.clear();
                pendingMember_.clear();
                publish();
            });
}
DesktopRuntime::~DesktopRuntime() {
    stop(domain::StopReason::AppExit);
}
void DesktopRuntime::initialize() {
    const auto released = proxyLease_.release();
    const bool leaseRecovered = std::holds_alternative<std::monostate>(released);
    if (const auto* failure = std::get_if<domain::Error>(&released))
        error(failure->code);
    auto result = repository_.load();
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
        return;
    }
    if (repository_.recoveryRequired()) {
        // The MVP Job guarantees no user-mode core survives its owner. System leases are
        // reconciled separately before a running transaction can be concluded.
        if (leaseRecovered) {
            result = repository_.rollback();
            if (const auto* failure = std::get_if<domain::Error>(&result))
                error(failure->code);
        } else
            error(QStringLiteral("TransactionRecoveryRequired"));
    }
    const auto catalog = store_.read(QStringLiteral("data/catalog.json"));
    if (const auto* failure = std::get_if<domain::Error>(&catalog)) {
        error(failure->code);
        return;
    }
    const auto document = std::get<std::optional<QJsonObject>>(catalog);
    if (document) {
        if (!document->value(QStringLiteral("profiles")).isArray() ||
            document->value(QStringLiteral("profiles")).toArray().size() > 100) {
            store_.freeze();
            error(QStringLiteral("CatalogRecoveryRequired"));
            return;
        }
        profiles_ = document->value(QStringLiteral("profiles")).toArray();
        QSet<QString> ids;
        for (const auto& value : profiles_) {
            const auto item = value.toObject();
            const auto id = item.value(QStringLiteral("id")).toString();
            const auto interval = item.value(QStringLiteral("updateIntervalMinutes")).toInt();
            if (QUuid(id).isNull() || ids.contains(id) ||
                (interval != 0 && (interval < 15 || interval > 10080)) ||
                (item.contains(QStringLiteral("autoApply")) &&
                 !item.value(QStringLiteral("autoApply")).isBool()) ||
                item.value(QStringLiteral("name")).toString().isEmpty() ||
                item.value(QStringLiteral("name")).toString().size() > 80 ||
                !item.value(QStringLiteral("sourceRef"))
                     .toString()
                     .startsWith(QStringLiteral("data/profiles/") + id + QLatin1Char('/')) ||
                std::holds_alternative<domain::Error>(
                    store_.path(item.value(QStringLiteral("sourceRef")).toString()))) {
                store_.freeze();
                error(QStringLiteral("CatalogRecoveryRequired"));
                return;
            }
            ids.insert(id);
            const auto source =
                store_.readBytes(item.value(QStringLiteral("sourceRef")).toString());
            if (!std::holds_alternative<QByteArray>(source) ||
                hash(std::get<QByteArray>(source)) != item.value(QStringLiteral("sourceHash"))) {
                store_.freeze();
                error(QStringLiteral("SourceIntegrityFailed"));
                return;
            }
        }
    }
    const auto selected = repository_.manifest().value(QStringLiteral("selection")).toObject();
    const auto defaults = store_.read(QStringLiteral("config/runtime_defaults.json"));
    if (const auto* value = std::get_if<std::optional<QJsonObject>>(&defaults); value && *value)
        runtimePreferences_ = **value;
    selectedId_ = selected.value(QStringLiteral("profileId")).toString();
    const auto prefRef = selected.value(QStringLiteral("preferenceSnapshotRef")).toString();
    if (!prefRef.isEmpty()) {
        const auto preferences = store_.read(prefRef);
        if (const auto* failure = std::get_if<domain::Error>(&preferences)) {
            error(failure->code);
            return;
        }
        const auto value = std::get<std::optional<QJsonObject>>(preferences);
        if (!value) {
            store_.freeze();
            error(QStringLiteral("PreferenceSnapshotMissing"));
            return;
        }
        runtimePreferences_ = *value;
        const auto semantic =
            store_.readBytes(selected.value(QStringLiteral("artifactRef")).toString());
        const auto resources =
            store_.read(selected.value(QStringLiteral("resourceManifestRef")).toString());
        if (profile(selectedId_).isEmpty() || !std::holds_alternative<QByteArray>(semantic) ||
            hash(std::get<QByteArray>(semantic)) !=
                selected.value(QStringLiteral("effectiveHash")) ||
            !std::holds_alternative<std::optional<QJsonObject>>(resources) ||
            !std::get<std::optional<QJsonObject>>(resources) ||
            *std::get<std::optional<QJsonObject>>(resources) !=
                infra::geoResourceManifest(
                    std::get<QByteArray>(semantic),
                    runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject())) {
            store_.freeze();
            error(QStringLiteral("ArtifactIntegrityFailed"));
            return;
        }
    }
    const auto mode = runtimePreferences_.value(QStringLiteral("mode")).toString();
    const auto mixed = runtimePreferences_.value(QStringLiteral("mixedPort")).toInt();
    const auto controller = runtimePreferences_.value(QStringLiteral("controllerPort")).toInt();
    if ((mode != QStringLiteral("rule") && mode != QStringLiteral("global") &&
         mode != QStringLiteral("direct")) ||
        mixed < 1024 || mixed > 65535 || controller < 1024 || controller > 65535 ||
        mixed == controller) {
        store_.freeze();
        error(QStringLiteral("RuntimePreferencesRecoveryRequired"));
        return;
    }
    const auto intent = store_.read(QStringLiteral("config/proxy_intent.json"));
    if (const auto* failure = std::get_if<domain::Error>(&intent)) {
        error(failure->code);
        return;
    }
    const auto savedIntent = std::get<std::optional<QJsonObject>>(intent);
    if (savedIntent && !savedIntent->value(QStringLiteral("desired")).isBool()) {
        store_.freeze();
        error(QStringLiteral("ProxyIntentRecoveryRequired"));
        return;
    }
    persistedProxyIntent_ = savedIntent && savedIntent->value(QStringLiteral("desired")).toBool();
    const auto selections = store_.read(QStringLiteral("data/node_selections.json"));
    if (const auto* value = std::get_if<std::optional<QJsonObject>>(&selections); value && *value)
        selections_ = (*value)->value(QStringLiteral("groups")).toObject();
    loadOfflineGroups();
    const auto target = credentials_.read(QStringLiteral("e430ed25-78b8-47ea-a7b0-d746a5783891"));
    if (const auto* value = std::get_if<QByteArray>(&target))
        testTarget_ = QString::fromUtf8(*value);
    const auto storedErrors = store_.read(QStringLiteral("logs/errors.json"));
    if (const auto* value = std::get_if<std::optional<QJsonObject>>(&storedErrors);
        value && *value) {
        errors_ = (*value)->value(QStringLiteral("entries")).toArray();
        while (errors_.size() > 100)
            errors_.removeFirst();
    }
    collectGarbage();
    scheduleSubscriptions();
    publish();
    // Older builds could leave valid imports in the catalog without a selection.
    if (selectedId_.isEmpty() && !profiles_.isEmpty() && !store_.readOnly() &&
        !repository_.recoveryRequired()) {
        selectProfile(profiles_.first().toObject().value(QStringLiteral("id")).toString());
    }
}
void DesktopRuntime::enableBackgroundManagement() {
    backgroundManaged_ = true;
    backgroundSuppressed_ = false;
    scheduleBackgroundStart();
}
void DesktopRuntime::scheduleBackgroundStart(bool afterFailure) {
    if (!backgroundManaged_ || backgroundSuppressed_ || shuttingDown_ || store_.readOnly() ||
        repository_.recoveryRequired() || selectedId_.isEmpty() || engine_.ready())
        return;
    if (afterFailure) {
        recoveryFailures_ = std::min(recoveryFailures_ + 1, 6);
        status_ = QStringLiteral("RecoveryBackoff");
        recoveryTimer_.start(std::min(30000, 1000 << (recoveryFailures_ - 1)));
    } else if (!recoveryTimer_.isActive()) {
        recoveryTimer_.start(0);
    }
}
QJsonObject DesktopRuntime::snapshot() const {
    auto runtimePreferences = runtimePreferences_;
    runtimePreferences.insert(QStringLiteral("systemProxyDesired"), persistedProxyIntent_);
    return {
        {QStringLiteral("profiles"), profiles_},
        {QStringLiteral("features"), featureState_},
        {QStringLiteral("connections"), connections_},
        {QStringLiteral("rules"), rules_},
        {QStringLiteral("providers"), providers_},
        {QStringLiteral("logs"), logs_},
        {QStringLiteral("droppedLogs"), droppedLogs_},
        {QStringLiteral("delays"), delays_},
        {QStringLiteral("epoch"), epoch_},
        {QStringLiteral("platform"), QSysInfo::prettyProductName()},
        {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("groups"), groups_},
        {QStringLiteral("selectedId"), selectedId_},
        {QStringLiteral("selectionGeneration"), repository_.manifest()
                                                    .value(QStringLiteral("selection"))
                                                    .toObject()
                                                    .value(QStringLiteral("generation"))},
        {QStringLiteral("selectedName"), profile(selectedId_).value(QStringLiteral("name"))},
        {QStringLiteral("status"), status_},
        {QStringLiteral("busy"), busy_},
        {QStringLiteral("ready"), engine_.ready()},
        {QStringLiteral("backgroundManaged"), backgroundManaged_},
        {QStringLiteral("recovering"), recoveryTimer_.isActive()},
        {QStringLiteral("reconciliation"),
         QJsonObject{{QStringLiteral("requests"), QString::number(reconcileRequests_)},
                     {QStringLiteral("completed"), QString::number(reconcileCount_)}}},
        {QStringLiteral("corePid"), static_cast<int>(engine_.pid())},
        {QStringLiteral("coreUptimeSeconds"),
         engine_.ready() && coreUptime_.isValid() ? coreUptime_.elapsed() / 1000 : 0},
        {QStringLiteral("tunState"),
         engine_.tunActive() ? QStringLiteral("On")
         : runtimePreferences_.value(QStringLiteral("tunDesired")).toBool()
             ? QStringLiteral("AuthorizationRequired")
             : QStringLiteral("Off")},
        {QStringLiteral("readOnly"), store_.readOnly()},
        {QStringLiteral("systemProxy"), proxyLease_.active()},
        {QStringLiteral("proxyConflict"), proxyLease_.conflict()},
        {QStringLiteral("error"), lastError_},
        {QStringLiteral("configIssues"), configIssues_},
        {QStringLiteral("errors"), errors_},
        {QStringLiteral("testResult"), testResult_},
        {QStringLiteral("testTarget"), testTarget_},
        {QStringLiteral("operation"), operationSnapshot_},
        {QStringLiteral("effectivePreview"), effectivePreview_},
        {QStringLiteral("preferences"), runtimePreferences},
        {QStringLiteral("coreVersion"), engine_.ready() ? QStringLiteral("v1.19.33") : QString{}},
        {QStringLiteral("dataDirectory"), store_.root()},
        {QStringLiteral("dataUrl"), QUrl::fromLocalFile(store_.root()).toString()}};
}
void DesktopRuntime::publish() {
    if (!busy_ && !updatingSubscription_.isEmpty())
        completeSubscriptionUpdate(lastError_.isEmpty() ? QStringLiteral("Updated")
                                                        : QStringLiteral("Failed"));
    emit changed(snapshot());
}
void DesktopRuntime::configureSubscription(const QString& id, int intervalMinutes, bool autoApply) {
    if (busy_ || store_.readOnly() ||
        profile(id).value(QStringLiteral("secretRef")).toString().isEmpty() ||
        (intervalMinutes != 0 && (intervalMinutes < 15 || intervalMinutes > 10080))) {
        error(QStringLiteral("InvalidSubscriptionSchedule"));
        return;
    }
    auto catalog = profiles_;
    for (qsizetype i = 0; i < catalog.size(); ++i) {
        auto item = catalog[i].toObject();
        if (item.value(QStringLiteral("id")) != id)
            continue;
        item.insert(QStringLiteral("updateIntervalMinutes"), intervalMinutes);
        item.insert(QStringLiteral("autoApply"), autoApply);
        item.insert(QStringLiteral("nextUpdateAt"),
                    intervalMinutes ? clock_().addSecs(intervalMinutes * 60).toString(Qt::ISODate)
                                    : QString{});
        catalog[i] = item;
    }
    if (saveCatalog(catalog)) {
        lastError_.clear();
        scheduleSubscriptions();
        publish();
    }
}
void DesktopRuntime::scheduleSubscriptions() {
    subscriptionTimer_.stop();
    if (shuttingDown_ || store_.readOnly())
        return;
    qint64 delay = 15 * 60 * 1000;
    bool scheduled = false;
    for (const auto& value : profiles_) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("secretRef")).toString().isEmpty() ||
            item.value(QStringLiteral("updateIntervalMinutes")).toInt() <= 0)
            continue;
        scheduled = true;
        const auto due = QDateTime::fromString(
            item.value(QStringLiteral("nextUpdateAt")).toString(), Qt::ISODate);
        delay = std::min(delay, due.isValid() ? clock_().msecsTo(due) : qint64{0});
    }
    if (scheduled)
        subscriptionTimer_.start(static_cast<int>(std::max<qint64>(1000, delay)));
}
void DesktopRuntime::completeSubscriptionUpdate(const QString& result) {
    const auto id = std::exchange(updatingSubscription_, {});
    auto catalog = profiles_;
    for (qsizetype i = 0; i < catalog.size(); ++i) {
        auto item = catalog[i].toObject();
        if (item.value(QStringLiteral("id")) != id)
            continue;
        const bool success = result == QStringLiteral("Updated");
        const auto failures =
            success ? 0
                    : std::clamp(item.value(QStringLiteral("updateFailures")).toInt(), 0, 5) + 1;
        const auto interval = item.value(QStringLiteral("updateIntervalMinutes")).toInt();
        const int retrySeconds = lastError_ == QStringLiteral("SubscriptionDownloadFailed")
                                     ? std::min(3600, 300 << std::max(0, failures - 1))
                                     : std::max(3600, interval * 60);
        item.insert(QStringLiteral("lastUpdateResult"), result);
        item.insert(QStringLiteral("lastUpdateError"), result == QStringLiteral("Cancelled")
                                                           ? QStringLiteral("UserStopped")
                                                       : success ? QString{}
                                                                 : lastError_);
        item.insert(QStringLiteral("lastAttemptAt"), clock_().toString(Qt::ISODate));
        item.insert(QStringLiteral("updateFailures"), failures);
        item.insert(
            QStringLiteral("nextUpdateAt"),
            interval > 0
                ? clock_().addSecs(success ? interval * 60 : retrySeconds).toString(Qt::ISODate)
                : QString{});
        catalog[i] = item;
    }
    (void)saveCatalog(catalog);
    scheduleSubscriptions();
}
void DesktopRuntime::error(const QString& code) {
    appendLog(QStringLiteral("App"), QStringLiteral("error"), code);
    lastError_ = code;
    configIssues_ = {};
    errors_.append(
        QJsonObject{{QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {QStringLiteral("code"), code}});
    while (errors_.size() > 100) {
        errors_.removeFirst();
    }
    if (!store_.readOnly()) {
        const auto saved = store_.write(QStringLiteral("logs/errors.json"),
                                        {{QStringLiteral("entries"), errors_}});
        Q_UNUSED(saved)
    }
    publish();
}
void DesktopRuntime::configError(const domain::Error& failure) {
    error(failure.code);
    for (const auto& issue : failure.configIssues) {
        configIssues_.append(QJsonObject{{QStringLiteral("code"), issue.code},
                                         {QStringLiteral("path"), issue.path},
                                         {QStringLiteral("feature"), issue.feature},
                                         {QStringLiteral("phase"), issue.phase}});
    }
    publish();
}
QString DesktopRuntime::coreExecutable() const {
    const auto directory = QCoreApplication::applicationDirPath();
    const auto packaged = QDir(directory).filePath(QStringLiteral("../core/mihomo.exe"));
    if (QFileInfo::exists(packaged)) {
        return QFileInfo(packaged).absoluteFilePath();
    }
#ifdef QUICKCLASH_DEVELOPMENT_CORE
    return QStringLiteral(QUICKCLASH_DEVELOPMENT_CORE);
#else
    return QDir(directory).filePath(QStringLiteral("core/mihomo.exe"));
#endif
}
domain::RuntimePreferences DesktopRuntime::preferences() const {
    domain::RuntimePreferences value;
    const auto mode = runtimePreferences_.value(QStringLiteral("mode")).toString();
    value.mode = mode == QStringLiteral("global")   ? domain::ProxyMode::Global
                 : mode == QStringLiteral("direct") ? domain::ProxyMode::Direct
                                                    : domain::ProxyMode::Rule;
    value.mixedPort =
        static_cast<quint16>(runtimePreferences_.value(QStringLiteral("mixedPort")).toInt(7890));
    value.controllerPort = static_cast<quint16>(
        runtimePreferences_.value(QStringLiteral("controllerPort")).toInt(9090));
    value.dnsMode =
        runtimePreferences_.value(QStringLiteral("dnsMode")).toString(QStringLiteral("profile"));
    return value;
}
QJsonObject DesktopRuntime::profile(const QString& id) const {
    for (const auto& value : profiles_) {
        if (value.toObject().value(QStringLiteral("id")) == id) {
            return value.toObject();
        }
    }
    return {};
}
bool DesktopRuntime::saveCatalog(const QJsonArray& profiles) {
    const auto result =
        store_.write(QStringLiteral("data/catalog.json"), {{QStringLiteral("profiles"), profiles}});
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
        return false;
    }
    profiles_ = profiles;
    return true;
}
void DesktopRuntime::importFile(const QString& url, const QString& name) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const auto fileUrl = QUrl(url);
    if (!fileUrl.isLocalFile()) {
        error(QStringLiteral("LocalFileRequired"));
        return;
    }
    QFile file(fileUrl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > kSourceLimit) {
        error(QStringLiteral("ProfileReadFailed"));
        return;
    }
    const auto bytes = file.read(kSourceLimit + 1);
    if (bytes.size() > kSourceLimit || file.error() != QFileDevice::NoError) {
        error(QStringLiteral("ProfileReadFailed"));
        return;
    }
    resolveImport(bytes, name.isEmpty() ? QFileInfo(file).completeBaseName() : name, {});
}
void DesktopRuntime::addSubscription(const QString& url, const QString& name) {
    download(url, name);
}
void DesktopRuntime::updateProfile(const QString& id) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const auto item = profile(id);
    if (item.value(QStringLiteral("secretRef")).toString().isEmpty()) {
        error(QStringLiteral("ProfileNotFound"));
        return;
    }
    updatingSubscription_ = id;
    lastError_.clear();
    const auto secret = credentials_.read(item.value(QStringLiteral("secretRef")).toString());
    if (const auto* failure = std::get_if<domain::Error>(&secret)) {
        error(failure->code);
        return;
    }
    const auto bytes = std::get<QByteArray>(secret);
    const auto envelope = QJsonDocument::fromJson(bytes).object();
    download(envelope.isEmpty() ? QString::fromUtf8(bytes)
                                : envelope.value(QStringLiteral("url")).toString(),
             item.value(QStringLiteral("name")).toString(), id,
             envelope.value(QStringLiteral("headers")).toObject());
}
void DesktopRuntime::changeProfileSource(const QString& id, const QString& url,
                                         const QString& headers) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const auto item = profile(id);
    const auto document = headers.trimmed().isEmpty() ? QJsonDocument(QJsonObject{})
                                                      : QJsonDocument::fromJson(headers.toUtf8());
    if (item.isEmpty() || !document.isObject() || headers.size() > 8192) {
        error(QStringLiteral("InvalidSubscriptionHeaders"));
        return;
    }
    updatingSubscription_ = id;
    lastError_.clear();
    download(url, item.value(QStringLiteral("name")).toString(), id, document.object());
}
void DesktopRuntime::download(const QString& url, const QString& name, const QString& id,
                              const QJsonObject& headers) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const auto normalizedUrl = url.trimmed();
    const QUrl source(normalizedUrl);
    if (!source.isValid() || source.scheme() != QStringLiteral("https") ||
        source.host().isEmpty() || !source.userInfo().isEmpty() || source.hasFragment() ||
        url.size() > 8192) {
        error(QStringLiteral("HttpsSubscriptionRequired"));
        return;
    }
    if (headers.size() > 16) {
        error(QStringLiteral("InvalidSubscriptionHeaders"));
        return;
    }
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const auto key = it.key().toLower();
        if ((key != QStringLiteral("authorization") && key != QStringLiteral("user-agent") &&
             key != QStringLiteral("accept")) ||
            !it.value().isString() || it.value().toString().size() > 4096 ||
            it.value().toString().contains(QLatin1Char('\r')) ||
            it.value().toString().contains(QLatin1Char('\n'))) {
            error(QStringLiteral("InvalidSubscriptionHeaders"));
            return;
        }
    }
    busy_ = true;
    const auto generation = ++operationGeneration_;
    publish();
    QNetworkRequest request(source);
    // Standard subscription endpoints negotiate their output from the client identity.
    // Preserve the original query (including signatures) instead of guessing format flags.
    request.setRawHeader("User-Agent", "clash.meta/1.19.33 QuickClash/0.1.0");
    request.setRawHeader("Accept", "application/yaml, text/yaml, text/plain;q=0.9, */*;q=0.1");
    for (auto it = headers.begin(); it != headers.end(); ++it)
        request.setRawHeader(it.key().toLatin1(), it.value().toString().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto* reply = directNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(kSourceLimit + 1);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes] {
        bytes->append(reply->read(kSourceLimit + 1 - bytes->size()));
        if (bytes->size() > kSourceLimit) {
            reply->abort();
        }
    });
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->start(20000);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, generation, normalizedUrl, name, id, headers] {
                reply->deleteLater();
                if (generation != operationGeneration_) {
                    return;
                }
                busy_ = false;
                bytes->append(reply->read(kSourceLimit + 1 - bytes->size()));
                if (bytes->size() > kSourceLimit) {
                    error(QStringLiteral("SubscriptionTooLarge"));
                    return;
                }
                const auto status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (status >= 300 && status < 400) {
                    error(QStringLiteral("SubscriptionRedirectUnsupported"));
                    return;
                }
                if (reply->error() != QNetworkReply::NoError || status != 200) {
                    error(status == 401 || status == 403
                              ? QStringLiteral("SubscriptionAuthenticationFailed")
                              : QStringLiteral("SubscriptionDownloadFailed"));
                    return;
                }
                // Some endpoints serve YAML with text/html. Inspect the bounded body, not MIME.
                const auto payloadError = infra::subscriptionPayloadError(*bytes);
                if (!payloadError.isEmpty()) {
                    error(payloadError);
                    return;
                }
                const auto secretRef = QUuid::createUuid().toString(QUuid::WithoutBraces);
                {
                    const auto secured = credentials_.write(
                        secretRef, QJsonDocument(QJsonObject{{QStringLiteral("url"), normalizedUrl},
                                                             {QStringLiteral("headers"), headers}})
                                       .toJson(QJsonDocument::Compact));
                    if (const auto* failure = std::get_if<domain::Error>(&secured)) {
                        error(failure->code);
                        return;
                    }
                }
                QJsonObject metadata, usage;
                const auto usageHeader = reply->rawHeader("subscription-userinfo");
                if (usageHeader.size() <= 1024) {
                    for (const auto& field : usageHeader.split(';')) {
                        const auto pair = field.trimmed().split('=');
                        if (pair.size() != 2 ||
                            !QList<QByteArray>{"upload", "download", "total", "expire"}.contains(
                                pair[0]))
                            continue;
                        bool valid = false;
                        const auto value = pair[1].toULongLong(&valid);
                        if (valid && value <= 9007199254740991ULL)
                            usage.insert(QString::fromLatin1(pair[0]), QString::number(value));
                    }
                }
                metadata.insert(QStringLiteral("usage"), usage);
                resolveImport(*bytes, name, secretRef, id, metadata);
            });
}
void DesktopRuntime::validate(const QByteArray& semantic, std::function<void(bool)> completion) {
    lastError_.clear();
    QFile binary(coreExecutable());
    QCryptographicHash checksum(QCryptographicHash::Sha256);
    if (!binary.open(QIODevice::ReadOnly) || !checksum.addData(&binary) ||
        checksum.result().toHex() !=
            QByteArrayLiteral("1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a")) {
        error(QStringLiteral("CoreIntegrityFailed"));
        completion(false);
        return;
    }
    const auto stagingRoot = store_.path(QStringLiteral("cache/validation"));
    if (!std::holds_alternative<QString>(stagingRoot) ||
        !QDir().mkpath(std::get<QString>(stagingRoot))) {
        completion(false);
        return;
    }
    staging_ = std::make_unique<QTemporaryDir>(std::get<QString>(stagingRoot) +
                                               QStringLiteral("/check_XXXXXX"));
    if (!staging_->isValid() || std::holds_alternative<domain::Error>(
                                    infra::restrictToCurrentUser(staging_->path(), true))) {
        staging_.reset();
        completion(false);
        return;
    }
    const auto materialized = infra::materializeSession({semantic, {}}, newSecret());
    QFile config(staging_->filePath(QStringLiteral("config.yaml")));
    const auto copied = staging_->filePath(QStringLiteral("mihomo.exe"));
    if (!std::holds_alternative<QByteArray>(materialized) || !config.open(QIODevice::WriteOnly) ||
        config.write(std::get<QByteArray>(materialized)) !=
            std::get<QByteArray>(materialized).size() ||
        !QFile::copy(coreExecutable(), copied)) {
        staging_.reset();
        completion(false);
        return;
    }
    config.close();
    const auto resources = infra::prepareGeoResources(
        semantic, staging_->path(),
        runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject(), store_.root());
    if (const auto* failure = std::get_if<domain::Error>(&resources)) {
        staging_.reset();
        error(failure->code);
        completion(false);
        return;
    }
    validationCompletion_ = std::move(completion);
    const auto launched =
        validator_.start(copied,
                         {QStringLiteral("-t"), QStringLiteral("-f"), config.fileName(),
                          QStringLiteral("-d"), staging_->path()},
                         staging_->path(), true);
    if (const auto* failure = std::get_if<domain::Error>(&launched)) {
        auto callback = std::move(validationCompletion_);
        staging_.reset();
        error(failure->code);
        callback(false);
        return;
    }
    validationDeadline_.start();
}
void DesktopRuntime::importBytes(const QByteArray& bytes, const QString& name,
                                 const QString& secretRef, const QString& existingId,
                                 const QByteArray& original, const QJsonObject& metadata) {
    if (profiles_.size() >= 100 && existingId.isEmpty()) {
        error(QStringLiteral("ProfileLimitExceeded"));
        return;
    }
    const auto preflight = infra::preflightConfig(bytes, preferences());
    if (const auto* failure = std::get_if<domain::Error>(&preflight)) {
        configError(*failure);
        return;
    }
    busy_ = true;
    const auto generation = ++operationGeneration_;
    publish();
    validate(std::get<infra::PreflightConfig>(preflight).semanticYaml, [this, generation, bytes,
                                                                        name, secretRef, existingId,
                                                                        original,
                                                                        metadata](bool valid) {
        if (generation != operationGeneration_) {
            return;
        }
        busy_ = false;
        if (!valid) {
            error(lastError_.isEmpty() ? QStringLiteral("CoreConfigValidationFailed") : lastError_);
            return;
        }
        const auto id =
            existingId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingId;
        const auto sourceRef = QStringLiteral("data/profiles/%1/%2.yaml")
                                   .arg(id, QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto source = original.isEmpty() ? bytes : original;
        const auto saved = store_.writeBytes(sourceRef, source, true);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            error(failure->code);
            return;
        }
        const auto previous = profile(existingId);
        QJsonObject item = previous;
        const QJsonObject imported{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"),
             name.trimmed().left(80).isEmpty() ? tr("New profile") : name.trimmed().left(80)},
            {QStringLiteral("sourceRef"), sourceRef},
            {QStringLiteral("sourceHash"), hash(source)},
            {QStringLiteral("secretRef"), secretRef},
            {QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
        for (auto it = imported.begin(); it != imported.end(); ++it)
            item.insert(it.key(), it.value());
        for (auto it = metadata.begin(); it != metadata.end(); ++it)
            item.insert(it.key(), it.value());
        if (!original.isEmpty()) {
            const auto snapshotRef = sourceRef + QStringLiteral(".snapshot.yaml");
            const auto result = store_.writeBytes(snapshotRef, bytes, true);
            if (const auto* failure = std::get_if<domain::Error>(&result)) {
                error(failure->code);
                return;
            }
            item.insert(QStringLiteral("snapshotRef"), snapshotRef);
            item.insert(QStringLiteral("snapshotHash"), hash(bytes));
        } else {
            item.remove(QStringLiteral("snapshotRef"));
            item.remove(QStringLiteral("snapshotHash"));
        }
        if (!previous.isEmpty() && (previous.value(QStringLiteral("sourceHash")) !=
                                        item.value(QStringLiteral("sourceHash")) ||
                                    previous.value(QStringLiteral("snapshotHash")) !=
                                        item.value(QStringLiteral("snapshotHash")))) {
            auto history = previous.value(QStringLiteral("history")).toArray();
            auto version = previous;
            version.remove(QStringLiteral("history"));
            history.prepend(version);
            while (history.size() > 5)
                history.removeLast();
            item.insert(QStringLiteral("history"), history);
        }
        if (previous.isEmpty() && !secretRef.isEmpty()) {
            item.insert(QStringLiteral("updateIntervalMinutes"), 360);
            item.insert(QStringLiteral("autoApply"), true);
            item.insert(QStringLiteral("nextUpdateAt"),
                        clock_().addSecs(360 * 60).toString(Qt::ISODate));
            item.insert(QStringLiteral("lastUpdateResult"), QStringLiteral("Updated"));
        }
        const bool changed = previous.value(QStringLiteral("sourceHash")) != hash(source) ||
                             previous.value(QStringLiteral("snapshotHash")) !=
                                 item.value(QStringLiteral("snapshotHash"));
        const bool activate =
            selectedId_.isEmpty() ||
            (id == selectedId_ && item.value(QStringLiteral("autoApply")).toBool() &&
             (changed || item.value(QStringLiteral("pendingApply")).toBool()));
        item.insert(QStringLiteral("pendingApply"),
                    changed || item.value(QStringLiteral("pendingApply")).toBool());
        auto catalog = profiles_;
        bool replaced = false;
        for (qsizetype i = 0; i < catalog.size(); ++i) {
            if (catalog[i].toObject().value(QStringLiteral("id")) == id) {
                catalog[i] = item;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            catalog.append(item);
        }
        if (saveCatalog(catalog)) {
            collectGarbage();
            lastError_.clear();
            configIssues_ = {};
            scheduleSubscriptions();
            if (activate) {
                applyProfile(id, engine_.ready());
                return;
            }
            publish();
        }
    });
}
void DesktopRuntime::selectProfile(const QString& id) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    backgroundSuppressed_ = false;
    recoveryFailures_ = 0;
    recoveryTimer_.stop();
    applyProfile(id, engine_.ready());
}
void DesktopRuntime::renameProfile(const QString& id, const QString& name) {
    if (busy_ || name.trimmed().isEmpty()) {
        return;
    }
    auto catalog = profiles_;
    for (qsizetype i = 0; i < catalog.size(); ++i) {
        auto item = catalog[i].toObject();
        if (item.value(QStringLiteral("id")) == id) {
            item.insert(QStringLiteral("name"), name.trimmed().left(80));
            catalog[i] = item;
        }
    }
    if (saveCatalog(catalog)) {
        publish();
    }
}
void DesktopRuntime::deleteProfile(const QString& id) {
    if (busy_ || store_.readOnly() || profile(id).isEmpty()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    if (id == selectedId_) {
        stop();
        if (!stopFailure_.isEmpty())
            return;
        auto target = repository_.manifest();
        for (const auto* key : {"selection", "activeRevision", "lastGoodRevision"})
            target.insert(QLatin1String(key), QJsonValue::Null);
        const auto prepared = repository_.prepare(target, QStringLiteral("Select"), false);
        if (const auto* failure = std::get_if<domain::Error>(&prepared)) {
            error(failure->code);
            return;
        }
        const auto committed = repository_.commit();
        if (const auto* failure = std::get_if<domain::Error>(&committed)) {
            error(failure->code);
            return;
        }
        selectedId_.clear();
        effectivePreview_.clear();
    }
    auto catalog = profiles_;
    for (qsizetype i = catalog.size(); i > 0; --i) {
        if (catalog[i - 1].toObject().value(QStringLiteral("id")) == id) {
            catalog.removeAt(i - 1);
        }
    }
    if (saveCatalog(catalog)) {
        collectGarbage();
        publish();
    }
}
void DesktopRuntime::applyProfile(const QString& id, bool startAfter) {
    backgroundSuppressed_ = false;
    effectivePreview_.clear();
    const auto item = profile(id);
    if (item.isEmpty()) {
        error(QStringLiteral("ProfileNotFound"));
        return;
    }
    const auto selected = repository_.manifest().value(QStringLiteral("selection")).toObject();
    const bool adjustSelected = id == selectedId_ && !previousPreferences_.isEmpty();
    const auto source =
        store_.readBytes(adjustSelected ? selected.value(QStringLiteral("artifactRef")).toString()
                                        : item.value(item.contains(QStringLiteral("snapshotRef"))
                                                         ? QStringLiteral("snapshotRef")
                                                         : QStringLiteral("sourceRef"))
                                              .toString());
    if (const auto* failure = std::get_if<domain::Error>(&source)) {
        restorePreferences();
        error(failure->code);
        return;
    }
    if (hash(std::get<QByteArray>(source)) !=
        (adjustSelected ? selected.value(QStringLiteral("effectiveHash"))
                        : item.value(item.contains(QStringLiteral("snapshotRef"))
                                         ? QStringLiteral("snapshotHash")
                                         : QStringLiteral("sourceHash")))
            .toString()) {
        restorePreferences();
        error(QStringLiteral("SourceIntegrityFailed"));
        return;
    }
    const auto composed = infra::preflightConfig(std::get<QByteArray>(source), preferences());
    if (const auto* failure = std::get_if<domain::Error>(&composed)) {
        restorePreferences();
        configError(*failure);
        return;
    }
    busy_ = true;
    status_ = QStringLiteral("Validating");
    const auto generation = ++operationGeneration_;
    const auto semantic = std::get<infra::PreflightConfig>(composed).semanticYaml;
    publish();
    validate(semantic, [this, generation, semantic, id, startAfter](bool valid) {
        if (generation != operationGeneration_) {
            return;
        }
        if (!valid) {
            restorePreferences();
            busy_ = false;
            status_ = engine_.ready() ? QStringLiteral("Ready") : QStringLiteral("Stopped");
            error(lastError_.isEmpty() ? QStringLiteral("CoreConfigValidationFailed") : lastError_);
            return;
        }
        const auto revision = QString::number(std::max(
            QDateTime::currentMSecsSinceEpoch(), repository_.manifest()
                                                         .value(QStringLiteral("selection"))
                                                         .toObject()
                                                         .value(QStringLiteral("selectedRevision"))
                                                         .toString()
                                                         .toLongLong() +
                                                     1));
        const auto artifact = QStringLiteral("data/runtime/%1/config.yaml").arg(revision);
        const auto pref = QStringLiteral("data/preferences/%1.json").arg(revision);
        const auto resources = QStringLiteral("data/resources/%1.json").arg(revision);
        auto result = store_.writeBytes(artifact, semantic, true);
        if (std::holds_alternative<std::monostate>(result)) {
            result = store_.write(pref, runtimePreferences_);
        }
        if (std::holds_alternative<std::monostate>(result)) {
            result = store_.write(
                resources,
                infra::geoResourceManifest(
                    semantic, runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject()));
        }
        if (const auto* failure = std::get_if<domain::Error>(&result)) {
            restorePreferences();
            busy_ = false;
            error(failure->code);
            return;
        }
        candidate_ = repository_.manifest();
        const QJsonObject selection{
            {QStringLiteral("profileId"), id},
            {QStringLiteral("sourceHash"), id == selectedId_ && !previousPreferences_.isEmpty()
                                               ? repository_.manifest()
                                                     .value(QStringLiteral("selection"))
                                                     .toObject()
                                                     .value(QStringLiteral("sourceHash"))
                                               : profile(id).value(QStringLiteral("sourceHash"))},
            {QStringLiteral("selectedRevision"), revision},
            {QStringLiteral("artifactRef"), artifact},
            {QStringLiteral("effectiveHash"), hash(semantic)},
            {QStringLiteral("preferenceSnapshotRef"), pref},
            {QStringLiteral("resourceManifestRef"), resources},
            {QStringLiteral("generation"), QString::number(repository_.generation() + 1)}};
        candidate_.insert(QStringLiteral("selection"), selection);
        if (startAfter) {
            candidate_.insert(QStringLiteral("activeRevision"), revision);
            candidate_.insert(QStringLiteral("lastGoodRevision"), revision);
        }
        previous_ = repository_.manifest();
        const auto prepared = repository_.prepare(
            candidate_, startAfter ? QStringLiteral("Apply") : QStringLiteral("Select"),
            engine_.ready());
        if (const auto* failure = std::get_if<domain::Error>(&prepared)) {
            restorePreferences();
            busy_ = false;
            error(failure->code);
            return;
        }
        candidate_ = repository_.pending().value(QStringLiteral("target")).toObject();
        applyingId_ = id;
        applying_ = true;
        startAfter_ = startAfter;
        if (!startAfter) {
            confirmed();
            return;
        }
        previouslyProxied_ = proxyLease_.active();
        if (previousPreferences_.isEmpty()) {
            const auto previousPref =
                store_.read(previous_.value(QStringLiteral("selection"))
                                .toObject()
                                .value(QStringLiteral("preferenceSnapshotRef"))
                                .toString());
            if (const auto* value = std::get_if<std::optional<QJsonObject>>(&previousPref);
                value && *value)
                previousPreferences_ = **value;
        }
        proxyDesired_ = previouslyProxied_;
        if (previouslyProxied_) {
            const auto suspended = proxyLease_.suspend();
            if (const auto* failure = std::get_if<domain::Error>(&suspended)) {
                // Core has not changed; preserve the current session on a lease conflict.
                const auto rolledBack = repository_.rollback();
                Q_UNUSED(rolledBack)
                restorePreferences();
                applying_ = false;
                busy_ = false;
                status_ = QStringLiteral("Ready");
                error(failure->code);
                return;
            }
        }
        const auto staged = repository_.phase(QStringLiteral("CoreApplying"));
        if (const auto* failure = std::get_if<domain::Error>(&staged)) {
            failedApply(failure->code);
            return;
        }
        engine_.stop();
        cleanupSession();
        launch();
    });
}
void DesktopRuntime::launch() {
    featureRequest_ = false;
    cancelDelayTests();
    closingConnections_ = false;
    connections_ = {};
    rules_ = {};
    providers_ = {};
    delays_ = {};
    featureState_.remove(QStringLiteral("traffic"));
    connectionSampleMs_ = 0;
    const auto selection = candidate_.value(QStringLiteral("selection")).toObject();
    const auto semantic =
        store_.readBytes(selection.value(QStringLiteral("artifactRef")).toString());
    if (const auto* failure = std::get_if<domain::Error>(&semantic)) {
        failedApply(failure->code);
        return;
    }
    if (hash(std::get<QByteArray>(semantic)) !=
        selection.value(QStringLiteral("effectiveHash")).toString()) {
        failedApply(QStringLiteral("ArtifactIntegrityFailed"));
        return;
    }
    auto key = credentials_.read(QStringLiteral("c137006a-58f4-4d6c-a795-b987e368c820"));
    const auto keyPath =
        store_.path(QStringLiteral("data/credentials/c137006a-58f4-4d6c-a795-b987e368c820.blob"));
    if (std::holds_alternative<domain::Error>(key) && std::holds_alternative<QString>(keyPath) &&
        !QFileInfo::exists(std::get<QString>(keyPath))) {
        const auto bytes = newSecret();
        const auto saved =
            credentials_.write(QStringLiteral("c137006a-58f4-4d6c-a795-b987e368c820"), bytes);
        if (std::holds_alternative<std::monostate>(saved))
            key = bytes;
    }
    if (const auto* failure = std::get_if<domain::Error>(&key)) {
        failedApply(failure->code);
        return;
    }
    identities_ = infra::nodeIdentities(std::get<QByteArray>(semantic), std::get<QByteArray>(key));
    featureState_.insert(
        QStringLiteral("nodes"),
        infra::configurationSummary(std::get<QByteArray>(semantic)).value(QStringLiteral("nodes")));
    restoringNodes_ = true;
    restoredGroups_.clear();
    epoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    secret_ = newSecret();
    const auto materialized =
        infra::materializeSession({std::get<QByteArray>(semantic), {}}, secret_);
    if (const auto* failure = std::get_if<domain::Error>(&materialized)) {
        failedApply(failure->code);
        return;
    }
    const auto reference = QStringLiteral("data/sessions/%1/config.yaml").arg(epoch_);
    const auto result = store_.writeBytes(reference, std::get<QByteArray>(materialized), true);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        failedApply(failure->code);
        return;
    }
    const auto config = store_.path(reference);
    const auto coreDirectory = store_.path(QStringLiteral("data/core_state"));
    if (!std::holds_alternative<QString>(config) ||
        !std::holds_alternative<QString>(coreDirectory) ||
        !QDir().mkpath(std::get<QString>(coreDirectory))) {
        failedApply(QStringLiteral("SessionWriteFailed"));
        return;
    }
    const auto values = preferences();
    const auto manifest =
        store_.read(selection.value(QStringLiteral("resourceManifestRef")).toString());
    if (!std::holds_alternative<std::optional<QJsonObject>>(manifest) ||
        !std::get<std::optional<QJsonObject>>(manifest) ||
        *std::get<std::optional<QJsonObject>>(manifest) !=
            infra::geoResourceManifest(
                std::get<QByteArray>(semantic),
                runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject())) {
        failedApply(QStringLiteral("GeoResourceIntegrityFailed"));
        return;
    }
    const auto resources = infra::prepareGeoResources(
        std::get<QByteArray>(semantic), std::get<QString>(coreDirectory),
        runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject(), store_.root());
    if (const auto* failure = std::get_if<domain::Error>(&resources)) {
        failedApply(failure->code);
        return;
    }
    status_ = QStringLiteral("Starting");
    publish();
    engine_.start(coreExecutable(), std::get<QString>(config), std::get<QString>(coreDirectory),
                  secret_, values.mixedPort, values.controllerPort,
                  runtimePreferences_.value(QStringLiteral("mode")).toString(),
                  runtimePreferences_.value(QStringLiteral("tunDesired")).toBool());
}
void DesktopRuntime::confirmed() {
    QString networkFailure;
    if (startAfter_ && applying_ && !recovering_) {
        const auto result = repository_.phase(QStringLiteral("CoreConfirmed"));
        if (const auto* failure = std::get_if<domain::Error>(&result)) {
            failedApply(failure->code);
            return;
        }
    }
    if (proxyDesired_) {
        if (applying_) {
            const auto intent = repository_.phase(QStringLiteral("PlatformApplying"));
            if (const auto* failure = std::get_if<domain::Error>(&intent)) {
                failedApply(failure->code);
                return;
            }
        }
        const auto leased = proxyLease_.acquire(preferences().mixedPort, epoch_);
        if (const auto* failure = std::get_if<domain::Error>(&leased)) {
            if (backgroundManaged_) {
                // A platform proxy failure must not take down the local forwarding engine.
                networkFailure = failure->code;
                proxyDesired_ = false;
                const auto released = proxyLease_.release();
                if (const auto* restoreFailure = std::get_if<domain::Error>(&released))
                    networkFailure = restoreFailure->code;
                const auto saved = saveProxyIntent(false);
                if (const auto* saveFailure = std::get_if<domain::Error>(&saved))
                    networkFailure = saveFailure->code;
                error(networkFailure);
            } else if (applying_)
                failedApply(failure->code);
            else {
                const auto released = proxyLease_.release();
                if (const auto* restoreFailure = std::get_if<domain::Error>(&released))
                    error(restoreFailure->code);
                engine_.stop();
                cleanupSession();
                busy_ = false;
                status_ = QStringLiteral("Failed");
                error(failure->code);
                finishOperation(domain::OperationState::Failed, failure->code);
            }
            if (!backgroundManaged_)
                return;
        }
    }
    if (recovering_) {
        recovering_ = false;
        applying_ = false;
        busy_ = false;
        status_ = QStringLiteral("Ready");
        const auto result = repository_.rollback();
        if (const auto* failure = std::get_if<domain::Error>(&result)) {
            error(failure->code);
        }
        publish();
        finishOperation(domain::OperationState::Failed, lastError_);
        return;
    }
    if (!applying_) {
        busy_ = false;
        status_ = QStringLiteral("Ready");
        publish();
        finishOperation(domain::OperationState::Succeeded);
        return;
    }
    if (proxyDesired_) {
        previousProxyIntent_ = persistedProxyIntent_;
        const auto saved = saveProxyIntent(true);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            failedApply(failure->code);
            return;
        }
    }
    const auto committed = repository_.commit();
    if (const auto* failure = std::get_if<domain::Error>(&committed)) {
        if (store_.readOnly()) {
            if (previousProxyIntent_) {
                const auto restored = saveProxyIntent(*previousProxyIntent_);
                previousProxyIntent_.reset();
                if (const auto* intentFailure = std::get_if<domain::Error>(&restored))
                    error(intentFailure->code);
            }
            applying_ = false;
            busy_ = false;
            engine_.stop();
            const auto released = proxyLease_.release();
            Q_UNUSED(released)
            cleanupSession();
            status_ = QStringLiteral("Failed");
            error(failure->code);
            finishOperation(domain::OperationState::Failed, failure->code);
            return;
        }
        failedApply(failure->code);
        return;
    }
    previousProxyIntent_.reset();
    selectedId_ = applyingId_;
    auto catalog = profiles_;
    for (qsizetype i = 0; i < catalog.size(); ++i) {
        auto item = catalog[i].toObject();
        if (item.value(QStringLiteral("id")) == selectedId_ &&
            candidate_.value(QStringLiteral("selection"))
                    .toObject()
                    .value(QStringLiteral("sourceHash")) ==
                item.value(QStringLiteral("sourceHash")) &&
            item.value(QStringLiteral("pendingApply")).toBool()) {
            item.insert(QStringLiteral("pendingApply"), false);
            catalog[i] = item;
        }
    }
    const bool catalogSaved = saveCatalog(catalog);
    previousPreferences_ = {};
    applying_ = false;
    busy_ = false;
    if (catalogSaved && networkFailure.isEmpty())
        lastError_.clear();
    configIssues_ = {};
    status_ = engine_.ready() ? QStringLiteral("Ready") : QStringLiteral("Stopped");
    if (!engine_.ready())
        loadOfflineGroups();
    else
        pollFeatures();
    testResult_.clear();
    ++testGeneration_;
    featureState_.insert(QStringLiteral("websiteTest"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("Stale")}});
    testQueue_.clear();
    finishOperation(networkFailure.isEmpty() ? domain::OperationState::Succeeded
                                             : domain::OperationState::Failed,
                    networkFailure);
    collectGarbage();
    scheduleBackgroundStart();
    publish();
}
void DesktopRuntime::failedApply(const QString& code) {
    engine_.stop();
    cleanupSession();
    restorePreferences();
    if (previousProxyIntent_) {
        const auto restored = saveProxyIntent(*previousProxyIntent_);
        previousProxyIntent_.reset();
        if (const auto* failure = std::get_if<domain::Error>(&restored))
            error(failure->code);
    }
    error(code);
    if (applying_ && !recovering_ &&
        repository_.pending().value(QStringLiteral("hadRunningSession")).toBool()) {
        recovering_ = true;
        proxyDesired_ = previouslyProxied_;
        candidate_ = previous_;
        const auto phase = repository_.phase(QStringLiteral("Compensating"));
        if (std::holds_alternative<std::monostate>(phase)) {
            launch();
            return;
        }
    }
    recovering_ = false;
    applying_ = false;
    busy_ = false;
    status_ = QStringLiteral("Failed");
    const auto released = proxyLease_.release();
    if (const auto* failure = std::get_if<domain::Error>(&released))
        error(failure->code);
    const auto result = repository_.rollback();
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
    }
    publish();
    finishOperation(domain::OperationState::Failed, code);
    scheduleBackgroundStart(true);
    publish();
}
void DesktopRuntime::start(bool systemProxy) {
    backgroundSuppressed_ = false;
    if (busy_ || engine_.ready()) {
        if (engine_.ready() && systemProxy)
            (void)setSystemProxy(true);
        return;
    }
    if (store_.readOnly() || repository_.recoveryRequired() || selectedId_.isEmpty()) {
        error(QStringLiteral("SelectValidConfigurationFirst"));
        return;
    }
    if (!operation_)
        (void)beginOperation(systemProxy ? QStringLiteral("start_proxy")
                                         : QStringLiteral("start_core"));
    if (!restartAttempt_)
        crashTimes_.clear();
    restartAttempt_ = false;
    proxyDesired_ = systemProxy;
    candidate_ = repository_.manifest();
    previous_ = candidate_;
    const auto selection = candidate_.value(QStringLiteral("selection")).toObject();
    candidate_.insert(QStringLiteral("activeRevision"),
                      selection.value(QStringLiteral("selectedRevision")));
    candidate_.insert(QStringLiteral("lastGoodRevision"),
                      selection.value(QStringLiteral("selectedRevision")));
    const auto prepared = repository_.prepare(candidate_, QStringLiteral("Apply"), false);
    if (const auto* failure = std::get_if<domain::Error>(&prepared)) {
        error(failure->code);
        finishOperation(domain::OperationState::Failed, failure->code);
        return;
    }
    candidate_ = repository_.pending().value(QStringLiteral("target")).toObject();
    applying_ = true;
    startAfter_ = true;
    applyingId_ = selectedId_;
    busy_ = true;
    const auto phase = repository_.phase(QStringLiteral("CoreApplying"));
    if (const auto* failure = std::get_if<domain::Error>(&phase)) {
        failedApply(failure->code);
        return;
    }
    launch();
}
domain::CommandResult DesktopRuntime::saveProxyIntent(bool desired) {
    if (desired == persistedProxyIntent_)
        return std::monostate{};
    const auto result = store_.write(QStringLiteral("config/proxy_intent.json"),
                                     {{QStringLiteral("desired"), desired}});
    if (std::holds_alternative<std::monostate>(result))
        persistedProxyIntent_ = desired;
    return result;
}
void DesktopRuntime::stop(domain::StopReason reason) {
    featureRequest_ = false;
    cancelDelayTests();
    closingConnections_ = false;
    connections_ = {};
    featureState_.remove(QStringLiteral("traffic"));
    if (reason == domain::StopReason::AppExit) {
        shuttingDown_ = true;
        subscriptionTimer_.stop();
    }
    // Internal stop is also used for deleting the selected profile and test teardown.
    backgroundSuppressed_ = true;
    if (!updatingSubscription_.isEmpty())
        completeSubscriptionUpdate(QStringLiteral("Cancelled"));
    stopFailure_.clear();
    finishOperation(domain::OperationState::Cancelled, QStringLiteral("UserStopped"));
    ++operationGeneration_;
    recoveryTimer_.stop();
    reconcileTimer_.stop();
    proxyDesired_ = false;
    if (previousProxyIntent_) {
        const auto restored = saveProxyIntent(*previousProxyIntent_);
        previousProxyIntent_.reset();
        if (const auto* failure = std::get_if<domain::Error>(&restored)) {
            stopFailure_ = failure->code;
            error(failure->code);
        }
    }
    if (reason == domain::StopReason::UserStop) {
        const auto saved = saveProxyIntent(false);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            stopFailure_ = failure->code;
            error(failure->code);
        }
    }
    if (downloadReply_)
        downloadReply_->abort();
    if (testReply_)
        testReply_->abort();
    testQueue_.clear();
    ++testGeneration_;
    validator_.stop();
    validationDeadline_.stop();
    validationCompletion_ = {};
    staging_.reset();
    const auto released = proxyLease_.release();
    if (const auto* failure = std::get_if<domain::Error>(&released)) {
        stopFailure_ = failure->code;
        error(failure->code);
    }
    engine_.stop();
    busy_ = false;
    pendingGroup_.clear();
    pendingMember_.clear();
    restoringNodes_ = false;
    applying_ = false;
    recovering_ = false;
    status_ = QStringLiteral("Stopped");
    groups_ = {};
    testResult_.clear();
    cleanupSession();
    restorePreferences();
    const auto result = repository_.rollback();
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
    }
    loadOfflineGroups();
    publish();
}
void DesktopRuntime::setTun(bool desired) {
    if (busy_ || selectedId_.isEmpty() || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    if (desired && !runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject().isEmpty()) {
        error(QStringLiteral("TunBundledGeoRequired"));
        return;
    }
    backgroundSuppressed_ = false;
    recoveryTimer_.stop();
    if (desired && !runtimePreferences_.value(QStringLiteral("tunDesired")).toBool()) {
        busy_ = true;
        publish();
        const auto generation = ++operationGeneration_;
        engine_.probeService([this, generation](domain::Result<QJsonObject> result) {
            if (generation != operationGeneration_)
                return;
            busy_ = false;
            if (const auto* failure = std::get_if<domain::Error>(&result)) {
                error(failure->code);
                return;
            }
            previousPreferences_ = runtimePreferences_;
            runtimePreferences_.insert(QStringLiteral("tunDesired"), true);
            applyProfile(selectedId_, true);
        });
        return;
    }
    previousPreferences_ = runtimePreferences_;
    runtimePreferences_.insert(QStringLiteral("tunDesired"), desired);
    applyProfile(selectedId_, backgroundManaged_ || engine_.ready());
}
void DesktopRuntime::cleanupSession() {
    if (!epoch_.isEmpty()) {
        const auto removed =
            store_.remove(QStringLiteral("data/sessions/%1/config.yaml").arg(epoch_));
        if (const auto* failure = std::get_if<domain::Error>(&removed)) {
            error(failure->code);
        }
    }
    secret_.fill('\0');
    secret_.clear();
    epoch_.clear();
}
void DesktopRuntime::collectGarbage() {
    if (store_.readOnly() || repository_.recoveryRequired() || applying_)
        return;
    const auto manifest = repository_.manifest();
    QSet<QString> revisions;
    revisions.insert(manifest.value(QStringLiteral("selection"))
                         .toObject()
                         .value(QStringLiteral("selectedRevision"))
                         .toString());
    revisions.insert(manifest.value(QStringLiteral("activeRevision")).toString());
    revisions.insert(manifest.value(QStringLiteral("lastGoodRevision")).toString());
    const auto runtimeFiles = store_.files(QStringLiteral("data/runtime"));
    if (!std::holds_alternative<QStringList>(runtimeFiles))
        return;
    QList<quint64> versions;
    for (const auto& file : std::get<QStringList>(runtimeFiles)) {
        if (file.endsWith(QStringLiteral("/config.yaml")))
            versions.append(file.section(QLatin1Char('/'), 2, 2).toULongLong());
    }
    std::sort(versions.begin(), versions.end(), std::greater<quint64>());
    for (qsizetype i = 0; i < std::min<qsizetype>(5, versions.size()); ++i)
        revisions.insert(QString::number(versions[i]));
    QSet<QString> sources;
    QSet<QString> credentialRefs{QStringLiteral("c137006a-58f4-4d6c-a795-b987e368c820"),
                                 QStringLiteral("e430ed25-78b8-47ea-a7b0-d746a5783891")};
    for (const auto& item : profiles_) {
        sources.insert(item.toObject().value(QStringLiteral("sourceRef")).toString());
        sources.insert(item.toObject().value(QStringLiteral("snapshotRef")).toString());
        for (const auto& version : item.toObject().value(QStringLiteral("history")).toArray()) {
            sources.insert(version.toObject().value(QStringLiteral("sourceRef")).toString());
            sources.insert(version.toObject().value(QStringLiteral("snapshotRef")).toString());
            credentialRefs.insert(version.toObject().value(QStringLiteral("secretRef")).toString());
        }
    }
    for (const auto& item : profiles_)
        credentialRefs.insert(item.toObject().value(QStringLiteral("secretRef")).toString());
    // Keep immutable Geo files referenced by retained preference revisions or the current
    // operation. Unreferenced downloads are safe to remove, never core-owned files.
    QSet<QString> geoFiles;
    const auto retainGeo = [&geoFiles](const QJsonObject& preferences) {
        for (const auto* key : {"geoSnapshot", "previousGeoSnapshot"}) {
            const auto snapshot = preferences.value(QLatin1String(key)).toObject();
            for (auto it = snapshot.begin(); it != snapshot.end(); ++it)
                geoFiles.insert(
                    it.value().toObject().value(QStringLiteral("sourceRef")).toString());
        }
    };
    retainGeo(runtimePreferences_);
    retainGeo(previousPreferences_);
    for (const auto& revision : revisions) {
        if (revision.isEmpty())
            continue;
        const auto pref = store_.read(QStringLiteral("data/preferences/%1.json").arg(revision));
        if (const auto* value = std::get_if<std::optional<QJsonObject>>(&pref); value && *value)
            retainGeo(**value);
    }
    if (!busy_) {
        const auto files = store_.files(QStringLiteral("data/geo"));
        if (const auto* listed = std::get_if<QStringList>(&files))
            for (const auto& file : *listed)
                if (!geoFiles.contains(file)) {
                    const auto removed = store_.remove(file);
                    Q_UNUSED(removed)
                }
    }
    for (const auto* directory :
         {"data/runtime", "data/preferences", "data/resources", "data/profiles", "data/sessions"}) {
        const auto listed = store_.files(QString::fromLatin1(directory));
        if (!std::holds_alternative<QStringList>(listed))
            continue;
        for (const auto& file : std::get<QStringList>(listed)) {
            bool keep = true;
            if (file.startsWith(QStringLiteral("data/runtime/")) &&
                file.endsWith(QStringLiteral("/config.yaml")))
                keep = revisions.contains(file.section(QLatin1Char('/'), 2, 2));
            else if (file.startsWith(QStringLiteral("data/preferences/")) ||
                     file.startsWith(QStringLiteral("data/resources/")))
                keep = revisions.contains(QFileInfo(file).completeBaseName());
            else if (file.startsWith(QStringLiteral("data/profiles/")))
                keep = sources.contains(file);
            else if (file.startsWith(QStringLiteral("data/sessions/")))
                keep = !epoch_.isEmpty() && file.section(QLatin1Char('/'), 2, 2) == epoch_;
            if (!keep) {
                const auto removed = store_.remove(file);
                Q_UNUSED(removed)
            }
        }
    }
    if (!busy_) {
        const auto blobs = store_.files(QStringLiteral("data/credentials"));
        if (const auto* files = std::get_if<QStringList>(&blobs)) {
            for (const auto& file : *files) {
                if (!credentialRefs.contains(QFileInfo(file).completeBaseName())) {
                    const auto removed = store_.remove(file);
                    Q_UNUSED(removed)
                }
            }
        }
    }
    const auto journals = store_.files(QStringLiteral("data/transactions"));
    if (const auto* files = std::get_if<QStringList>(&journals)) {
        auto ordered = *files;
        std::sort(ordered.begin(), ordered.end(),
                  [this](const QString& left, const QString& right) {
                      return QFileInfo(std::get<QString>(store_.path(left))).lastModified() >
                             QFileInfo(std::get<QString>(store_.path(right))).lastModified();
                  });
        for (qsizetype i = 32; i < ordered.size(); ++i) {
            const auto removed = store_.remove(ordered[i]);
            Q_UNUSED(removed)
        }
    }
}
void DesktopRuntime::restorePreferences() {
    if (!previousPreferences_.isEmpty())
        runtimePreferences_ = previousPreferences_;
    previousPreferences_ = {};
}
void DesktopRuntime::setMode(const QString& mode) {
    if (busy_ || selectedId_.isEmpty() ||
        (mode != QStringLiteral("rule") && mode != QStringLiteral("global") &&
         mode != QStringLiteral("direct"))) {
        return;
    }
    if (store_.readOnly()) {
        error(QStringLiteral("RepositoryReadOnly"));
        return;
    }
    previousPreferences_ = runtimePreferences_;
    runtimePreferences_.insert(QStringLiteral("mode"), mode);
    applyProfile(selectedId_, engine_.ready());
}
void DesktopRuntime::setPorts(int mixed, int controller) {
    if (store_.readOnly()) {
        error(QStringLiteral("RepositoryReadOnly"));
        return;
    }
    if (busy_ || mixed < 1024 || mixed > 65535 || controller < 1024 || controller > 65535 ||
        mixed == controller) {
        error(QStringLiteral("InvalidListenerPorts"));
        return;
    }
    previousPreferences_ = runtimePreferences_;
    runtimePreferences_.insert(QStringLiteral("mixedPort"), mixed);
    runtimePreferences_.insert(QStringLiteral("controllerPort"), controller);
    if (!selectedId_.isEmpty()) {
        applyProfile(selectedId_, engine_.ready());
    } else {
        const auto saved =
            store_.write(QStringLiteral("config/runtime_defaults.json"), runtimePreferences_);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            restorePreferences();
            error(failure->code);
        } else
            previousPreferences_ = {};
        publish();
    }
}
void DesktopRuntime::selectProxy(const QString& group, const QString& member) {
    if (busy_ || !identities_.contains(group) || !identities_.contains(member))
        return;
    bool present = false;
    for (const auto& item : groups_) {
        const auto value = item.toObject();
        if (value.value(QStringLiteral("name")) == group) {
            if (!value.value(QStringLiteral("canSelect")).toBool()) {
                error(QStringLiteral("ProxyGroupAutomatic"));
                return;
            }
            present = value.value(QStringLiteral("members")).toArray().contains(member);
        }
    }
    if (!present) {
        error(QStringLiteral("ProxyMemberNotFound"));
        return;
    }
    if (!engine_.ready()) {
        auto next = selections_;
        next.insert(group,
                    QJsonObject{{QStringLiteral("member"), member},
                                {QStringLiteral("profileId"), selectedId_},
                                {QStringLiteral("identity"), identities_.value(member)},
                                {QStringLiteral("groupIdentity"), identities_.value(group)}});
        const auto saved = store_.write(QStringLiteral("data/node_selections.json"),
                                        {{QStringLiteral("groups"), next}});
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            error(failure->code);
            return;
        }
        selections_ = next;
        loadOfflineGroups();
        publish();
        return;
    }
    busy_ = true;
    pendingGroup_ = group;
    pendingMember_ = member;
    testResult_.clear();
    testQueue_.clear();
    ++testGeneration_;
    featureState_.insert(QStringLiteral("websiteTest"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("Stale")}});
    if (testReply_) {
        const auto reply = testReply_;
        testReply_.clear();
        reply->abort();
    }
    engine_.select(group, member);
    publish();
}
void DesktopRuntime::refresh() {
    if (!busy_)
        engine_.refreshGroups();
    if (!engine_.ready())
        loadOfflineGroups();
    publish();
}
void DesktopRuntime::previewConfiguration() {
    const auto selection = repository_.manifest().value(QStringLiteral("selection")).toObject();
    if (selection.isEmpty()) {
        error(QStringLiteral("SelectValidConfigurationFirst"));
        return;
    }
    const auto bytes = store_.readBytes(selection.value(QStringLiteral("artifactRef")).toString());
    if (const auto* failure = std::get_if<domain::Error>(&bytes)) {
        error(failure->code);
        return;
    }
    if (hash(std::get<QByteArray>(bytes)) !=
        selection.value(QStringLiteral("effectiveHash")).toString()) {
        error(QStringLiteral("ArtifactIntegrityFailed"));
        return;
    }
    effectivePreview_ = infra::redactedConfig(std::get<QByteArray>(bytes));
    publish();
}
void DesktopRuntime::reapplySystemProxy() {
    if (busy_ || !engine_.ready()) {
        error(QStringLiteral("ReadyProxyRequired"));
        return;
    }
    if (!proxyLease_.conflict() || proxyLease_.active()) {
        error(QStringLiteral("SystemProxyConflictRequired"));
        return;
    }
    auto result = proxyLease_.reapply(preferences().mixedPort, epoch_);
    if (std::holds_alternative<std::monostate>(result))
        result = saveProxyIntent(true);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        const auto released = proxyLease_.release();
        if (const auto* restoreFailure = std::get_if<domain::Error>(&released))
            error(restoreFailure->code);
        proxyDesired_ = false;
        error(failure->code);
    } else {
        proxyDesired_ = true;
        lastError_.clear();
        configIssues_ = {};
        publish();
    }
}
void DesktopRuntime::reconcile() {
    if (shuttingDown_)
        return;
    ++reconcileRequests_;
    if (!reconcileTimer_.isActive()) {
        // Invalidate immediately: an old reply can finish during the debounce window.
        testResult_.clear();
        testQueue_.clear();
        ++testGeneration_;
        featureState_.insert(QStringLiteral("websiteTest"),
                             QJsonObject{{QStringLiteral("state"), QStringLiteral("Stale")}});
        if (testReply_) {
            const auto reply = testReply_;
            testReply_.clear();
            reply->abort();
        }
        cancelDelayTests();
        reconcileTimer_.start();
        publish();
    }
}
QUuid DesktopRuntime::setSystemProxy(bool desired) {
    if (busy_ || (desired && !engine_.ready())) {
        if (desired && !busy_) {
            const auto id = beginOperation(QStringLiteral("start_proxy"));
            start(true);
            if (!busy_ && !engine_.ready())
                finishOperation(domain::OperationState::Failed, lastError_);
            return id;
        }
        return {};
    }
    const auto id = beginOperation(desired ? QStringLiteral("system_proxy_on")
                                           : QStringLiteral("system_proxy_off"));
    auto result =
        desired ? proxyLease_.acquire(preferences().mixedPort, epoch_) : proxyLease_.release();
    if (std::holds_alternative<std::monostate>(result))
        result = saveProxyIntent(desired);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        if (desired) {
            const auto released = proxyLease_.release();
            if (const auto* restoreFailure = std::get_if<domain::Error>(&released))
                error(restoreFailure->code);
            proxyDesired_ = false;
        }
        error(failure->code);
        finishOperation(domain::OperationState::Failed, failure->code);
    } else {
        proxyDesired_ = desired;
        finishOperation(domain::OperationState::Succeeded);
        publish();
    }
    return id;
}
QUuid DesktopRuntime::beginOperation(const QString& kind) {
    operation_ = std::make_unique<domain::Operation>(kind, std::chrono::steady_clock::now() +
                                                               std::chrono::seconds(30));
    (void)operation_->begin();
    operationSnapshot_ = {{QStringLiteral("id"), operation_->id().toString(QUuid::WithoutBraces)},
                          {QStringLiteral("kind"), kind},
                          {QStringLiteral("state"), QStringLiteral("Running")}};
    return operation_->id();
}
void DesktopRuntime::finishOperation(domain::OperationState state, const QString& code) {
    if (!operation_)
        return;
    const auto id = operation_->id();
    std::optional<domain::Error> failure;
    if (state == domain::OperationState::Failed || state == domain::OperationState::Uncertain)
        failure = infra::storageError(code.toLatin1().constData());
    if (!operation_->finish(state, failure))
        return;
    const auto name = state == domain::OperationState::Succeeded   ? QStringLiteral("Succeeded")
                      : state == domain::OperationState::Cancelled ? QStringLiteral("Cancelled")
                                                                   : QStringLiteral("Failed");
    operationSnapshot_.insert(QStringLiteral("state"), name);
    operationSnapshot_.insert(QStringLiteral("error"), code);
    operation_.reset();
    emit operationCompleted(id, state, code);
}
QUuid DesktopRuntime::startCore(domain::Generation generation) {
    if (generation != repository_.manifest()
                          .value(QStringLiteral("selection"))
                          .toObject()
                          .value(QStringLiteral("generation"))
                          .toString()
                          .toULongLong() ||
        busy_)
        return {};
    const auto id = beginOperation(QStringLiteral("start_core"));
    start(false);
    if (!busy_)
        finishOperation(engine_.ready() ? domain::OperationState::Succeeded
                                        : domain::OperationState::Failed,
                        lastError_);
    return id;
}
QUuid DesktopRuntime::startProxy(domain::Generation generation) {
    if (generation != repository_.manifest()
                          .value(QStringLiteral("selection"))
                          .toObject()
                          .value(QStringLiteral("generation"))
                          .toString()
                          .toULongLong() ||
        busy_)
        return {};
    if (engine_.ready())
        return setSystemProxy(true);
    const auto id = beginOperation(QStringLiteral("start_proxy"));
    start(true);
    if (!busy_)
        finishOperation(domain::OperationState::Failed, lastError_);
    return id;
}
QUuid DesktopRuntime::stopProxy(domain::StopReason reason) {
    stop(reason);
    const auto id = beginOperation(QStringLiteral("stop_proxy"));
    finishOperation(stopFailure_.isEmpty() ? domain::OperationState::Succeeded
                                           : domain::OperationState::Failed,
                    stopFailure_);
    publish();
    return id;
}
void DesktopRuntime::testWebsite(const QString& target, const QString& path) {
    if (testReply_ && !testReply_->isFinished()) {
        error(QStringLiteral("WebsiteTestBusy"));
        return;
    }
    const QUrl url(target);
    if ((path == QStringLiteral("CurrentProxy") && !engine_.ready()) ||
        !QStringList{QStringLiteral("CurrentProxy"), QStringLiteral("Direct"),
                     QStringLiteral("SystemDefault")}
             .contains(path) ||
        !url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasFragment() || target.size() > 8192) {
        testQueue_.clear();
        error(QStringLiteral("ReadyProxyAndHttpsTargetRequired"));
        return;
    }
    const auto saved =
        credentials_.write(QStringLiteral("e430ed25-78b8-47ea-a7b0-d746a5783891"), target.toUtf8());
    if (const auto* failure = std::get_if<domain::Error>(&saved)) {
        testQueue_.clear();
        error(failure->code);
        return;
    }
    testTarget_ = target;
    const auto testGeneration = ++testGeneration_;
    QString resolved = path;
    QNetworkProxy proxy(QNetworkProxy::NoProxy);
    if (path == QStringLiteral("CurrentProxy"))
        proxy = QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                              preferences().mixedPort);
    else if (path == QStringLiteral("SystemDefault")) {
        const auto proxies = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(url));
        if (proxies.isEmpty()) {
            error(QStringLiteral("SystemProxyResolutionFailed"));
            testQueue_.clear();
            return;
        }
        proxy = proxies.first();
        resolved = proxy.type() == QNetworkProxy::NoProxy
                       ? QStringLiteral("NoProxy")
                       : QStringLiteral("Proxy:%1:%2").arg(proxy.hostName()).arg(proxy.port());
    }
    featureState_.insert(QStringLiteral("websiteTest"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("Running")},
                                     {QStringLiteral("path"), path},
                                     {QStringLiteral("resolved"), resolved},
                                     {QStringLiteral("epoch"), epoch_}});
    publish();
    testNetwork_.setProxy(proxy);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10000);
    auto* reply = testNetwork_.get(request);
    reply->setReadBufferSize(65537);
    auto received = std::make_shared<qint64>(0);
    connect(reply, &QIODevice::readyRead, this, [reply, received] {
        *received += reply->read(65537 - *received).size();
        if (*received > 65536)
            reply->abort();
    });
    testReply_ = reply;
    const auto generation = operationGeneration_;
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->start(12000);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, timer, received, generation, testGeneration, path, resolved, target] {
            reply->deleteLater();
            if (testReply_ != reply || generation != operationGeneration_ ||
                testGeneration != testGeneration_ ||
                (path == QStringLiteral("CurrentProxy") && !engine_.ready())) {
                if (testReply_ == reply) {
                    testQueue_.clear();
                    testReply_.clear();
                    featureState_.insert(
                        QStringLiteral("websiteTest"),
                        QJsonObject{{QStringLiteral("state"), QStringLiteral("Stale")}});
                    publish();
                }
                return;
            }
            testReply_.clear();
            *received += reply->read(65537 - *received).size();
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString outcome = QStringLiteral("NetworkError");
            if (*received > 65536)
                outcome = QStringLiteral("ResponseLimit");
            else if (status >= 300 && status < 400)
                outcome = QStringLiteral("Redirect");
            else if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300)
                outcome = QStringLiteral("Reachable");
            else if (status > 0)
                outcome = QStringLiteral("HttpError");
            else if (reply->error() == QNetworkReply::SslHandshakeFailedError)
                outcome = QStringLiteral("TlsError");
            else if (reply->error() == QNetworkReply::OperationCanceledError ||
                     reply->error() == QNetworkReply::TimeoutError)
                outcome = QStringLiteral("Timeout");
            const QJsonObject result{
                {QStringLiteral("state"), outcome},
                {QStringLiteral("status"), status},
                {QStringLiteral("elapsedMs"), timer->elapsed()},
                {QStringLiteral("bytes"), *received},
                {QStringLiteral("path"), path},
                {QStringLiteral("target"), target},
                {QStringLiteral("resolved"), resolved},
                {QStringLiteral("epoch"), epoch_},
                {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
            featureState_.insert(QStringLiteral("websiteTest"), result);
            auto history = featureState_.value(QStringLiteral("testHistory")).toArray();
            history.append(result);
            while (history.size() > 32)
                history.removeFirst();
            featureState_.insert(QStringLiteral("testHistory"), history);
            testResult_ =
                outcome == QStringLiteral("Reachable")
                    ? (path == QStringLiteral("CurrentProxy")
                           ? tr("HTTP %1 · %2 ms · Current proxy")
                           : tr("HTTP %1 · %2 ms"))
                          .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                          .arg(timer->elapsed())
                    : (path == QStringLiteral("CurrentProxy")
                           ? tr("Current proxy test failed (no direct fallback)")
                           : tr("Website test failed"));
            publish();
            if (!testQueue_.isEmpty()) {
                const auto next = testQueue_.takeFirst();
                QTimer::singleShot(0, this, [this, next, path, testGeneration] {
                    if (testGeneration == testGeneration_)
                        testWebsite(next, path);
                });
            }
        });
}
} // namespace quickclash::application
