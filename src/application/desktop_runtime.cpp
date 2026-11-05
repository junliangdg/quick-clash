#include "application/desktop_runtime.h"
#include "domain/subscription_usage.h"
#include "infra/config/config_overlay.h"
#include "infra/config/custom_groups.h"
#include "infra/config/geo_resources.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/runtime_provider_cache.h"
#include "infra/storage/storage_maintenance.h"
#include "platform/core_artifacts.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
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
#include <algorithm>
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
DesktopRuntime::DesktopRuntime(infra::JsonStore& store, DesktopServices services, QObject* parent,
                               std::function<QDateTime()> clock, const QUrl& geoSourceBase)
    : QObject(parent), store_(store), repository_(store),
      credentials_(std::move(services.credentials)),
      platformInfo_(std::move(services.platformInfo)), resources_(std::move(services.resources)),
      proxyLease_(std::move(services.proxyLease)), serviceSetup_(std::move(services.serviceSetup)),
      validator_(std::move(services.validator)),
      clock_(clock ? std::move(clock) : [] { return QDateTime::currentDateTimeUtc(); }),
      runtimePreferences_(infra::SettingsRepository::runtimeDefaults()) {
    bundledCoreExecutable_ = std::move(services.bundledCoreExecutable);
    coreUpdateSource_ = std::move(services.coreUpdateSource);
    geoSourceBase_ =
        geoSourceBase.isEmpty()
            ? QUrl(QStringLiteral(
                  "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/"))
            : geoSourceBase;
    directNetwork_.setProxy(QNetworkProxy::NoProxy);
    resourceNetwork_.setProxy(QNetworkProxy::NoProxy);
    geoTimer_.setSingleShot(true);
    connect(&geoTimer_, &QTimer::timeout, this, [this] {
        if (shuttingDown_ || store_.readOnly())
            return;
        if (busy_ || selectedId_.isEmpty() || !geoPending_.isEmpty()) {
            geoTimer_.start(busy_ ? 5000 : 60000);
            return;
        }
        geoScheduled_ = true;
        updateGeo();
    });
    publicationTimer_.setSingleShot(true);
    publicationTimer_.setInterval(0);
    connect(&publicationTimer_, &QTimer::timeout, this, &DesktopRuntime::flushData);
    logPublicationTimer_.setSingleShot(true);
    logPublicationTimer_.setInterval(100);
    connect(&logPublicationTimer_, &QTimer::timeout, this, &DesktopRuntime::publish);
    featureTimer_.setInterval(1000);
    connect(&featureTimer_, &QTimer::timeout, this, &DesktopRuntime::pollFeatures);
    connect(&engine_, &infra::EngineSession::streamData, this,
            [this](const QString& stream, const QJsonObject& data) {
                if (!featureDemands_.contains(stream) ||
                    (stream == QStringLiteral("logs") &&
                     !featureDemands_.contains(QStringLiteral("log_feed"))))
                    return;
                if (stream == QStringLiteral("logs")) {
                    if (data.value(QStringLiteral("source")) == QStringLiteral("Service"))
                        logs_.addDropped(
                            std::max(0, data.value(QStringLiteral("dropped")).toInt()));
                    appendLog(data.value(QStringLiteral("source")) == QStringLiteral("Service")
                                  ? QStringLiteral("Service")
                                  : QStringLiteral("Core"),
                              data.value(QStringLiteral("type")).toString(),
                              data.value(QStringLiteral("payload")).toString(),
                              data.value(QStringLiteral("truncated")).toBool());
                } else {
                    auto sample = data;
                    sample.insert(QStringLiteral("time"),
                                  QString::number(QDateTime::currentMSecsSinceEpoch()));
                    featureState_.insert(QStringLiteral("traffic"), sample);
                    // Do not resample the stream with the independent feature timer: phase
                    // drift can overwrite an unpublished point and create a false gap.
                    publish();
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
        usageMinute_ = -1; // A threshold may cross within the last published minute.
        publish();         // Refresh local expiry warnings without any additional download.
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
        start(backgroundManaged_ ? persistedProxyIntent_ && !proxyLease_->conflict()
                                 : proxyDesired_);
    });
    reconcileTimer_.setSingleShot(true);
    reconcileTimer_.setInterval(1500);
    delayReconcileTimer_.setSingleShot(true);
    delayReconcileTimer_.setInterval(1500);
    connect(&delayReconcileTimer_, &QTimer::timeout, this,
            &DesktopRuntime::resumeDelayTestsAfterReconcile);
    connect(&reconcileTimer_, &QTimer::timeout, this, [this] {
        if (shuttingDown_)
            return;
        ++reconcileCount_;
        if (proxyLease_->active()) {
            const auto result = proxyLease_->matches();
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
        auto completion = std::move(validationCompletion_);
        const auto stopped = stopValidation();
        if (const auto* failure = std::get_if<domain::Error>(&stopped))
            error(failure->code);
        if (completion) {
            completion(false);
        }
    });
    connect(validator_.get(), &IDesktopProcess::finished, this, [this](int code) {
        validationDeadline_.stop();
        if (validationCompletion_) {
            auto completion = std::move(validationCompletion_);
            staging_.reset();
            completion(code == 0);
        }
    });
    connect(&engine_, &infra::EngineSession::becameReady, this, [this](const QString& version) {
        runningCoreVersion_ = version;
        coreExitDetail_.clear();
        tunPending_ = false;
        tunDisabling_ = false;
        if (lastError_.startsWith(QStringLiteral("Service")))
            lastError_.clear();
        listenerRecoveryBlocked_ = false;
        listenerIssue_ = {};
        effectivePreferences_ = sessionPreferences_;
        coreUptime_.start();
        status_ = QStringLiteral("RestoringSelections");
        publish();
    });
    connect(&engine_, &infra::EngineSession::failed, this, [this](const QString& code) {
        recordCoreExit();
        tunPending_ = false;
        tunDisabling_ = false;
        if (code.startsWith(QStringLiteral("CoreListener"))) {
            listenerIssue_ = engine_.listenerIssue();
            listenerRecoveryBlocked_ = true;
            recoveryTimer_.stop();
        }
        if (applying_) {
            failedApply(code);
        } else {
            if (infra::classifyRecoveryFailure(code) == infra::RecoveryFailure::CleanupUncertain) {
                status_ = QStringLiteral("Uncertain");
                backgroundSuppressed_ = true;
                recoveryTimer_.stop();
            }
            error(code);
        }
    });
    connect(&engine_, &infra::EngineSession::groupsRefreshFailed, this,
            [this](const QString& code) {
                // A group read is observational. Keep the last confirmed membership visible
                // while marking it stale instead of turning a transient API failure into a
                // core error.
                featureState_.insert(QStringLiteral("groupsStale"), true);
                featureState_.insert(QStringLiteral("groupsError"), code);
                publish();
            });
    connect(&engine_, &infra::EngineSession::tunStateChanged, this, [this](const QString& state) {
        if (state == QStringLiteral("Missing") || state == QStringLiteral("Unknown")) {
            appendLog(QStringLiteral("App"), QStringLiteral("warning"),
                      state == QStringLiteral("Missing")
                          ? QStringLiteral("The TUN adapter is no longer observable. The "
                                           "core stays running; turn TUN off and on to "
                                           "recreate it.")
                          : QStringLiteral("TUN network state could not be inspected. The "
                                           "core stays running; retry or inspect the "
                                           "Windows network state."));
            error(QStringLiteral("TunNetworkUnavailable"));
        } else if (state == QStringLiteral("On") &&
                   lastError_ == QStringLiteral("TunNetworkUnavailable")) {
            lastError_.clear();
        }
        publish();
    });
    connect(&engine_, &infra::EngineSession::stopped, this, [this](bool unexpected) {
        effectivePreferences_ = {};
        effectiveProfileId_.clear();
        sessionProfileId_.clear();
        invalidateWebsiteTest();
        for (const auto& stream :
             {QStringLiteral("connections"), QStringLiteral("rules"), QStringLiteral("providers"),
              QStringLiteral("traffic"), QStringLiteral("resources")})
            releaseFeature(stream);
        cancelDelayTests();
        resetConnectionClose();
        queuedSelections_.clear();
        connections_ = {};
        featureState_.remove(QStringLiteral("traffic"));
        if (!applying_)
            status_ = QStringLiteral("Stopped");
        if (unexpected) {
            tunPending_ = false;
            tunDisabling_ = false;
            if (applying_) {
                failedApply(QStringLiteral("CoreExitedDuringApply"));
                return;
            }
            cleanupSession();
            loadOfflineGroups();
            busy_ = false;
            pendingGroup_.clear();
            pendingMember_.clear();
            const auto released = proxyLease_->release();
            if (const auto* failure = std::get_if<domain::Error>(&released))
                error(failure->code);
            recordCoreExit();
            error(QStringLiteral("CoreExited"));
            if (backgroundManaged_) {
                const bool ranLongEnough = coreUptime_.isValid() && coreUptime_.elapsed() >= 60000;
                if (ranLongEnough)
                    recoveryFailures_ = 0;
                const bool tunSession =
                    runtimePreferences_.value(QStringLiteral("tunDesired")).toBool() &&
                    !tunRecoveryBlocked_;
                scheduleBackgroundStart(true, !ranLongEnough && tunSession);
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
    connect(&engine_, &infra::EngineSession::providerNodeNamesChanged, this,
            [this](const QJsonObject& providers) {
                if (!featureDemands_.contains(QStringLiteral("custom_groups")) || busy_ ||
                    customGroupLiveProviders_ == providers)
                    return;
                customGroupLiveProviders_ = providers;
                customGroupState_ = {};
                customGroupDraft_ = {};
                refreshCustomGroupContext();
            });
    connect(
        &engine_, &infra::EngineSession::groupsChanged, this, [this](const QJsonObject& objects) {
            featureState_.insert(QStringLiteral("groupsStale"), false);
            featureState_.remove(QStringLiteral("groupsError"));
            QJsonObject previous;
            for (const auto& entry : groups_) {
                const auto group = entry.toObject();
                previous.insert(group.value(QStringLiteral("name")).toString(),
                                group.value(QStringLiteral("current")));
            }
            groups_ = infra::orderedProxyGroups(configuredGroups_, objects);
            groupDelayTargets_.clear();
            for (const auto& value : groups_) {
                const auto name = value.toObject().value("name").toString();
                const auto target = objects.value(name).toObject().value("testUrl").toString();
                if (!target.isEmpty() && target.size() <= 8192)
                    groupDelayTargets_.insert(name, target);
            }
            QJsonArray liveNodes;
            for (auto it = objects.begin(); it != objects.end(); ++it) {
                const auto node = it.value().toObject();
                auto type = node.value(QStringLiteral("type")).toString().toLower();
                if (type == QStringLiteral("shadowsocks"))
                    type = QStringLiteral("ss");
                if (type == QStringLiteral("http") || type == QStringLiteral("socks5") ||
                    type == QStringLiteral("ss") || type == QStringLiteral("hysteria2") ||
                    type == QStringLiteral("trojan") || type == QStringLiteral("vless") ||
                    type == QStringLiteral("vmess") || type == QStringLiteral("tuic"))
                    liveNodes.append(QJsonObject{{QStringLiteral("name"), it.key()},
                                                 {QStringLiteral("type"), type},
                                                 {QStringLiteral("udp"), node.value("udp")}});
            }
            featureState_.insert(QStringLiteral("nodes"), liveNodes);
            const auto now = clock_();
            const bool latencyChanged = proxyLatency_.ingest(objects, now);
            // Most periodic core polls repeat identical histories. Reuse the projection
            // unless its data changed, an observation expired, or the clock moved back.
            if (latencyChanged || (delayExpiry_.isValid() && now >= delayExpiry_) ||
                (delayViewsAt_.isValid() && now < delayViewsAt_))
                syncDelayViews();
            // API descriptors omit endpoint credentials. Permit explicit live selection,
            // but never persist/restore a dynamic node using its display name as identity.
            QSet<QString> liveNames;
            for (auto it = objects.begin(); it != objects.end(); ++it)
                liveNames.insert(it.key());
            for (const auto& value : groups_)
                for (const auto& member : value.toObject().value("members").toArray())
                    liveNames.insert(member.toString());
            for (const auto& name : liveNames)
                if (!identities_.contains(name) ||
                    objects.value(name).toObject().value("providerDynamic").toBool())
                    identities_.insert(name, QStringLiteral("unverified:"));
            for (auto it = identities_.begin(); it != identities_.end();)
                if (it.value() == QStringLiteral("unverified:") && !liveNames.contains(it.key()))
                    it = identities_.erase(it);
                else
                    ++it;
            for (const auto& entry : groups_) {
                const auto group = entry.toObject();
                const auto name = group.value(QStringLiteral("name")).toString();
                if (previous.contains(name) &&
                    previous.value(name) != group.value(QStringLiteral("current"))) {
                    invalidateWebsiteTest();
                    break;
                }
            }
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
                        identities_.value(member) != QStringLiteral("unverified:") &&
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
                    if (identities_.value(pendingMember_) == QStringLiteral("unverified:")) {
                        selections_.remove(pendingGroup_);
                    } else {
                        selections_.insert(pendingGroup_,
                                           QJsonObject{{QStringLiteral("member"), pendingMember_},
                                                       {QStringLiteral("profileId"), selectedId_},
                                                       {QStringLiteral("identity"),
                                                        identities_.value(pendingMember_)},
                                                       {QStringLiteral("groupIdentity"),
                                                        identities_.value(pendingGroup_)}});
                    }
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
                if (!queuedSelections_.isEmpty()) {
                    const auto next = queuedSelections_.firstKey();
                    const auto member = queuedSelections_.take(next);
                    selectProxy(next, member);
                }
            });
}
DesktopRuntime::~DesktopRuntime() {
    cancelIpQueries(false);
    stop(domain::StopReason::AppExit);
    if (staging_ && validator_->running())
        staging_->setAutoRemove(false);
    if (coreStage_ && coreArchive_.running())
        coreStage_->setAutoRemove(false);
}
void DesktopRuntime::initialize() {
    refreshStartup();
    if (const auto stored = store_.read(QStringLiteral("config/geo_update.json"));
        std::holds_alternative<std::optional<QJsonObject>>(stored))
        geoSettings_ = std::get<std::optional<QJsonObject>>(stored).value_or(QJsonObject{});
    if (const auto stored = store_.read(QStringLiteral("config/geo_pending.json"));
        std::holds_alternative<std::optional<QJsonObject>>(stored))
        geoPending_ = std::get<std::optional<QJsonObject>>(stored)
                          .value_or(QJsonObject{})
                          .value(QStringLiteral("snapshot"))
                          .toObject();
    const auto released = proxyLease_->release();
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
    runtimePreferences_.remove(QStringLiteral("schemaVersion"));
    const auto mode = runtimePreferences_.value(QStringLiteral("mode")).toString();
    const auto mixed = runtimePreferences_.value(QStringLiteral("mixedPort")).toInt();
    const auto controller = runtimePreferences_.value(QStringLiteral("controllerPort")).toInt();
    const auto dnsMode =
        runtimePreferences_.value(QStringLiteral("dnsMode")).toString(QStringLiteral("profile"));
    if ((mode != QStringLiteral("rule") && mode != QStringLiteral("global") &&
         mode != QStringLiteral("direct")) ||
        mixed < 1024 || mixed > 65535 || controller < 1024 || controller > 65535 ||
        mixed == controller ||
        !QStringList{QStringLiteral("profile"), QStringLiteral("fake-ip"),
                     QStringLiteral("redir-host")}
             .contains(dnsMode)) {
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
    savedPreferences_ = runtimePreferences_;
    const auto selections = store_.read(QStringLiteral("data/node_selections.json"));
    if (const auto* value = std::get_if<std::optional<QJsonObject>>(&selections); value && *value)
        selections_ = (*value)->value(QStringLiteral("groups")).toObject();
    loadOfflineGroups();
    const auto target = credentials_->read(QStringLiteral("e430ed25-78b8-47ea-a7b0-d746a5783891"));
    if (const auto* value = std::get_if<QByteArray>(&target))
        testTarget_ = QString::fromUtf8(*value);
    const auto savedDelayTarget =
        credentials_->read(QStringLiteral("6c7a5e7f-56c4-4f6e-a2c1-15c0a2f2ef31"));
    if (const auto* value = std::get_if<QByteArray>(&savedDelayTarget))
        delayTarget_ = QString::fromUtf8(*value);
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
    scheduleGeo();
    backgroundSuppressed_ = false;
    scheduleBackgroundStart();
}
void DesktopRuntime::scheduleBackgroundStart(bool afterFailure, bool shortFailure) {
    if (listenerRecoveryBlocked_)
        return;
    if (!backgroundManaged_ || backgroundSuppressed_ || shuttingDown_ || store_.readOnly() ||
        repository_.recoveryRequired() || selectedId_.isEmpty() || engine_.ready())
        return;
    if (afterFailure) {
        const bool tunSession = runtimePreferences_.value(QStringLiteral("tunDesired")).toBool() &&
                                !tunRecoveryBlocked_;
        if (tunRecoveryBudget_.failed(tunSession, shortFailure)) {
            tunRecoveryBudget_.reset();
            tunRecoveryBlocked_ = true;
            appendLog(QStringLiteral("App"), QStringLiteral("error"),
                      QStringLiteral("TUN failed repeatedly in a short time. Automatic TUN "
                                     "restart is paused; restarting in proxy-only mode."));
            error(QStringLiteral("TunRecoveryExhausted"));
            if (!recoveryTimer_.isActive())
                recoveryTimer_.start(0);
            return;
        }
        recoveryFailures_ = std::min(recoveryFailures_ + 1, 6);
        status_ = QStringLiteral("RecoveryBackoff");
        recoveryTimer_.start(std::min(30000, 1000 << (recoveryFailures_ - 1)));
    } else if (!recoveryTimer_.isActive()) {
        recoveryTimer_.start(0);
    }
}
domain::Generation DesktopRuntime::selectionGeneration() const {
    return repository_.manifest()
        .value(QStringLiteral("selection"))
        .toObject()
        .value(QStringLiteral("generation"))
        .toString()
        .toULongLong();
}
QJsonObject DesktopRuntime::controlState() const {
    auto runtimePreferences = runtimePreferences_;
    runtimePreferences.remove(QStringLiteral("userRules"));
    auto savedPreferences = savedPreferences_;
    savedPreferences.remove(QStringLiteral("userRules"));
    runtimePreferences.insert(QStringLiteral("systemProxyDesired"), persistedProxyIntent_);
    auto effective = engine_.ready() ? effectivePreferences_ : QJsonObject{};
    effective.remove(QStringLiteral("userRules"));
    effective.insert(QStringLiteral("systemProxy"), proxyLease_->active());
    effective.insert(QStringLiteral("tun"), engine_.tunActive());
    auto features = featureState_;
    features.insert(QStringLiteral("delayTimes"), delayTimes_);
    for (const auto* key :
         {"nodes", "traffic", "memory", "memorySample", "memoryComplete", "connectionSample",
          "connectionsStale", "connectionsPaused", "connectionError"})
        features.remove(QLatin1String(key));
    return {
        {QStringLiteral("features"), features},
        {QStringLiteral("startup"), startupState_},
        {QStringLiteral("coreUpdate"), coreUpdateState_},
        {QStringLiteral("geoUpdate"),
         QJsonObject{{QStringLiteral("settings"), geoSettings_},
                     {QStringLiteral("pending"), !geoPending_.isEmpty()},
                     {QStringLiteral("snapshot"),
                      runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject()}}},
        {QStringLiteral("publicationGeneration"), QString::number(publicationGeneration_)},
        {QStringLiteral("epoch"), epoch_},
        {QStringLiteral("platform"), QSysInfo::prettyProductName()},
        {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("capabilities"), platformInfo_.capabilities},
        {QStringLiteral("outbound"), outbound_},
        {QStringLiteral("selectedId"), selectedId_},
        {QStringLiteral("applyingId"), applying_ ? applyingId_ : QString{}},
        {QStringLiteral("selectionGeneration"), repository_.manifest()
                                                    .value(QStringLiteral("selection"))
                                                    .toObject()
                                                    .value(QStringLiteral("generation"))},
        {QStringLiteral("selectedName"), selectedName_},
        {QStringLiteral("status"), status_},
        {QStringLiteral("busy"), busy_},
        {QStringLiteral("selectingProxy"), !pendingGroup_.isEmpty() && !restoringNodes_},
        {QStringLiteral("ready"), engine_.ready()},
        {QStringLiteral("backgroundManaged"), backgroundManaged_},
        {QStringLiteral("recovering"), recoveryTimer_.isActive()},
        {QStringLiteral("reconciliation"),
         QJsonObject{{QStringLiteral("requests"), QString::number(reconcileRequests_)},
                     {QStringLiteral("completed"), QString::number(reconcileCount_)}}},
        {QStringLiteral("corePid"), static_cast<int>(engine_.pid())},
        {QStringLiteral("coreExecutionMode"), !engine_.running()       ? QString{}
                                              : engine_.usingService() ? QStringLiteral("Service")
                                                                       : QStringLiteral("Local")},
        {QStringLiteral("tunState"),
         engine_.cleanupUncertain()                  ? QStringLiteral("Uncertain")
         : engine_.ready() && engine_.usingService() ? engine_.tunState()
         : runtimePreferences_.value(QStringLiteral("tunDesired")).toBool()
             ? (tunRecoveryBlocked_ || engine_.ready() ? QStringLiteral("Blocked")
                                                       : QStringLiteral("AuthorizationRequired"))
             : QStringLiteral("Off")},
        {QStringLiteral("readOnly"), store_.readOnly()},
        {QStringLiteral("tunPending"), tunPending_},
        {QStringLiteral("tunDisabling"), tunDisabling_},
        {QStringLiteral("systemProxy"), proxyLease_->active()},
        {QStringLiteral("proxyConflict"), proxyLease_->conflict()},
        {QStringLiteral("error"), lastError_},
        {QStringLiteral("listenerIssue"), listenerIssue_},
        {QStringLiteral("tunDevice"), engine_.tunDevice()},
        {QStringLiteral("listenerRecoveryBlocked"), listenerRecoveryBlocked_},
        {QStringLiteral("configIssues"), configIssues_},
        {QStringLiteral("errors"), errors_},
        {QStringLiteral("testResult"), testResult_},
        {QStringLiteral("testTarget"), testTarget_},
        {QStringLiteral("delayTarget"), delayTarget_},
        {QStringLiteral("operation"), operationSnapshot_},
        {QStringLiteral("effectivePreview"), effectivePreview_},
        {QStringLiteral("preferences"), runtimePreferences},
        {QStringLiteral("settingsState"),
         QJsonObject{{QStringLiteral("requested"), runtimePreferences},
                     {QStringLiteral("saved"), savedPreferences},
                     {QStringLiteral("effective"), effective},
                     {QStringLiteral("running"), engine_.ready()},
                     {QStringLiteral("pending"), runtimePreferences_ != savedPreferences_},
                     {QStringLiteral("phase"), status_}}},
        {QStringLiteral("coreName"), QStringLiteral("mihomo")},
        {QStringLiteral("coreVersion"), engine_.ready() ? runningCoreVersion_ : QString{}},
        {QStringLiteral("dataDirectory"), store_.root()},
        {QStringLiteral("dataUrl"), QUrl::fromLocalFile(store_.root()).toString()}};
}
QJsonObject DesktopRuntime::snapshot() const {
    auto result = controlState();
    result.insert(QStringLiteral("selectedName"),
                  profile(selectedId_).value(QStringLiteral("name")));
    result.insert(
        QStringLiteral("outbound"),
        infra::outboundSummary(groups_, defaultPolicy_,
                               runtimePreferences_.value(QStringLiteral("mode")).toString(),
                               engine_.ready() && status_ == QStringLiteral("Ready")));
    result.insert(QStringLiteral("profiles"), profileViews());
    result.insert(QStringLiteral("customGroups"), customGroupState_);
    result.insert(QStringLiteral("features"), featureState_);
    result.insert(QStringLiteral("connections"), connections_);
    result.insert(QStringLiteral("rules"), rules_);
    result.insert(QStringLiteral("providers"), providers_);
    result.insert(QStringLiteral("rulePage"), rulePageState_);
    result.insert(QStringLiteral("providerPage"), providerPageState_);
    result.insert(QStringLiteral("logs"), logs_.entries());
    QJsonObject revisions;
    for (auto it = demandState_.generations.constBegin(); it != demandState_.generations.constEnd();
         ++it)
        revisions.insert(it.key(), QString::number(it.value()));
    result.insert(QStringLiteral("dataLifecycle"),
                  QJsonObject{{QStringLiteral("active"),
                               QJsonArray::fromStringList(featureDemands_.values())},
                              {QStringLiteral("generations"), revisions},
                              {QStringLiteral("publishedChannels"),
                               QJsonArray::fromStringList(publishedData_.keys())},
                              {QStringLiteral("pendingReads"),
                               QJsonArray::fromStringList(pendingFeatureRequests_.values())},
                              {QStringLiteral("providerDraftCount"), providerDraft_.size()},
                              {QStringLiteral("providerResultCount"), providerResults_.size()},
                              {QStringLiteral("appLogCount"), appLogs_.entries().size()}});
    // Sparse diagnostic counters only; never included in production control/data publications.
    result.insert(
        QStringLiteral("configLifecycle"),
        QJsonObject{
            {QStringLiteral("providerFiles"),
             providerImport_ ? providerImport_->stage.fileCount() : 0},
            {QStringLiteral("providerBytes"),
             providerImport_ ? providerImport_->stage.totalBytes() : 0},
            {QStringLiteral("semanticBytes"),
             configTransaction_ ? configTransaction_->semantic.size() : 0},
            {QStringLiteral("validated"), configTransaction_ && configTransaction_->validated},
            {QStringLiteral("validationActive"), static_cast<bool>(validationCompletion_)}});
    result.insert(QStringLiteral("droppedLogs"),
                  logs_.status().value(QStringLiteral("droppedLogs")));
    result.insert(QStringLiteral("delays"), delays_);
    result.insert(QStringLiteral("delay_details"), delayDetails_);
    result.insert(QStringLiteral("groups"), groups_);
    result.insert(QStringLiteral("coreUptimeSeconds"),
                  engine_.ready() && coreUptime_.isValid() ? coreUptime_.elapsed() / 1000 : 0);
    return result;
}
void DesktopRuntime::publish() {
    finishCustomGroupOperation();
    finishUserRuleOperation();
    if (coreOperationActive_ && !busy_) {
        coreOperationActive_ = false;
        coreUpdateState_.insert(QStringLiteral("state"), lastError_.isEmpty()
                                                             ? QStringLiteral("Applied")
                                                             : QStringLiteral("Failed"));
    }
    finishSnapshotProviderRefresh();
    if (geoOperationActive_ && !busy_) {
        geoOperationActive_ = false;
        geoScheduled_ = false;
        if (lastError_.isEmpty() && !geoPending_.isEmpty() &&
            runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject() == geoPending_) {
            const auto cleared = store_.write(QStringLiteral("config/geo_pending.json"), {});
            if (std::holds_alternative<std::monostate>(cleared))
                geoPending_ = {};
            else
                lastError_ = std::get<domain::Error>(cleared).code;
        }
        featureState_.insert(QStringLiteral("geoResult"), lastError_.isEmpty()
                                                              ? QStringLiteral("Applied")
                                                              : QStringLiteral("Failed"));
    }
    if (!busy_ && !updatingSubscription_.isEmpty())
        completeSubscriptionUpdate(lastError_.isEmpty() ? QStringLiteral("Updated")
                                                        : QStringLiteral("Failed"));
    if (publicationGeneration_ == 0 || publicationEpoch_ != epoch_) {
        ++publicationGeneration_;
        publicationEpoch_ = epoch_;
        publishedData_ = {};
    }
    const auto mode = runtimePreferences_.value(QStringLiteral("mode")).toString();
    const bool ready = engine_.ready() && status_ == QStringLiteral("Ready");
    if (summaryGroups_ != groups_ || summaryMode_ != mode || summaryPolicy_ != defaultPolicy_ ||
        summaryReady_ != ready || outbound_.isEmpty()) {
        summaryGroups_ = groups_;
        summaryMode_ = mode;
        summaryPolicy_ = defaultPolicy_;
        summaryReady_ = ready;
        outbound_ = infra::outboundSummary(groups_, defaultPolicy_, mode, ready);
    }
    if (summaryProfiles_ != profiles_ || summarySelectedId_ != selectedId_ ||
        usageMinute_ != clock_().toSecsSinceEpoch() / 60) {
        summaryProfiles_ = profiles_;
        profileViews_ = profileViews();
        usageMinute_ = clock_().toSecsSinceEpoch() / 60;
        summarySelectedId_ = selectedId_;
        selectedName_ = profile(selectedId_).value(QStringLiteral("name"));
    }
    const auto control = controlState();
    if (publishedControl_ != control) {
        publishedControl_ = control;
        emit changed(control);
    }
    if (!publicationTimer_.isActive())
        publicationTimer_.start();
}
void DesktopRuntime::flushData() {
    refreshCustomGroupContext();
    refreshUserRuleContext();
    QJsonObject samples;
    samples.insert(QStringLiteral("coreUptimeSeconds"),
                   engine_.ready() && coreUptime_.isValid() ? coreUptime_.elapsed() / 1000 : 0);
    const auto fields = [this](const QStringList& keys) {
        QJsonObject value;
        for (const auto& key : keys)
            if (featureState_.contains(key))
                value.insert(key, featureState_.value(key));
        return value;
    };
    const QJsonObject channels{
        {QStringLiteral("samples"), samples},
        {QStringLiteral("profiles"), profileViews_},
        {QStringLiteral("groups"), groups_},
        {QStringLiteral("nodes"), featureState_.value(QStringLiteral("nodes")).toArray()},
        {QStringLiteral("delays"), delays_},
        {QStringLiteral("delay_details"), delayDetails_},
        {QStringLiteral("connections"), connections_},
        {QStringLiteral("rules"), rules_},
        {QStringLiteral("providers"), providers_},
        {QStringLiteral("rule_status"), QJsonObject{{QStringLiteral("rulePage"), rulePageState_}}},
        {QStringLiteral("provider_status"),
         QJsonObject{{QStringLiteral("providerPage"), providerPageState_}}},
        {QStringLiteral("logs"), logs_.entries()},
        {QStringLiteral("editor"), editor_},
        {QStringLiteral("custom_groups"), customGroupState_},
        {QStringLiteral("user_rules"), userRuleState_},
        {QStringLiteral("traffic"), fields({QStringLiteral("traffic")})},
        {QStringLiteral("resources"),
         fields({QStringLiteral("memory"), QStringLiteral("memorySample"),
                 QStringLiteral("memoryComplete")})},
        {QStringLiteral("connection_status"),
         fields({QStringLiteral("connectionSample"), QStringLiteral("connectionsStale"),
                 QStringLiteral("connectionsPaused"), QStringLiteral("connectionError")})},
        {QStringLiteral("log_status"), logs_.status()}};
    RuntimeUpdate update{publicationGeneration_, epoch_, {}};
    update.demandGenerations = demandState_.generations;
    update.invalidated = std::move(invalidatedChannels_);
    invalidatedChannels_.clear();
    for (auto it = channels.constBegin(); it != channels.constEnd(); ++it) {
        const auto stream = channelStream(it.key());
        if (!stream.isEmpty() && !featureDemands_.contains(stream))
            continue;
        // Unchanged implicitly shared JSON collections are compared without QVariant expansion.
        if (!publishedData_.contains(it.key()) || publishedData_.value(it.key()) != it.value()) {
            publishedData_.insert(it.key(), it.value());
            update.values.insert(it.key(), it.value());
        }
    }
    if (!update.values.isEmpty() || !update.invalidated.isEmpty())
        emit dataPublished(update);
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
        const auto usage = domain::subscriptionUsageStatus(
            item.value(QStringLiteral("usage")).toObject(), clock_());
        const auto expiry =
            QDateTime::fromString(usage.value(QStringLiteral("expiresAt")).toString(), Qt::ISODate);
        if (expiry.isValid() && expiry > clock_()) {
            scheduled = true;
            const auto soon = expiry.addDays(-7);
            delay = std::min(delay, clock_().msecsTo(soon > clock_() ? soon : expiry));
        }
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
    if (!busy_) {
        tunPending_ = false;
        tunDisabling_ = false;
    }
    appendLog(QStringLiteral("App"), QStringLiteral("error"), code);
    lastError_ = code;
    configIssues_ = {};
    QJsonObject entry{
        {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("code"), code}};
    // Keep the service-reported exit cause beside the code so a field report stays actionable.
    if ((code.startsWith(QStringLiteral("CoreExited")) ||
         code == QStringLiteral("CoreReadyTimeout")) &&
        !coreExitDetail_.isEmpty())
        entry.insert(QStringLiteral("detail"), coreExitDetail_.left(2048));
    errors_.append(entry);
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
                                         {QStringLiteral("phase"), issue.phase},
                                         {QStringLiteral("occurrences"), issue.occurrences}});
    }
    publish();
}
QString DesktopRuntime::coreExecutable() const {
    return platform::managedCorePath(
        bundledCoreExecutable(),
        runtimePreferences_.value(QStringLiteral("coreArtifactId")).toString());
}
QString DesktopRuntime::bundledCoreExecutable() const {
    return bundledCoreExecutable_.isEmpty()
               ? platform::bundledCorePath(QCoreApplication::applicationDirPath())
               : bundledCoreExecutable_;
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
QJsonArray DesktopRuntime::profileViews() const {
    QJsonArray views;
    const auto now = clock_();
    for (const auto& entry : profiles_) {
        auto view = entry.toObject();
        view.insert(QStringLiteral("revision"), profileRevision(view));
        view.remove(QStringLiteral("overlay"));
        view.remove(QStringLiteral("providerResources"));
        view.remove(QStringLiteral("restoredUserRules"));
        auto history = view.value(QStringLiteral("history")).toArray();
        for (qsizetype i = 0; i < history.size(); ++i) {
            auto version = history[i].toObject();
            version.remove(QStringLiteral("overlay"));
            version.remove(QStringLiteral("providerResources"));
            version.remove(QStringLiteral("restoredUserRules"));
            history[i] = version;
        }
        view.insert(QStringLiteral("history"), history);
        view.insert(
            QStringLiteral("usageStatus"),
            domain::subscriptionUsageStatus(view.value(QStringLiteral("usage")).toObject(), now));
        views.append(view);
    }
    return views;
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
    const auto previousProviders = profile(selectedId_).value(QStringLiteral("managedProviders"));
    const auto result =
        store_.write(QStringLiteral("data/catalog.json"), {{QStringLiteral("profiles"), profiles}});
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
        return false;
    }
    profiles_ = profiles;
    if (previousProviders != profile(selectedId_).value(QStringLiteral("managedProviders"))) {
        // A saved candidate can change while auto-apply is off and the old core keeps running.
        ++featureRequests_[QStringLiteral("providers")];
        pendingFeatureRequests_.remove(QStringLiteral("providers"));
        loadedFeatures_.remove(QStringLiteral("providers"));
        providerDraft_ = {};
        providerPageState_ = {};
    }
    scheduleSubscriptions();
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
    auto loaded = infra::readFileBounded(file, kSourceLimit);
    if (!loaded.ok) {
        error(QStringLiteral("ProfileReadFailed"));
        return;
    }
    resolveImport(std::move(loaded.bytes),
                  name.isEmpty() ? QFileInfo(file).completeBaseName() : name, {});
}
void DesktopRuntime::addSubscription(const QString& url, const QString& name,
                                     const QString& route) {
    download(url, name, {}, {}, route);
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
    const auto secret = credentials_->read(item.value(QStringLiteral("secretRef")).toString());
    if (const auto* failure = std::get_if<domain::Error>(&secret)) {
        error(failure->code);
        return;
    }
    const auto bytes = std::get<QByteArray>(secret);
    const auto envelope = QJsonDocument::fromJson(bytes).object();
    download(envelope.isEmpty() ? QString::fromUtf8(bytes)
                                : envelope.value(QStringLiteral("url")).toString(),
             item.value(QStringLiteral("name")).toString(), id,
             envelope.value(QStringLiteral("headers")).toObject(),
             item.value(QStringLiteral("downloadRoute")).toString(QStringLiteral("Direct")),
             envelope.value(QStringLiteral("cache")).toObject());
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
void DesktopRuntime::validate(const QByteArray& semantic, std::function<void(bool)> completion) {
    lastError_.clear();
    if (validator_->running()) {
        error(QStringLiteral("CoreStopFailed"));
        completion(false);
        return;
    }
    if (platformInfo_.capabilities.value(QStringLiteral("core")) != QStringLiteral("Supported")) {
        error(QStringLiteral("CorePlatformUnsupported"));
        completion(false);
        return;
    }
    if (!platform::verifyCoreArtifact(
            coreExecutable(),
            runtimePreferences_.value(QStringLiteral("coreArtifactId")).toString())) {
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
    QFile config(staging_->filePath(QStringLiteral("config.yaml")));
    const auto copied = staging_->filePath(platformInfo_.coreFileName);
    {
        const auto materialized = infra::materializeSession({semantic, {}}, newSecret());
        if (!std::holds_alternative<QByteArray>(materialized) ||
            !config.open(QIODevice::WriteOnly) ||
            config.write(std::get<QByteArray>(materialized)) !=
                std::get<QByteArray>(materialized).size() ||
            !QFile::copy(coreExecutable(), copied)) {
            config.close();
            staging_.reset();
            completion(false);
            return;
        }
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
        validator_->start(copied,
                          {QStringLiteral("-t"), QStringLiteral("-f"), config.fileName(),
                           QStringLiteral("-d"), staging_->path()},
                          staging_->path(), true);
    if (const auto* failure = std::get_if<domain::Error>(&launched)) {
        auto callback = std::move(validationCompletion_);
        if (!validator_->running())
            staging_.reset();
        error(failure->code);
        callback(false);
        return;
    }
    validationDeadline_.start();
}
DesktopRuntime::ConfigCandidates::~ConfigCandidates() {
    for (const auto& reference : references) {
        const auto removed = store.remove(reference);
        if (std::holds_alternative<domain::Error>(removed))
            qWarning("Configuration candidate cleanup failed");
    }
}
void DesktopRuntime::importBytes(QByteArray bytes, const QString& name, const QString& secretRef,
                                 const QString& existingId, const QJsonObject& storedSource,
                                 const QJsonObject& metadata,
                                 std::shared_ptr<ConfigCandidates> candidates) {
    if (profiles_.size() >= 100 && existingId.isEmpty()) {
        error(QStringLiteral("ProfileLimitExceeded"));
        return;
    }
    auto context = std::make_shared<ConfigTransaction>();
    context->candidates =
        candidates ? std::move(candidates) : std::make_shared<ConfigCandidates>(store_);
    context->profileId =
        existingId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingId;
    context->previewOnly = metadata.value(QStringLiteral("_previewOnly")).toBool();
    context->forceApply = metadata.value(QStringLiteral("_forceApply")).toBool();
    context->editorToken = metadata.value(QStringLiteral("_editorToken")).toString();
    context->customGroupToken = metadata.value(QStringLiteral("_customGroupToken")).toString();
    context->preferences = runtimePreferences_;
    const auto sourceRef =
        storedSource.isEmpty()
            ? QStringLiteral("data/profiles/%1/%2.yaml")
                  .arg(context->profileId, QUuid::createUuid().toString(QUuid::WithoutBraces))
            : storedSource.value(QStringLiteral("sourceRef")).toString();
    auto imported = storedSource;
    if (storedSource.isEmpty()) {
        const auto saved = store_.writeBytes(sourceRef, bytes, true);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            error(failure->code);
            return;
        }
        context->candidates->references.append(sourceRef);
        imported.insert(QStringLiteral("sourceHash"), hash(bytes));
    } else if (storedSource.contains(QStringLiteral("snapshotRef"))) {
        if (hash(bytes) != storedSource.value(QStringLiteral("snapshotHash")) ||
            std::holds_alternative<domain::Error>(store_.verifyBytes(
                storedSource.value(QStringLiteral("snapshotRef")).toString(), hash(bytes)))) {
            error(QStringLiteral("SourceIntegrityFailed"));
            return;
        }
    } else {
        const auto snapshotRef = sourceRef + QStringLiteral(".snapshot.yaml");
        const auto saved = store_.writeBytes(snapshotRef, bytes, true);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            error(failure->code);
            return;
        }
        context->candidates->references.append(snapshotRef);
        imported.insert(QStringLiteral("snapshotRef"), snapshotRef);
        imported.insert(QStringLiteral("snapshotHash"), hash(bytes));
    }
    imported.insert(QStringLiteral("sourceRef"), sourceRef);
    imported.insert(QStringLiteral("id"), context->profileId);
    imported.insert(QStringLiteral("name"), name.trimmed().left(80).isEmpty()
                                                ? tr("New profile")
                                                : name.trimmed().left(80));
    imported.insert(QStringLiteral("secretRef"), secretRef);
    imported.insert(QStringLiteral("updatedAt"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    for (auto it = metadata.begin(); it != metadata.end(); ++it)
        if (!it.key().startsWith(QLatin1Char('_')))
            imported.insert(it.key(), it.value());
    if (!imported.contains(QStringLiteral("overlay")))
        imported.insert(QStringLiteral("overlay"),
                        profile(existingId).value(QStringLiteral("overlay")).toObject());
    if (!imported.contains(QStringLiteral("customGroups")))
        imported.insert(QStringLiteral("customGroups"),
                        profile(existingId).value("customGroups").toArray());
    context->imported = imported;
    auto composed = infra::composeProfileConfig(bytes, imported);
    if (const auto* failure = std::get_if<domain::Error>(&composed)) {
        configError(*failure);
        return;
    }
    auto preflight = infra::preflightConfig(std::get<QByteArray>(composed), preferences());
    bytes = {}; // Persisted raw/snapshot input does not cross the asynchronous validation boundary.
    if (const auto* failure = std::get_if<domain::Error>(&preflight)) {
        configError(*failure);
        return;
    }
    auto& checked = std::get<infra::PreflightConfig>(preflight);
    QJsonArray notices;
    for (const auto& field : checked.overriddenFields) {
        if (field == QStringLiteral("geox-url"))
            notices.append(QStringLiteral("ManagedGeo"));
        else if (field == QStringLiteral("proxies.<extension>"))
            notices.append(QStringLiteral("IgnoredProxyExtensions"));
    }
    context->imported.insert(QStringLiteral("compatibilityNotices"), notices);
    context->semantic = std::move(checked.semanticYaml);
    busy_ = true;
    context->generation = ++operationGeneration_;
    configTransaction_ = context;
    publish();
    const auto weak = std::weak_ptr<ConfigTransaction>(context);
    validate(context->semantic, [this, weak](bool valid) {
        const auto context = weak.lock();
        if (!context || context != configTransaction_ ||
            context->generation != operationGeneration_) {
            return;
        }
        if (!valid) {
            configTransaction_.reset();
            context->semantic = {};
            busy_ = false;
            tunPending_ = false;
            tunDisabling_ = false;
            error(lastError_.isEmpty() ? QStringLiteral("CoreConfigValidationFailed") : lastError_);
            return;
        }
        context->validated = true;
        QTimer::singleShot(0, this, [this, weak] {
            const auto context = weak.lock();
            if (context && context == configTransaction_ &&
                context->generation == operationGeneration_)
                finishImport(context);
        });
    });
}
void DesktopRuntime::finishImport(const std::shared_ptr<ConfigTransaction>& context) {
    if (context->previewOnly) {
        if (!context->customGroupToken.isEmpty()) {
            finishCustomGroupPreview(context);
            return;
        }
        finishEditorPreview(context);
        return;
    }
    const auto id = context->profileId;
    const auto previous = profile(id);
    QJsonObject item = previous;
    const auto& imported = context->imported;
    item.remove(QStringLiteral("snapshotRef"));
    item.remove(QStringLiteral("snapshotHash"));
    for (auto it = imported.begin(); it != imported.end(); ++it)
        item.insert(it.key(), it.value());
    if (!previous.isEmpty() &&
        (previous.value(QStringLiteral("sourceHash")) != item.value(QStringLiteral("sourceHash")) ||
         previous.value(QStringLiteral("snapshotHash")) !=
             item.value(QStringLiteral("snapshotHash")) ||
         (previous.value(QStringLiteral("overlay")) != item.value(QStringLiteral("overlay")) ||
          previous.value("customGroups") != item.value("customGroups")))) {
        auto history = previous.value(QStringLiteral("history")).toArray();
        auto version = previous;
        version.remove(QStringLiteral("history"));
        history.prepend(version);
        while (history.size() > 5)
            history.removeLast();
        item.insert(QStringLiteral("history"), history);
    }
    if (previous.isEmpty() && !imported.value(QStringLiteral("secretRef")).toString().isEmpty()) {
        item.insert(QStringLiteral("updateIntervalMinutes"), 360);
        item.insert(QStringLiteral("autoApply"), true);
        item.insert(QStringLiteral("nextUpdateAt"),
                    clock_().addSecs(360 * 60).toString(Qt::ISODate));
        item.insert(QStringLiteral("lastUpdateResult"), QStringLiteral("Updated"));
    }
    const bool changed =
        previous.value(QStringLiteral("sourceHash")) != item.value(QStringLiteral("sourceHash")) ||
        previous.value(QStringLiteral("snapshotHash")) !=
            item.value(QStringLiteral("snapshotHash")) ||
        (previous.value(QStringLiteral("overlay")) != item.value(QStringLiteral("overlay")) ||
         previous.value("customGroups") != item.value("customGroups"));
    const bool activate =
        selectedId_.isEmpty() ||
        (id == selectedId_ &&
         (context->forceApply || item.value(QStringLiteral("autoApply")).toBool()) &&
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
        // Catalog now owns the immutable source/snapshot, even if later application fails.
        context->candidates->references.clear();
        if (!context->editorToken.isEmpty() &&
            context->editorToken == editor_.value(QStringLiteral("token")) &&
            id == editor_.value(QStringLiteral("id"))) {
            editor_.insert(QStringLiteral("candidateSaved"), true);
            editor_.insert(QStringLiteral("baseRevision"), profileRevision(item));
            editor_.insert(QStringLiteral("sourceHash"), item.value(QStringLiteral("sourceHash")));
            editor_.insert(QStringLiteral("baseSourceRef"),
                           item.value(QStringLiteral("sourceRef")));
            editor_.insert(QStringLiteral("baseSnapshotRef"),
                           item.value(QStringLiteral("snapshotRef")));
            editor_.insert(QStringLiteral("baseOverlay"), item.value(QStringLiteral("overlay")));
            editor_.insert(QStringLiteral("conflict"), false);
            saveEditorDraft(context->editorToken, editor_.value(QStringLiteral("text")).toString());
        }
        collectGarbage();
        lastError_.clear();
        configIssues_ = {};
        scheduleSubscriptions();
        if (activate) {
            applyProfile(id, engine_.ready());
            return;
        }
        configTransaction_.reset();
        context->semantic = {};
        busy_ = false;
        publish();
    } else {
        configTransaction_.reset();
        context->semantic = {};
        busy_ = false;
        publish();
    }
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
    if (!store_.readOnly() && busy_ && updatingSubscription_ == id && downloadReply_ &&
        downloadReply_->manager() == &subscriptionNetwork_) {
        ++operationGeneration_;
        downloadReply_->abort();
        downloadReply_.clear();
        busy_ = false;
        auto state = featureState_.value(QStringLiteral("subscriptionDownload")).toObject();
        state.insert(QStringLiteral("state"), QStringLiteral("Cancelled"));
        featureState_.insert(QStringLiteral("subscriptionDownload"), state);
        completeSubscriptionUpdate(QStringLiteral("Cancelled"));
    }
    if (busy_ || store_.readOnly() || profile(id).isEmpty()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    if (id == selectedId_) {
        stop();
        if (!stopFailure_.isEmpty())
            return;
        // Once the selection is cleared, startup reads defaults instead of the selected
        // revision's preference snapshot. Preserve global rules before clearing it.
        const auto defaults =
            store_.write(QStringLiteral("config/runtime_defaults.json"), runtimePreferences_);
        if (const auto* failure = std::get_if<domain::Error>(&defaults)) {
            error(failure->code);
            return;
        }
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
        if (selectedId_.isEmpty()) {
            const auto cleanedRules = userRuleBook();
            if (cleanedRules != runtimePreferences_.value(QStringLiteral("userRules")).toObject()) {
                auto updated = runtimePreferences_;
                updated.insert(QStringLiteral("userRules"), cleanedRules);
                const auto saved =
                    store_.write(QStringLiteral("config/runtime_defaults.json"), updated);
                if (const auto* failure = std::get_if<domain::Error>(&saved)) {
                    error(failure->code);
                    return;
                }
                runtimePreferences_ = updated;
                savedPreferences_ = updated;
            }
        }
        collectGarbage();
        publish();
    }
}
void DesktopRuntime::discardConfigTransaction() {
    if (configTransaction_)
        configTransaction_->semantic = {};
    configTransaction_.reset();
}
void DesktopRuntime::applyProfile(const QString& id, bool startAfter) {
    backgroundSuppressed_ = false;
    featureState_.remove(QStringLiteral("userRuleBinding"));
    effectivePreview_.clear();
    const auto item = profile(id);
    if (item.isEmpty()) {
        discardConfigTransaction();
        restorePreferences();
        busy_ = false;
        error(QStringLiteral("ProfileNotFound"));
        return;
    }
    // Restored profile rules are staged with their new catalog identity. Include
    // those layers in the preference snapshot committed with the runtime revision.
    const auto currentRules = userRuleBook();
    if (currentRules != runtimePreferences_.value(QStringLiteral("userRules")).toObject()) {
        if (previousPreferences_.isEmpty())
            previousPreferences_ = runtimePreferences_;
        runtimePreferences_.insert(QStringLiteral("userRules"), currentRules);
    }
    const auto selected = repository_.manifest().value(QStringLiteral("selection")).toObject();
    const bool adjustSelected = id == selectedId_ && !previousPreferences_.isEmpty() &&
                                previousPreferences_.value(QStringLiteral("userRules")) ==
                                    runtimePreferences_.value(QStringLiteral("userRules"));
    auto context = configTransaction_;
    const bool trusted = context && context->validated &&
                         context->generation == operationGeneration_ && context->profileId == id &&
                         context->preferences == runtimePreferences_ && !adjustSelected &&
                         userRuleBook().isEmpty() &&
                         context->imported.value(QStringLiteral("sourceRef")) ==
                             item.value(QStringLiteral("sourceRef")) &&
                         context->imported.value(QStringLiteral("sourceHash")) ==
                             item.value(QStringLiteral("sourceHash")) &&
                         context->imported.value(QStringLiteral("snapshotRef")) ==
                             item.value(QStringLiteral("snapshotRef")) &&
                         context->imported.value(QStringLiteral("snapshotHash")) ==
                             item.value(QStringLiteral("snapshotHash")) &&
                         context->imported.value(QStringLiteral("overlay")) ==
                             item.value(QStringLiteral("overlay")) &&
                         context->imported.value("customGroups") == item.value("customGroups");
    if (trusted) {
        const auto reference = item.contains(QStringLiteral("snapshotRef"))
                                   ? QStringLiteral("snapshotRef")
                                   : QStringLiteral("sourceRef");
        const auto digest = item.contains(QStringLiteral("snapshotRef"))
                                ? QStringLiteral("snapshotHash")
                                : QStringLiteral("sourceHash");
        const auto checked =
            store_.verifyBytes(item.value(reference).toString(), item.value(digest).toString());
        if (const auto* failure = std::get_if<domain::Error>(&checked)) {
            discardConfigTransaction();
            busy_ = false;
            error(failure->code);
            return;
        }
        if (item.contains(QStringLiteral("snapshotRef"))) {
            const auto sourceChecked =
                store_.verifyBytes(item.value(QStringLiteral("sourceRef")).toString(),
                                   item.value(QStringLiteral("sourceHash")).toString());
            if (const auto* failure = std::get_if<domain::Error>(&sourceChecked)) {
                discardConfigTransaction();
                busy_ = false;
                error(failure->code);
                return;
            }
        }
    } else {
        discardConfigTransaction();
        context = std::make_shared<ConfigTransaction>();
        context->profileId = id;
        context->preferences = runtimePreferences_;
        // This read and its preflight temporaries end before validation is started.
        const auto source = store_.readBytes(
            adjustSelected ? selected.value(QStringLiteral("artifactRef")).toString()
                           : item.value(item.contains(QStringLiteral("snapshotRef"))
                                            ? QStringLiteral("snapshotRef")
                                            : QStringLiteral("sourceRef"))
                                 .toString());
        if (const auto* failure = std::get_if<domain::Error>(&source)) {
            busy_ = false;
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
            busy_ = false;
            restorePreferences();
            error(QStringLiteral("SourceIntegrityFailed"));
            return;
        }
        auto overlaid = adjustSelected
                            ? domain::Result<QByteArray>(std::get<QByteArray>(source))
                            : infra::composeProfileConfig(std::get<QByteArray>(source), item);
        if (const auto* failure = std::get_if<domain::Error>(&overlaid)) {
            busy_ = false;
            restorePreferences();
            configError(*failure);
            return;
        }
        if (!adjustSelected) {
            const auto withRules =
                infra::composeUserRules(std::get<QByteArray>(overlaid), userRuleBook(), id);
            if (const auto* failure = std::get_if<domain::Error>(&withRules)) {
                if (failure->code == QStringLiteral("UserRuleProxyMissing") && !userRuleApplying_) {
                    const auto targets = infra::userRuleTargets(std::get<QByteArray>(overlaid));
                    if (const auto* value = std::get_if<QJsonObject>(&targets))
                        featureState_.insert(QStringLiteral("userRuleBinding"),
                                             QJsonObject{{"profileId", id},
                                                         {"profileName", item.value("name")},
                                                         {"groups", value->value("groups")}});
                }
                busy_ = false;
                restorePreferences();
                configError(*failure);
                return;
            }
            overlaid = withRules;
        }
        auto composed = infra::preflightConfig(std::get<QByteArray>(overlaid), preferences());
        if (const auto* failure = std::get_if<domain::Error>(&composed)) {
            busy_ = false;
            restorePreferences();
            configError(*failure);
            return;
        }
        context->semantic = std::move(std::get<infra::PreflightConfig>(composed).semanticYaml);
    }
    busy_ = true;
    status_ = QStringLiteral("Validating");
    invalidateWebsiteTest();
    const auto generation = ++operationGeneration_;
    context->generation = generation;
    configTransaction_ = context;
    publish();
    const auto weak = std::weak_ptr<ConfigTransaction>(context);
    auto commit = [this, generation, weak, id, startAfter](bool valid) {
        const auto context = weak.lock();
        if (!context || context != configTransaction_ || generation != operationGeneration_) {
            return;
        }
        if (!valid) {
            discardConfigTransaction();
            restorePreferences();
            busy_ = false;
            tunPending_ = false;
            tunDisabling_ = false;
            status_ = engine_.ready() ? QStringLiteral("Ready") : QStringLiteral("Stopped");
            error(lastError_.isEmpty() ? QStringLiteral("CoreConfigValidationFailed") : lastError_);
            return;
        }
        context->validated = true;
        const auto& semantic = context->semantic;
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
            discardConfigTransaction();
            restorePreferences();
            busy_ = false;
            error(failure->code);
            return;
        }
        context->artifactRef = artifact;
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
            discardConfigTransaction();
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
            discardConfigTransaction();
            confirmed();
            return;
        }
        previouslyProxied_ = proxyLease_->active();
        if (previousPreferences_.isEmpty()) {
            const auto previousPref =
                store_.read(previous_.value(QStringLiteral("selection"))
                                .toObject()
                                .value(QStringLiteral("preferenceSnapshotRef"))
                                .toString());
            if (const auto* value = std::get_if<std::optional<QJsonObject>>(&previousPref);
                value && *value)
                previousPreferences_ = **value;
            previousPreferences_.remove(QStringLiteral("schemaVersion"));
        }
        proxyDesired_ = previouslyProxied_;
        if (modeOnlyApply_ && engine_.ready()) {
            const auto staged = repository_.phase(QStringLiteral("CoreApplying"));
            if (const auto* failure = std::get_if<domain::Error>(&staged)) {
                failedApply(failure->code);
                return;
            }
            status_ = QStringLiteral("Reloading");
            discardConfigTransaction();
            publish();
            if (generation != operationGeneration_ || !modeOnlyApply_)
                return;
            engine_.changeMode(runtimePreferences_.value(QStringLiteral("mode")).toString(),
                               [this, generation](bool success) {
                                   if (generation != operationGeneration_ || !modeOnlyApply_)
                                       return;
                                   if (!success) {
                                       failedApply(QStringLiteral("CoreModeReadbackFailed"));
                                       return;
                                   }
                                   sessionPreferences_ = runtimePreferences_;
                                   confirmed();
                               });
            return;
        }
        if (previouslyProxied_) {
            const auto suspended = proxyLease_->suspend();
            if (const auto* failure = std::get_if<domain::Error>(&suspended)) {
                // Core has not changed; preserve the current session on a lease conflict.
                const auto rolledBack = repository_.rollback();
                Q_UNUSED(rolledBack)
                restorePreferences();
                discardConfigTransaction();
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
        const auto stopped = engine_.stop();
        if (const auto* failure = std::get_if<domain::Error>(&stopped)) {
            failedApply(failure->code);
            return;
        }
        cleanupSession();
        QTimer::singleShot(0, this, [this, generation] {
            if (generation == operationGeneration_ && applying_)
                launch();
        });
    };
    if (!trusted)
        validate(context->semantic,
                 [this, generation, commit = std::move(commit)](bool valid) mutable {
                     QTimer::singleShot(
                         0, this, [this, generation, valid, commit = std::move(commit)]() mutable {
                             if (generation == operationGeneration_)
                                 commit(valid);
                         });
                 });
    else
        QTimer::singleShot(0, this, [commit = std::move(commit)]() mutable { commit(true); });
}
void DesktopRuntime::launch() {
    if (engine_.running() && !engine_.usingService()) {
        failedApply(QStringLiteral("CoreStopFailed"));
        return;
    }
    loadedFeatures_.clear();
    pendingFeatureRequests_.clear();
    cancelDelayTests();
    resetConnectionClose();
    featureState_.remove(QStringLiteral("connectionsPaused"));
    featureState_.remove(QStringLiteral("connectionSample"));
    featureState_.remove(QStringLiteral("connectionError"));
    featureState_.remove(QStringLiteral("connectionsStale"));
    connections_ = {};
    rules_ = {};
    providers_ = {};
    providerDraft_ = {};
    rulePageState_ = {};
    providerPageState_ = {};
    providerResults_ = {};
    delays_ = {};
    proxyLatency_.clear();
    groupDelayTargets_.clear();
    delayDetails_ = {};
    delayTimes_ = {};
    delayExpiry_ = {};
    delayViewsAt_ = {};
    featureState_.remove(QStringLiteral("delayTest"));
    featureState_.remove(QStringLiteral("traffic"));
    featureState_.remove(QStringLiteral("dnsResult"));
    connectionSampleMs_ = 0;
    const auto selection = candidate_.value(QStringLiteral("selection")).toObject();
    QByteArray semantic;
    if (configTransaction_ && configTransaction_->validated &&
        configTransaction_->generation == operationGeneration_ &&
        configTransaction_->preferences == runtimePreferences_ &&
        configTransaction_->artifactRef ==
            selection.value(QStringLiteral("artifactRef")).toString() &&
        hash(configTransaction_->semantic) ==
            selection.value(QStringLiteral("effectiveHash")).toString()) {
        const auto checked =
            store_.verifyBytes(configTransaction_->artifactRef,
                               selection.value(QStringLiteral("effectiveHash")).toString());
        if (const auto* failure = std::get_if<domain::Error>(&checked)) {
            discardConfigTransaction();
            failedApply(failure->code);
            return;
        }
        semantic = std::move(configTransaction_->semantic);
        configTransaction_.reset();
    } else {
        discardConfigTransaction();
        auto loaded = store_.readBytes(selection.value(QStringLiteral("artifactRef")).toString());
        if (const auto* failure = std::get_if<domain::Error>(&loaded)) {
            failedApply(failure->code);
            return;
        }
        semantic = std::move(std::get<QByteArray>(loaded));
        if (hash(semantic) != selection.value(QStringLiteral("effectiveHash")).toString()) {
            semantic = {};
            failedApply(QStringLiteral("ArtifactIntegrityFailed"));
            return;
        }
    }
    auto key = credentials_->read(QStringLiteral("c137006a-58f4-4d6c-a795-b987e368c820"));
    const auto keyPath =
        store_.path(QStringLiteral("data/credentials/c137006a-58f4-4d6c-a795-b987e368c820.blob"));
    if (std::holds_alternative<domain::Error>(key) && std::holds_alternative<QString>(keyPath) &&
        !QFileInfo::exists(std::get<QString>(keyPath))) {
        const auto bytes = newSecret();
        const auto saved =
            credentials_->write(QStringLiteral("c137006a-58f4-4d6c-a795-b987e368c820"), bytes);
        if (std::holds_alternative<std::monostate>(saved))
            key = bytes;
    }
    if (const auto* failure = std::get_if<domain::Error>(&key)) {
        failedApply(failure->code);
        return;
    }
    identities_ = infra::nodeIdentities(semantic, std::get<QByteArray>(key));
    const auto summary = infra::configurationSummary(semantic);
    configuredGroups_ = summary.value(QStringLiteral("groups")).toArray();
    defaultPolicy_ = summary.value(QStringLiteral("defaultPolicy")).toString();
    featureState_.insert(QStringLiteral("nodes"), summary.value(QStringLiteral("nodes")));
    restoringNodes_ = true;
    restoredGroups_.clear();
    epoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    secret_ = newSecret();
    const auto reference = QStringLiteral("data/sessions/%1/config.yaml").arg(epoch_);
    {
        const auto materialized = infra::materializeSession({semantic, {}}, secret_);
        if (const auto* failure = std::get_if<domain::Error>(&materialized)) {
            semantic = {};
            failedApply(failure->code);
            return;
        }
        const auto result = store_.writeBytes(reference, std::get<QByteArray>(materialized), true);
        if (const auto* failure = std::get_if<domain::Error>(&result)) {
            semantic = {};
            failedApply(failure->code);
            return;
        }
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
                semantic, runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject())) {
        failedApply(QStringLiteral("GeoResourceIntegrityFailed"));
        return;
    }
    const auto resources = infra::prepareGeoResources(
        semantic, std::get<QString>(coreDirectory),
        runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject(), store_.root());
    if (const auto* failure = std::get_if<domain::Error>(&resources)) {
        semantic = {};
        failedApply(failure->code);
        return;
    }
    const auto providerCache =
        infra::prepareRuntimeProviderCache(semantic, store_, QStringLiteral("data/core_state"));
    if (const auto* failure = std::get_if<domain::Error>(&providerCache)) {
        failedApply(failure->code);
        return;
    }
    semantic = {}; // Engine/service startup must not nest underneath retained YAML buffers.
    status_ = QStringLiteral("Starting");
    sessionPreferences_ = runtimePreferences_;
    sessionProfileId_ = applyingId_.isEmpty() ? selectedId_ : applyingId_;
    publish();
    engine_.start(
        coreExecutable(), std::get<QString>(config), std::get<QString>(coreDirectory), secret_,
        values.mixedPort, values.controllerPort,
        runtimePreferences_.value(QStringLiteral("mode")).toString(),
        runtimePreferences_.value(QStringLiteral("tunDesired")).toBool() && !tunRecoveryBlocked_,
        runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject(), store_.root(),
        runtimePreferences_.value(QStringLiteral("coreArtifactId")).toString());
}
void DesktopRuntime::confirmed() {
    if (engine_.ready()) {
        effectivePreferences_ = sessionPreferences_;
        effectiveProfileId_ = sessionProfileId_;
    }
    QString networkFailure;
    if (startAfter_ && applying_ && !recovering_) {
        const auto result = repository_.phase(QStringLiteral("CoreConfirmed"));
        if (const auto* failure = std::get_if<domain::Error>(&result)) {
            failedApply(failure->code);
            return;
        }
    }
    if (proxyDesired_ && !modeOnlyApply_) {
        if (applying_) {
            const auto intent = repository_.phase(QStringLiteral("PlatformApplying"));
            if (const auto* failure = std::get_if<domain::Error>(&intent)) {
                failedApply(failure->code);
                return;
            }
        }
        const auto leased = proxyLease_->acquire(preferences().mixedPort, epoch_);
        if (const auto* failure = std::get_if<domain::Error>(&leased)) {
            if (backgroundManaged_) {
                // A platform proxy failure must not take down the local forwarding engine.
                networkFailure = failure->code;
                proxyDesired_ = false;
                const auto released = proxyLease_->release();
                if (const auto* restoreFailure = std::get_if<domain::Error>(&released))
                    networkFailure = restoreFailure->code;
                const auto saved = saveProxyIntent(false);
                if (const auto* saveFailure = std::get_if<domain::Error>(&saved))
                    networkFailure = saveFailure->code;
                error(networkFailure);
            } else if (applying_)
                failedApply(failure->code);
            else {
                const auto released = proxyLease_->release();
                if (const auto* restoreFailure = std::get_if<domain::Error>(&released))
                    error(restoreFailure->code);
                const auto stopped = engine_.stop();
                const auto* stopFailure = std::get_if<domain::Error>(&stopped);
                cleanupSession();
                busy_ = false;
                status_ = stopFailure ? QStringLiteral("Uncertain") : QStringLiteral("Failed");
                const auto code = stopFailure ? stopFailure->code : failure->code;
                error(code);
                finishOperation(stopFailure ? domain::OperationState::Uncertain
                                            : domain::OperationState::Failed,
                                code);
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
    if (proxyDesired_ && !modeOnlyApply_) {
        previousProxyIntent_ = persistedProxyIntent_;
        const auto saved = saveProxyIntent(true);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            failedApply(failure->code);
            return;
        }
    }
    const auto committed = repository_.commit();
    if (const auto* failure = std::get_if<domain::Error>(&committed)) {
        if (modeOnlyApply_) {
            failedApply(failure->code);
            return;
        }
        if (store_.readOnly()) {
            if (previousProxyIntent_) {
                const auto restored = saveProxyIntent(*previousProxyIntent_);
                previousProxyIntent_.reset();
                if (const auto* intentFailure = std::get_if<domain::Error>(&restored))
                    error(intentFailure->code);
            }
            applying_ = false;
            busy_ = false;
            const auto stopped = engine_.stop();
            const auto* stopFailure = std::get_if<domain::Error>(&stopped);
            const auto released = proxyLease_->release();
            Q_UNUSED(released)
            cleanupSession();
            status_ = stopFailure ? QStringLiteral("Uncertain") : QStringLiteral("Failed");
            const auto code = stopFailure ? stopFailure->code : failure->code;
            error(code);
            finishOperation(stopFailure ? domain::OperationState::Uncertain
                                        : domain::OperationState::Failed,
                            code);
            return;
        }
        failedApply(failure->code);
        return;
    }
    previousProxyIntent_.reset();
    if (selectedId_ != applyingId_) {
        cancelDelayTests();
        delays_ = {};
        proxyLatency_.clear();
        delayDetails_ = {};
        delayTimes_ = {};
        delayExpiry_ = {};
        delayViewsAt_ = {};
        featureState_.remove(QStringLiteral("delayTest"));
    }
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
    modeOnlyApply_ = false;
    applying_ = false;
    busy_ = false;
    savedPreferences_ = runtimePreferences_;
    if (catalogSaved && networkFailure.isEmpty())
        lastError_.clear();
    configIssues_ = {};
    status_ = engine_.ready() ? QStringLiteral("Ready") : QStringLiteral("Stopped");
    if (!engine_.ready())
        loadOfflineGroups();
    else
        pollFeatures();
    invalidateWebsiteTest();
    finishOperation(networkFailure.isEmpty() ? domain::OperationState::Succeeded
                                             : domain::OperationState::Failed,
                    networkFailure);
    collectGarbage();
    scheduleBackgroundStart();
    publish();
}
void DesktopRuntime::failedApply(const QString& code) {
    if (modeOnlyApply_ && engine_.ready()) {
        failModeChange(code);
        return;
    }
    modeOnlyApply_ = false;
    const bool failedTunStart =
        sessionPreferences_.value(QStringLiteral("tunDesired")).toBool() && !tunRecoveryBlocked_;
    tunPending_ = false;
    tunDisabling_ = false;
    discardConfigTransaction();
    const auto stopped = engine_.stop();
    const auto* stopFailure = std::get_if<domain::Error>(&stopped);
    if (stopFailure || engine_.cleanupUncertain() ||
        infra::classifyRecoveryFailure(code) == infra::RecoveryFailure::CleanupUncertain) {
        const auto failureCode = stopFailure ? stopFailure->code : code;
        applying_ = false;
        busy_ = false;
        status_ = QStringLiteral("Uncertain");
        recoveryTimer_.stop();
        backgroundSuppressed_ = true;
        error(failureCode);
        const auto released = proxyLease_->release();
        Q_UNUSED(released)
        finishOperation(domain::OperationState::Uncertain, failureCode);
        return;
    }
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
            const auto generation = operationGeneration_;
            QTimer::singleShot(0, this, [this, generation] {
                if (generation == operationGeneration_ && recovering_)
                    launch();
            });
            return;
        }
    }
    recovering_ = false;
    applying_ = false;
    busy_ = false;
    status_ = QStringLiteral("Failed");
    const auto released = proxyLease_->release();
    if (const auto* failure = std::get_if<domain::Error>(&released))
        error(failure->code);
    const auto result = repository_.rollback();
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
    }
    publish();
    finishOperation(domain::OperationState::Failed, code);
    const auto recovery = infra::classifyRecoveryFailure(code);
    if (recovery == infra::RecoveryFailure::Retry && !engine_.cleanupUncertain())
        scheduleBackgroundStart(true, failedTunStart);
    else {
        backgroundSuppressed_ = true;
        recoveryTimer_.stop();
        if (recovery == infra::RecoveryFailure::CleanupUncertain)
            status_ = QStringLiteral("Uncertain");
    }
    publish();
}
void DesktopRuntime::failModeChange(const QString& code) {
    const auto operationId = repository_.pending().value(QStringLiteral("operationId")).toString();
    if (!operationId.isEmpty() &&
        repository_.manifest().value(QStringLiteral("committedOperationId")) == operationId) {
        // Manifest replacement is the commit point even if journal finalization failed.
        modeOnlyApply_ = false;
        previousPreferences_ = {};
        savedPreferences_ = runtimePreferences_;
        effectivePreferences_ = sessionPreferences_;
        applying_ = false;
        busy_ = false;
        status_ = QStringLiteral("Ready");
        error(code);
        finishOperation(domain::OperationState::Uncertain, code);
        publish();
        return;
    }
    // The original journal stays pending until rollback is confirmed on the live core.
    const auto generation = operationGeneration_;
    const auto oldPreferences = previousPreferences_;
    engine_.changeMode(oldPreferences.value(QStringLiteral("mode")).toString(),
                       [this, generation, oldPreferences, code](bool restored) {
                           if (generation != operationGeneration_ || !modeOnlyApply_)
                               return;
                           modeOnlyApply_ = false;
                           runtimePreferences_ = oldPreferences;
                           previousPreferences_ = {};
                           sessionPreferences_ = oldPreferences;
                           effectivePreferences_ = oldPreferences;
                           applying_ = false;
                           busy_ = false;
                           if (!restored) {
                               const auto stopped = engine_.stop();
                               Q_UNUSED(stopped)
                               const auto released = proxyLease_->release();
                               Q_UNUSED(released)
                               status_ = QStringLiteral("Uncertain");
                               backgroundSuppressed_ = true;
                               recoveryTimer_.stop();
                               error(QStringLiteral("CoreModeRecoveryRequired"));
                               finishOperation(domain::OperationState::Uncertain,
                                               QStringLiteral("CoreModeRecoveryRequired"));
                           } else {
                               const auto rolledBack = repository_.rollback();
                               const auto* failure = std::get_if<domain::Error>(&rolledBack);
                               status_ = QStringLiteral("Ready");
                               error(failure ? failure->code : code);
                               finishOperation(failure ? domain::OperationState::Uncertain
                                                       : domain::OperationState::Failed,
                                               failure ? failure->code : code);
                           }
                           publish();
                       });
}
void DesktopRuntime::recordCoreExit() {
    const auto diagnostics = engine_.coreExitDiagnostics();
    if (diagnostics.isEmpty())
        return;
    auto detail = QStringLiteral("Core exit diagnostics: code=%1, time=%2")
                      .arg(diagnostics.value(QStringLiteral("coreExitCode")).toInteger())
                      .arg(diagnostics.value(QStringLiteral("coreExitedAt")).toString());
    for (const auto* key : {"stopReason", "stopDetail", "coreStderr"}) {
        const auto value = diagnostics.value(QLatin1String(key)).toString();
        if (!value.isEmpty())
            detail += QStringLiteral("; %1=%2").arg(QLatin1String(key), value);
    }
    if (coreExitDetail_ != detail)
        appendLog(QStringLiteral("App"), QStringLiteral("error"), detail);
    coreExitDetail_ = detail;
}
void DesktopRuntime::start(bool systemProxy) {
    listenerRecoveryBlocked_ = false;
    listenerIssue_ = {};
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
    if (!restartAttempt_) {
        crashTimes_.clear();
        tunRecoveryBudget_.reset();
    }
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
    modeOnlyApply_ = false;
    cancelCoreUpdate();
    const auto archiveStopped = coreArchive_.cancel();
    if (!coreArchive_.running())
        coreStage_.reset();
    if (coreOperationActive_) {
        coreOperationActive_ = false;
        coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Cancelled"));
    }
    clearMaintenancePreview();
    snapshotProviderRefresh_ = {};
    releaseFeature(QStringLiteral("rules"));
    releaseFeature(QStringLiteral("providers"));
    if (geoOperationActive_) {
        geoOperationActive_ = false;
        featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Cancelled"));
    }
    featureState_.remove(QStringLiteral("dnsResult"));
    cancelDelayTests();
    resetConnectionClose();
    featureState_.remove(QStringLiteral("connectionsPaused"));
    featureState_.remove(QStringLiteral("connectionSample"));
    featureState_.remove(QStringLiteral("connectionError"));
    featureState_.remove(QStringLiteral("connectionsStale"));
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
    if (const auto* failure = std::get_if<domain::Error>(&archiveStopped)) {
        stopFailure_ = failure->code;
        error(failure->code);
    }
    finishOperation(domain::OperationState::Cancelled, QStringLiteral("UserStopped"));
    ++operationGeneration_;
    providerImport_.reset();
    cancelBackupRestore();
    discardConfigTransaction();
    recoveryTimer_.stop();
    reconcileTimer_.stop();
    delayReconcileTimer_.stop();
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
    if (downloadReply_) {
        const bool subscription = downloadReply_->manager() == &subscriptionNetwork_;
        downloadReply_->abort();
        downloadReply_.clear();
        if (subscription) {
            auto state = featureState_.value(QStringLiteral("subscriptionDownload")).toObject();
            state.insert(QStringLiteral("state"), QStringLiteral("Cancelled"));
            featureState_.insert(QStringLiteral("subscriptionDownload"), state);
        }
    }
    invalidateWebsiteTest();
    const auto validationStopped = stopValidation();
    if (const auto* failure = std::get_if<domain::Error>(&validationStopped)) {
        stopFailure_ = failure->code;
        error(failure->code);
    }
    const auto released = proxyLease_->release();
    if (const auto* failure = std::get_if<domain::Error>(&released)) {
        stopFailure_ = failure->code;
        error(failure->code);
    }
    const auto coreStopped = engine_.stop();
    if (const auto* failure = std::get_if<domain::Error>(&coreStopped)) {
        stopFailure_ = failure->code;
        busy_ = false;
        applying_ = false;
        status_ = QStringLiteral("Uncertain");
        error(failure->code);
        publish();
        return;
    }
    busy_ = false;
    pendingGroup_.clear();
    pendingMember_.clear();
    queuedSelections_.clear();
    restoringNodes_ = false;
    applying_ = false;
    recovering_ = false;
    status_ = stopFailure_.isEmpty() ? QStringLiteral("Stopped") : QStringLiteral("Uncertain");
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
    if (!busy_ && !engine_.cleanupUncertain() && desired == engine_.tunActive() &&
        desired == runtimePreferences_.value(QStringLiteral("tunDesired")).toBool())
        return;
    // An explicit TUN action is a fresh user retry; clear the automatic restart cap.
    tunRecoveryBlocked_ = false;
    tunRecoveryBudget_.reset();
    if (platformInfo_.capabilities.value(QStringLiteral("tun")) == QStringLiteral("Unsupported")) {
        error(QStringLiteral("ServicePlatformUnsupported"));
        return;
    }
    if (busy_ || selectedId_.isEmpty() || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    backgroundSuppressed_ = false;
    recoveryTimer_.stop();
    // Every explicit enable revalidates the service before touching the network. A persisted
    // "desired" flag must not bypass install/update/authorize after a package change or after a
    // previous cleanup failure.
    if (desired) {
        busy_ = true;
        tunPending_ = true;
        tunDisabling_ = false;
        publish();
        tunSetupAttempted_ = false;
        retryTunAfterSetup();
        return;
    }
    previousPreferences_ = runtimePreferences_;
    runtimePreferences_.insert(QStringLiteral("tunDesired"), desired);
    tunPending_ = false;
    tunDisabling_ = true;
    applyProfile(selectedId_, backgroundManaged_ || engine_.ready());
}
void DesktopRuntime::retryTunAfterSetup() {
    const auto generation = ++operationGeneration_;
    engine_.probeService([this, generation](domain::Result<QJsonObject> result) {
        if (generation != operationGeneration_)
            return;
        busy_ = false;
        if (const auto* failure = std::get_if<domain::Error>(&result)) {
            if (startTunSetup(failure->code))
                return;
            error(failure->code);
            return;
        }
        previousPreferences_ = runtimePreferences_;
        runtimePreferences_.insert(QStringLiteral("tunDesired"), true);
        applyProfile(selectedId_, true);
    });
}
bool DesktopRuntime::startTunSetup(const QString& failure) {
    if (!serviceSetup_ || tunSetupAttempted_)
        return false;
    ServiceSetupMode mode;
    if (failure == QStringLiteral("ServiceInstallationRequired"))
        mode = ServiceSetupMode::Install;
    else if (failure == QStringLiteral("ServiceProtocolMismatch"))
        mode = ServiceSetupMode::Update;
    else if (failure == QStringLiteral("ServiceClientUntrusted") ||
             failure == QStringLiteral("ServiceNotRunning"))
        mode = ServiceSetupMode::Authorize;
    else
        return false;
    tunSetupAttempted_ = true;
    busy_ = true;
    featureState_.insert(QStringLiteral("serviceSetupPending"), true);
    featureState_.remove(QStringLiteral("serviceSetupLog"));
    publish();
    serviceSetup_->start(
        mode, QCoreApplication::applicationDirPath(),
        [this](int exitCode) { finishTunSetup(exitCode); },
        [this](const QString& code) {
            featureState_.remove(QStringLiteral("serviceSetupPending"));
            if (serviceSetup_ && !serviceSetup_->logPath().isEmpty())
                featureState_.insert(QStringLiteral("serviceSetupLog"), serviceSetup_->logPath());
            busy_ = false;
            tunPending_ = false;
            error(code);
        });
    return true;
}
void DesktopRuntime::finishTunSetup(int exitCode) {
    featureState_.remove(QStringLiteral("serviceSetupPending"));
    if (serviceSetup_ && !serviceSetup_->logPath().isEmpty())
        featureState_.insert(QStringLiteral("serviceSetupLog"), serviceSetup_->logPath());
    if (exitCode != 0) {
        busy_ = false;
        tunPending_ = false;
        error(QStringLiteral("ServiceSetupFailed"));
        return;
    }
    retryTunAfterSetup();
}
void DesktopRuntime::cleanupSession() {
    if (engine_.running() && !engine_.usingService())
        return;
    for (const auto& stream :
         {QStringLiteral("connections"), QStringLiteral("rules"), QStringLiteral("providers"),
          QStringLiteral("traffic"), QStringLiteral("resources")})
        releaseFeature(stream);
    loadedFeatures_.clear();
    pendingFeatureRequests_.clear();
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
    if (store_.readOnly() || repository_.recoveryRequired() || applying_ || busy_ ||
        configTransaction_ || backupRestore_ || providerImport_ || downloadReply_)
        return;
    // Validate the whole tree and all journals before deleting even a single orphan.
    const auto inventory = infra::inspectStorage(store_);
    if (std::holds_alternative<domain::Error>(inventory))
        return;
    const auto journalsBefore = store_.files(QStringLiteral("data/transactions"));
    if (!std::holds_alternative<QStringList>(journalsBefore))
        return;
    for (const auto& ref : std::get<QStringList>(journalsBefore)) {
        const auto journal = store_.read(ref);
        const auto* value = std::get_if<std::optional<QJsonObject>>(&journal);
        if (!value || !*value ||
            ((*value)->value("stage") != "Committed" && (*value)->value("stage") != "RolledBack"))
            return;
    }
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
                                 QStringLiteral("6c7a5e7f-56c4-4f6e-a2c1-15c0a2f2ef31"),
                                 QStringLiteral("e430ed25-78b8-47ea-a7b0-d746a5783891")};
    const auto retainProviders = [&sources](const QJsonObject& item) {
        for (const auto& value : item.value(QStringLiteral("providerResources")).toArray())
            sources.insert(value.toObject().value(QStringLiteral("sourceRef")).toString());
    };
    for (const auto& item : profiles_) {
        retainProviders(item.toObject());
        sources.insert(item.toObject().value(QStringLiteral("sourceRef")).toString());
        sources.insert(item.toObject().value(QStringLiteral("snapshotRef")).toString());
        for (const auto& version : item.toObject().value(QStringLiteral("history")).toArray()) {
            retainProviders(version.toObject());
            sources.insert(version.toObject().value(QStringLiteral("sourceRef")).toString());
            sources.insert(version.toObject().value(QStringLiteral("snapshotRef")).toString());
            credentialRefs.insert(version.toObject().value(QStringLiteral("secretRef")).toString());
        }
    }
    for (const auto& item : profiles_)
        credentialRefs.insert(item.toObject().value(QStringLiteral("secretRef")).toString());
    const auto draftFiles = store_.files(QStringLiteral("data/drafts"));
    if (!std::holds_alternative<QStringList>(draftFiles))
        return;
    if (backupRestore_)
        for (const auto& reference : backupRestore_->candidates->references)
            sources.insert(reference);
    if (const auto* files = std::get_if<QStringList>(&draftFiles)) {
        for (const auto& file : *files) {
            const auto bytes = store_.readBytes(file);
            if (const auto* value = std::get_if<QByteArray>(&bytes)) {
                const auto parsed = QJsonDocument::fromJson(*value);
                if (!parsed.isObject() || parsed.object().isEmpty())
                    return;
                const auto draft = parsed.object();
                if (draft.value("schemaVersion") != 1 ||
                    draft.value("baseSourceRef").toString().isEmpty() ||
                    std::holds_alternative<domain::Error>(
                        store_.path(draft.value("baseSourceRef").toString())))
                    return;
                sources.insert(draft.value(QStringLiteral("baseSourceRef")).toString());
                sources.insert(draft.value(QStringLiteral("baseSnapshotRef")).toString());
            } else
                return;
        }
    }
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
    retainGeo({{QStringLiteral("geoSnapshot"), geoPending_}});
    for (const auto& revision : revisions) {
        if (revision.isEmpty())
            continue;
        const auto pref = store_.read(QStringLiteral("data/preferences/%1.json").arg(revision));
        if (std::holds_alternative<domain::Error>(pref) ||
            !std::get<std::optional<QJsonObject>>(pref))
            return;
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
    const auto usage = infra::inspectStorage(store_);
    if (const auto* value = std::get_if<infra::StorageInventory>(&usage)) {
        const auto cleaned = infra::trimStorage(store_, *value);
        if (const auto* result = std::get_if<QJsonObject>(&cleaned))
            featureState_.insert(QStringLiteral("storageCleanup"), *result);
        featureState_.insert(QStringLiteral("storageUsage"), value->summary());
    }
}
void DesktopRuntime::restorePreferences() {
    if (!previousPreferences_.isEmpty())
        runtimePreferences_ = previousPreferences_;
    previousPreferences_ = {};
    modeOnlyApply_ = false;
}
domain::CommandResult DesktopRuntime::stopValidation() {
    validationDeadline_.stop();
    validationCompletion_ = {};
    const auto stopped = validator_->stop();
    if (!validator_->running())
        staging_.reset();
    return stopped;
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
    if (runtimePreferences_.value(QStringLiteral("mode")) == mode)
        return;
    modeOnlyApply_ = engine_.ready();
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
    listenerRecoveryBlocked_ = false;
    listenerIssue_ = {};
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
        } else {
            previousPreferences_ = {};
            savedPreferences_ = runtimePreferences_;
        }
        publish();
    }
}
void DesktopRuntime::selectProxy(const QString& group, const QString& member) {
    if ((busy_ && (pendingGroup_.isEmpty() || restoringNodes_)) || !identities_.contains(group) ||
        !identities_.contains(member))
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
    if (busy_) {
        // One latest intent per group; the in-flight selection always completes readback.
        queuedSelections_.insert(group, member);
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
    invalidateWebsiteTest();
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
    if (!proxyLease_->conflict() || proxyLease_->active()) {
        error(QStringLiteral("SystemProxyConflictRequired"));
        return;
    }
    auto result = proxyLease_->reapply(preferences().mixedPort, epoch_);
    if (std::holds_alternative<std::monostate>(result))
        result = saveProxyIntent(true);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        const auto released = proxyLease_->release();
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
    const bool wasPending = delayReconcilePending_;
    restartDelayTestsAfterReconcile();
    delayReconcileTimer_.start();
    if (!reconcileTimer_.isActive()) {
        // Invalidate immediately: an old reply can finish during the debounce window.
        invalidateWebsiteTest();
        reconcileTimer_.start();
        publish();
    } else if (!wasPending && delayReconcilePending_)
        publish();
}
QUuid DesktopRuntime::setSystemProxy(bool desired) {
    if (platformInfo_.capabilities.value(QStringLiteral("systemProxy")) !=
        QStringLiteral("Supported")) {
        error(QStringLiteral("SystemProxyUnsupported"));
        return {};
    }
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
    invalidateWebsiteTest();
    auto result =
        desired ? proxyLease_->acquire(preferences().mixedPort, epoch_) : proxyLease_->release();
    if (desired && std::holds_alternative<domain::Error>(result) && proxyLease_->conflict() &&
        !proxyLease_->active()) {
        // An explicit switch-on authorizes abandoning a conflicted lease: the
        // current external value becomes the new restore baseline instead of
        // failing closed behind a hidden recovery entry. Corrupted lease files
        // still require manual repair and are not auto-baselined.
        result = proxyLease_->reapply(preferences().mixedPort, epoch_);
    }
    if (std::holds_alternative<std::monostate>(result))
        result = saveProxyIntent(desired);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        if (desired) {
            const auto released = proxyLease_->release();
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
    publish();
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
    if (testReply_ || busy_) {
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
    const auto saved = credentials_->write(QStringLiteral("e430ed25-78b8-47ea-a7b0-d746a5783891"),
                                           target.toUtf8());
    if (const auto* failure = std::get_if<domain::Error>(&saved)) {
        testQueue_.clear();
        error(failure->code);
        return;
    }
    testTarget_ = target;
    testResult_.clear();
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
    testNetwork_.setProxy(proxy);
    testNetwork_.clearAccessCache();
    auto probe = std::make_shared<WebsiteProbe>();
    probe->generation = ++testGeneration_;
    probe->operationGeneration = operationGeneration_;
    probe->context = QJsonObject{
        {QStringLiteral("path"), path},
        {QStringLiteral("target"), target},
        {QStringLiteral("resolved"), resolved},
        {QStringLiteral("epoch"), epoch_},
        {QStringLiteral("mode"), engine_.ready()
                                     ? effectivePreferences_.value(QStringLiteral("mode"))
                                     : runtimePreferences_.value(QStringLiteral("mode"))},
        {QStringLiteral("selectionGeneration"), QString::number(selectionGeneration())}};
    QJsonObject selected;
    for (const auto& entry : groups_) {
        const auto group = entry.toObject();
        selected.insert(group.value(QStringLiteral("name")).toString(),
                        group.value(QStringLiteral("current")));
    }
    probe->context.insert(QStringLiteral("selections"), selected);
    probe->timer.start();
    requestWebsite(probe, url);
}
void DesktopRuntime::requestWebsite(const std::shared_ptr<WebsiteProbe>& probe, const QUrl& url) {
    const auto remaining = 12000 - probe->timer.elapsed();
    if (remaining <= 0) {
        finishWebsite(probe, QStringLiteral("Timeout"), 0);
        return;
    }
    if (probe->context.value(QStringLiteral("path")) == QStringLiteral("SystemDefault")) {
        const auto proxies = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(url));
        if (proxies.isEmpty()) {
            finishWebsite(probe, QStringLiteral("ProxyUnavailable"), 0);
            return;
        }
        const auto proxy = proxies.first();
        testNetwork_.setProxy(proxy);
        probe->context.insert(
            QStringLiteral("resolved"),
            proxy.type() == QNetworkProxy::NoProxy
                ? QStringLiteral("NoProxy")
                : QStringLiteral("Proxy:%1:%2").arg(proxy.hostName()).arg(proxy.port()));
    }
    probe->visited.insert(url.adjusted(QUrl::NormalizePathSegments));
    QUrl origin;
    origin.setScheme(url.scheme());
    origin.setHost(url.host());
    origin.setPort(url.port());
    probe->context.insert(QStringLiteral("finalOrigin"), origin.toString());
    auto running = probe->context;
    running.insert(QStringLiteral("state"), QStringLiteral("Running"));
    featureState_.insert(QStringLiteral("websiteTest"), running);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(static_cast<int>(qMin<qint64>(10000, remaining)));
    auto* reply = testNetwork_.get(request);
    reply->setReadBufferSize(65537);
    connect(reply, &QIODevice::readyRead, this, [reply, probe] {
        if (infra::consumeAvailable(*reply, probe->received, 65536, [](QByteArrayView) {
                return true;
            }) != infra::ReadStatus::Complete)
            reply->abort();
    });
    testReply_ = reply;
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->start(static_cast<int>(remaining));
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, probe, url] {
        reply->deleteLater();
        if (testReply_ != reply || probe->operationGeneration != operationGeneration_ ||
            probe->generation != testGeneration_ ||
            (probe->context.value(QStringLiteral("path")) == QStringLiteral("CurrentProxy") &&
             !engine_.ready())) {
            if (testReply_ == reply) {
                invalidateWebsiteTest();
                publish();
            }
            return;
        }
        testReply_.clear();
        const auto readOk = infra::consumeAvailable(*reply, probe->received, 65536,
                                                    [](QByteArrayView) { return true; }) ==
                            infra::ReadStatus::Complete;
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString outcome = QStringLiteral("NetworkError");
        if (probe->received > 65536)
            outcome = QStringLiteral("ResponseLimit");
        else if (probe->timer.elapsed() >= 12000)
            outcome = QStringLiteral("Timeout");
        else if (status == 301 || status == 302 || status == 303 || status == 307 ||
                 status == 308) {
            auto next =
                url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            next.setFragment({});
            if (!readOk || reply->error() != QNetworkReply::NoError)
                outcome = QStringLiteral("NetworkError");
            else if (!next.isValid() || next.scheme() != QStringLiteral("https") ||
                     next.host().isEmpty() || !next.userInfo().isEmpty() ||
                     next.toString().size() > 8192)
                outcome = QStringLiteral("RedirectBlocked");
            else if (probe->redirects >= 3 ||
                     probe->visited.contains(next.adjusted(QUrl::NormalizePathSegments)))
                outcome = QStringLiteral("RedirectLimit");
            else {
                ++probe->redirects;
                requestWebsite(probe, next);
                return;
            }
        } else if (readOk && reply->error() == QNetworkReply::NoError && status >= 200 &&
                   status < 300)
            outcome = QStringLiteral("Reachable");
        else if (status > 0)
            outcome = QStringLiteral("HttpError");
        else if (reply->error() == QNetworkReply::SslHandshakeFailedError)
            outcome = QStringLiteral("TlsError");
        else if (reply->error() == QNetworkReply::OperationCanceledError ||
                 reply->error() == QNetworkReply::TimeoutError)
            outcome = QStringLiteral("Timeout");
        else if (reply->error() == QNetworkReply::HostNotFoundError)
            outcome = QStringLiteral("DnsError");
        else if (reply->error() >= QNetworkReply::ProxyConnectionRefusedError &&
                 reply->error() <= QNetworkReply::UnknownProxyError)
            outcome = QStringLiteral("ProxyUnavailable");
        finishWebsite(probe, outcome, status);
    });
    publish();
}
void DesktopRuntime::finishWebsite(const std::shared_ptr<WebsiteProbe>& probe,
                                   const QString& outcome, int status) {
    auto result = probe->context;
    result.insert(QStringLiteral("state"), outcome);
    result.insert(QStringLiteral("status"), status);
    result.insert(QStringLiteral("elapsedMs"), probe->timer.elapsed());
    result.insert(QStringLiteral("bytes"), probe->received);
    result.insert(QStringLiteral("redirects"), probe->redirects);
    result.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    featureState_.insert(QStringLiteral("websiteTest"), result);
    auto history = featureState_.value(QStringLiteral("testHistory")).toArray();
    history.append(result);
    while (history.size() > 32)
        history.removeFirst();
    featureState_.insert(QStringLiteral("testHistory"), history);
    const auto path = probe->context.value(QStringLiteral("path")).toString();
    testResult_ =
        outcome == QStringLiteral("Reachable")
            ? (path == QStringLiteral("CurrentProxy") ? tr("HTTP %1 · %2 ms · Current proxy")
                                                      : tr("HTTP %1 · %2 ms"))
                  .arg(status)
                  .arg(probe->timer.elapsed())
            : (path == QStringLiteral("CurrentProxy")
                   ? tr("Current proxy test failed (no direct fallback)")
                   : tr("Website test failed"));
    if (!testQueue_.isEmpty()) {
        QTimer::singleShot(0, this, [this, path, generation = probe->generation] {
            if (generation == testGeneration_ && !testQueue_.isEmpty())
                testWebsite(testQueue_.takeFirst(), path);
        });
    }
    publish();
}
void DesktopRuntime::invalidateWebsiteTest() {
    cancelIpQueries(true);
    testResult_.clear();
    testQueue_.clear();
    ++testGeneration_;
    auto current = featureState_.value(QStringLiteral("websiteTest")).toObject();
    if (!current.isEmpty()) {
        current.insert(QStringLiteral("state"), QStringLiteral("Stale"));
        featureState_.insert(QStringLiteral("websiteTest"), current);
    }
    auto history = featureState_.value(QStringLiteral("testHistory")).toArray();
    for (qsizetype index = 0; index < history.size(); ++index) {
        auto result = history[index].toObject();
        result.insert(QStringLiteral("stale"), true);
        history[index] = result;
    }
    featureState_.insert(QStringLiteral("testHistory"), history);
    if (testReply_) {
        const auto reply = testReply_;
        testReply_.clear();
        reply->abort();
    }
}
} // namespace quickclash::application
