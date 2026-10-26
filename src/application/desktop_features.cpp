#include "application/desktop_runtime.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <chrono>
#include <utility>
#ifdef Q_OS_WIN
// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace quickclash::application {
void DesktopRuntime::loadOfflineGroups() {
    if (engine_.ready())
        return;
    groups_ = {};
    configuredGroups_ = {};
    defaultPolicy_.clear();
    const auto selection = repository_.manifest().value(QStringLiteral("selection")).toObject();
    if (selection.isEmpty())
        return;
    const auto bytes = store_.readBytes(selection.value(QStringLiteral("artifactRef")).toString());
    if (!std::holds_alternative<QByteArray>(bytes))
        return;
    const auto keyId = QStringLiteral("c137006a-58f4-4d6c-a795-b987e368c820");
    auto key = credentials_.read(keyId);
    const auto keyPath = store_.path(QStringLiteral("data/credentials/%1.blob").arg(keyId));
    if (!std::holds_alternative<QByteArray>(key) && std::holds_alternative<QString>(keyPath) &&
        !QFileInfo::exists(std::get<QString>(keyPath))) {
        const auto generated =
            QUuid::createUuid().toByteArray() + QUuid::createUuid().toByteArray();
        if (!std::holds_alternative<std::monostate>(credentials_.write(keyId, generated)))
            return;
        key = generated;
    }
    if (const auto* failure = std::get_if<domain::Error>(&key)) {
        error(failure->code);
        return;
    }
    const auto semantic = std::get<QByteArray>(bytes);
    identities_ = infra::nodeIdentities(semantic, std::get<QByteArray>(key));
    const auto summary = infra::configurationSummary(semantic);
    groups_ = summary.value(QStringLiteral("groups")).toArray();
    configuredGroups_ = groups_;
    defaultPolicy_ = summary.value(QStringLiteral("defaultPolicy")).toString();
    featureState_.insert(QStringLiteral("nodes"), summary.value(QStringLiteral("nodes")));
    for (qsizetype i = 0; i < groups_.size(); ++i) {
        auto group = groups_[i].toObject();
        const auto name = group.value(QStringLiteral("name")).toString();
        const auto saved = selections_.value(name).toObject();
        const auto member = saved.value(QStringLiteral("member")).toString();
        if (saved.value(QStringLiteral("profileId")) == selectedId_ &&
            saved.value(QStringLiteral("groupIdentity")) == identities_.value(name) &&
            saved.value(QStringLiteral("identity")) == identities_.value(member) &&
            group.value(QStringLiteral("members")).toArray().contains(member))
            group.insert(QStringLiteral("current"), member);
        groups_[i] = group;
    }
}
void DesktopRuntime::setDemands(const QStringList& streams) {
    auto demands = demandState_;
    auto requested = streams;
    if (requested.contains(QStringLiteral("logs")))
        requested.append(QStringLiteral("log_feed"));
    demands.setActive(requested);
    applyDemands(demands);
}
void DesktopRuntime::applyDemands(const RuntimeDemands& demands) {
    for (const auto& stream : RuntimeDemands::streams()) {
        const auto previous = demandState_.generations.value(stream);
        const auto next = demands.generations.value(stream);
        if (next < previous || (next == previous && demands.active.contains(stream) !=
                                                        featureDemands_.contains(stream)))
            return;
    }
    const auto previous = demandState_;
    demandState_ = demands;
    featureDemands_ = demands.active;
    for (const auto& stream : RuntimeDemands::streams()) {
        if (previous.generations.value(stream) == demands.generations.value(stream))
            continue;
        releaseFeature(stream);
        if (stream == QStringLiteral("logs") && featureDemands_.contains(stream) &&
            !previous.active.contains(stream))
            logs_ = appLogs_;
    }
    if (featureDemands_.isEmpty())
        featureTimer_.stop();
    else if (!featureTimer_.isActive())
        featureTimer_.start();
    engine_.setStream(QStringLiteral("traffic"),
                      featureDemands_.contains(QStringLiteral("traffic")));
    engine_.setStream(QStringLiteral("logs"),
                      featureDemands_.contains(QStringLiteral("logs")) &&
                          featureDemands_.contains(QStringLiteral("log_feed")));
    pollFeatures();
}
void DesktopRuntime::releaseFeature(const QString& stream) {
    ++featureRequests_[stream];
    pendingFeatureRequests_.remove(stream);
    loadedFeatures_.remove(stream);
    for (const auto& channel : scopedChannels()) {
        if (channelStream(channel) != stream)
            continue;
        publishedData_.remove(channel);
        invalidatedChannels_.insert(channel);
    }
    if (stream == QStringLiteral("connections")) {
        connections_ = {};
        connectionSampleMs_ = 0;
        featureState_.remove(QStringLiteral("connectionSample"));
        featureState_.remove(QStringLiteral("connectionsStale"));
    } else if (stream == QStringLiteral("rules")) {
        rules_ = {};
    } else if (stream == QStringLiteral("providers")) {
        providers_ = {};
        providerDraft_ = {};
    } else if (stream == QStringLiteral("logs")) {
        engine_.setStream(QStringLiteral("logs"), false);
        logs_ = {};
        droppedLogs_ = 0;
    } else if (stream == QStringLiteral("log_feed")) {
        engine_.setStream(QStringLiteral("logs"), false);
    } else if (stream == QStringLiteral("traffic")) {
        engine_.setStream(QStringLiteral("traffic"), false);
        featureState_.remove(QStringLiteral("traffic"));
    } else if (stream == QStringLiteral("resources")) {
        resourceTick_ = 0;
        for (const auto* key : {"memory", "memorySample", "memoryComplete"})
            featureState_.remove(QLatin1String(key));
    }
}
RuntimeRequest DesktopRuntime::beginFeatureRequest(const QString& stream) {
    pendingFeatureRequests_.insert(stream);
    return {stream, epoch_, demandState_.generations.value(stream), operationGeneration_,
            ++featureRequests_[stream]};
}
bool DesktopRuntime::acceptFeatureRequest(const RuntimeRequest& request) {
    // An old completion must not clear a newer request's in-flight flag.
    if (request.request != featureRequests_.value(request.stream))
        return false;
    pendingFeatureRequests_.remove(request.stream);
    const bool accepted = request.matches(demandState_, epoch_, operationGeneration_,
                                          featureRequests_.value(request.stream));
    if (!accepted && request.stream == QStringLiteral("providers"))
        providerDraft_ = {};
    return accepted;
}
void DesktopRuntime::appendLog(const QString& source, const QString& level,
                               const QString& message) {
    auto safe = message.left(2048);
    safe.replace(QRegularExpression(QStringLiteral("https?://[^\\s]+")), QStringLiteral("[url]"));
    safe.replace(QRegularExpression(
                     QStringLiteral("(?i)(password|secret|token|authorization|uuid)[=: ]+[^ ,;]+")),
                 QStringLiteral("[credential]"));
    const QJsonObject entry{
        {QStringLiteral("localSeq"), QString::number(++logSequence_)},
        {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("source"), source.left(32)},
        {QStringLiteral("level"), level.left(32)},
        {QStringLiteral("message"), safe}};
    // Small App diagnostics survive page release; Core history is strictly page-owned.
    if (source == QStringLiteral("App")) {
        appLogs_.append(entry);
        while (appLogs_.size() > 100)
            appLogs_.removeFirst();
    }
    if (!featureDemands_.contains(QStringLiteral("logs")))
        return;
    logs_.append(entry);
    while (logs_.size() > 1000) {
        logs_.removeFirst();
        ++droppedLogs_;
    }
}
void DesktopRuntime::pollFeatures() {
    if (featureDemands_.contains(QStringLiteral("resources")) && resourceTick_++ % 5 == 0) {
        QJsonObject memory;
        bool memoryComplete = true;
#ifdef Q_OS_WIN
        const auto sample = [](HANDLE handle) {
            struct Counters {
                PROCESS_MEMORY_COUNTERS_EX base;
                SIZE_T privateWorkingSet;
                SIZE_T sharedCommit;
            } counters{};
            counters.base.cb = sizeof(counters);
            if (!GetProcessMemoryInfo(handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                      sizeof(counters)))
                return QJsonObject{};
            return QJsonObject{
                {QStringLiteral("privateWorkingSet"), QString::number(counters.privateWorkingSet)},
                {QStringLiteral("workingSet"), QString::number(counters.base.WorkingSetSize)},
                {QStringLiteral("privateCommit"), QString::number(counters.base.PrivateUsage)}};
        };
        memory.insert(QStringLiteral("client"), sample(GetCurrentProcess()));
        memoryComplete = !memory.value(QStringLiteral("client")).toObject().isEmpty();
        if (!engine_.tunActive()) {
            const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            const auto service =
                manager ? OpenServiceW(manager, L"QuickClashService", SERVICE_QUERY_STATUS)
                        : nullptr;
            if (service) {
                SERVICE_STATUS_PROCESS status{};
                DWORD bytes = 0;
                const bool queried =
                    QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                         reinterpret_cast<BYTE*>(&status), sizeof(status), &bytes);
                memoryComplete &= queried;
                if (queried && status.dwCurrentState == SERVICE_RUNNING) {
                    const auto handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                                    FALSE, status.dwProcessId);
                    const auto serviceMemory = handle ? sample(handle) : QJsonObject{};
                    if (handle)
                        CloseHandle(handle);
                    memoryComplete &= !serviceMemory.isEmpty();
                    auto client = memory.value(QStringLiteral("client")).toObject();
                    for (const auto* field : {"privateWorkingSet", "workingSet", "privateCommit"}) {
                        const auto key = QLatin1String(field);
                        client.insert(key, QString::number(
                                               client.value(key).toString().toULongLong() +
                                               serviceMemory.value(key).toString().toULongLong()));
                    }
                    memory.insert(QStringLiteral("client"), client);
                    memory.insert(QStringLiteral("service"), serviceMemory);
                }
                CloseServiceHandle(service);
            } else if (!manager || GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
                memoryComplete = false;
            if (manager)
                CloseServiceHandle(manager);
        }
        if (engine_.pid()) {
            const auto process =
                OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, engine_.pid());
            if (process) {
                memory.insert(QStringLiteral("core"), sample(process));
                CloseHandle(process);
            }
            memoryComplete &= !memory.value(QStringLiteral("core")).toObject().isEmpty();
        }
#endif
        featureState_.insert(QStringLiteral("memory"), memory);
        featureState_.insert(QStringLiteral("memoryComplete"),
                             memoryComplete && !engine_.tunActive());
        if (engine_.tunActive() && !pendingFeatureRequests_.contains(QStringLiteral("resources"))) {
            const auto ticket = beginFeatureRequest(QStringLiteral("resources"));
            engine_.serviceResources([this, memory,
                                      ticket](domain::Result<QJsonObject> result) mutable {
                if (!acceptFeatureRequest(ticket) || !engine_.tunActive())
                    return;
                const auto* value = std::get_if<QJsonObject>(&result);
                if (value) {
                    const auto service = value->value(QStringLiteral("service")).toObject();
                    auto client = memory.value(QStringLiteral("client")).toObject();
                    for (const auto* field : {"privateWorkingSet", "workingSet", "privateCommit"}) {
                        const auto key = QLatin1String(field);
                        client.insert(key,
                                      QString::number(client.value(key).toString().toULongLong() +
                                                      service.value(key).toString().toULongLong()));
                    }
                    memory.insert(QStringLiteral("client"), client);
                    memory.insert(QStringLiteral("service"), service);
                    memory.insert(QStringLiteral("core"), value->value(QStringLiteral("core")));
                    featureState_.insert(QStringLiteral("memory"), memory);
                    featureState_.insert(
                        QStringLiteral("memoryComplete"),
                        !service.isEmpty() &&
                            !value->value(QStringLiteral("core")).toObject().isEmpty());
                    publish();
                }
            });
        }
        featureState_.insert(QStringLiteral("memorySample"),
                             QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    }
    if (!engine_.ready()) {
        publish();
        return;
    }
    if (featureDemands_.contains(QStringLiteral("proxies")) && !busy_ && proxyTick_++ % 5 == 0)
        engine_.refreshGroups();
    engine_.setStream(QStringLiteral("traffic"),
                      featureDemands_.contains(QStringLiteral("traffic")));
    engine_.setStream(QStringLiteral("logs"),
                      featureDemands_.contains(QStringLiteral("logs")) &&
                          featureDemands_.contains(QStringLiteral("log_feed")));
    for (const auto* stream : {"rules", "providers"})
        if (featureDemands_.contains(QLatin1String(stream)) &&
            !loadedFeatures_.contains(QLatin1String(stream)) &&
            !pendingFeatureRequests_.contains(QLatin1String(stream)) && !busy_)
            requestPageData(QLatin1String(stream));
    if (featureDemands_.contains(QStringLiteral("connections")) &&
        !pendingFeatureRequests_.contains(QStringLiteral("connections")) && !busy_) {
        const auto ticket = beginFeatureRequest(QStringLiteral("connections"));
        engine_.api("GET", "/connections", {}, [this, ticket](domain::Result<QJsonObject> result) {
            if (!acceptFeatureRequest(ticket))
                return;
            if (const auto* value = std::get_if<QJsonObject>(&result)) {
                auto next = value->value(QStringLiteral("connections")).toArray();
                const auto now = std::chrono::steady_clock::now().time_since_epoch();
                const auto milliseconds =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                const auto elapsed = milliseconds - connectionSampleMs_;
                QHash<QString, QJsonObject> previous;
                for (const auto& entry : connections_)
                    previous.insert(entry.toObject().value(QStringLiteral("id")).toString(),
                                    entry.toObject());
                for (qsizetype i = 0; i < next.size(); ++i) {
                    auto entry = next[i].toObject();
                    const auto old = previous.value(entry.value(QStringLiteral("id")).toString());
                    if (!old.isEmpty() && elapsed >= 100 && elapsed <= 10000) {
                        for (const auto* field : {"upload", "download"}) {
                            const auto delta = entry.value(QLatin1String(field)).toDouble() -
                                               old.value(QLatin1String(field)).toDouble();
                            if (delta >= 0)
                                entry.insert(QString::fromLatin1(field) + QStringLiteral("Rate"),
                                             delta * 1000 / elapsed);
                        }
                    }
                    entry.insert(QStringLiteral("epoch"), epoch_);
                    next[i] = entry;
                }
                connectionSampleMs_ = milliseconds;
                connections_ = next;
                featureState_.insert(QStringLiteral("connectionSample"),
                                     QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
                featureState_.insert(QStringLiteral("connectionsStale"), false);
            } else {
                featureState_.insert(QStringLiteral("connectionsStale"), true);
            }
            publish();
        });
    }
    publish();
}
void DesktopRuntime::featureCommand(const QString& command, const QStringList& args) {
    if (command == QStringLiteral("update_geo")) {
        updateGeo();
        return;
    }
    if (command == QStringLiteral("rollback_geo")) {
        if (busy_ || store_.readOnly() || selectedId_.isEmpty() ||
            runtimePreferences_.value(QStringLiteral("tunDesired")).toBool() ||
            args.value(0) != QStringLiteral("confirmed"))
            return;
        previousPreferences_ = runtimePreferences_;
        const auto current = runtimePreferences_.value(QStringLiteral("geoSnapshot"));
        runtimePreferences_.insert(
            QStringLiteral("geoSnapshot"),
            runtimePreferences_.value(QStringLiteral("previousGeoSnapshot")));
        runtimePreferences_.insert(QStringLiteral("previousGeoSnapshot"), current);
        applyProfile(selectedId_, engine_.ready());
        return;
    }
    if (command == QStringLiteral("rollback_profile")) {
        if (busy_ || store_.readOnly() || args.value(1) != QStringLiteral("confirmed"))
            return;
        const auto current = profile(args.value(0));
        auto history = current.value(QStringLiteral("history")).toArray();
        if (history.isEmpty())
            return;
        auto restored = history.first().toObject();
        history.removeFirst();
        const auto reference = restored
                                   .value(restored.contains(QStringLiteral("snapshotRef"))
                                              ? QStringLiteral("snapshotRef")
                                              : QStringLiteral("sourceRef"))
                                   .toString();
        const auto bytes = store_.readBytes(reference);
        if (!std::holds_alternative<QByteArray>(bytes) ||
            QString::fromLatin1(
                QCryptographicHash::hash(std::get<QByteArray>(bytes), QCryptographicHash::Sha256)
                    .toHex()) != restored
                                     .value(restored.contains(QStringLiteral("snapshotRef"))
                                                ? QStringLiteral("snapshotHash")
                                                : QStringLiteral("sourceHash"))
                                     .toString()) {
            error(QStringLiteral("SourceIntegrityFailed"));
            return;
        }
        auto version = current;
        version.remove(QStringLiteral("history"));
        history.append(version);
        restored.insert(QStringLiteral("history"), history);
        restored.insert(QStringLiteral("pendingApply"), true);
        auto catalog = profiles_;
        for (qsizetype index = 0; index < catalog.size(); ++index)
            if (catalog[index].toObject().value(QStringLiteral("id")) == args.value(0))
                catalog[index] = restored;
        if (saveCatalog(catalog)) {
            if (selectedId_ == args.value(0))
                applyProfile(selectedId_, engine_.ready());
            else
                publish();
        }
        return;
    }
    if (command == QStringLiteral("test_batch")) {
        if (testReply_ || !testQueue_.isEmpty() || args.size() < 2 || args.size() > 9) {
            error(QStringLiteral("WebsiteTestBusy"));
            return;
        }
        for (const auto& target : args.mid(1)) {
            const QUrl url(target);
            if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty() ||
                !url.userInfo().isEmpty() || url.hasFragment() || target.size() > 8192) {
                error(QStringLiteral("ReadyProxyAndHttpsTargetRequired"));
                return;
            }
        }
        testQueue_ = args.mid(1);
        testPath_ = args.first();
        testWebsite(testQueue_.takeFirst(), testPath_);
        return;
    }
    if (command == QStringLiteral("tun")) {
        setTun(args.value(0) == QStringLiteral("true"));
        return;
    }
    if (command == QStringLiteral("close_filtered_connections")) {
        if (!engine_.ready() || busy_ || closingConnections_ || args.value(0) != epoch_ ||
            args.size() > 502 || args.value(1) != QStringLiteral("confirmed"))
            return;
        QJsonArray targets;
        for (const auto& value : connections_)
            if (args.mid(2).contains(value.toObject().value(QStringLiteral("id")).toString()))
                targets.append(QJsonObject{
                    {QStringLiteral("id"), value.toObject().value(QStringLiteral("id"))}});
        featureState_.insert(QStringLiteral("closeRemaining"), targets.size());
        featureState_.insert(QStringLiteral("closeSucceeded"), 0);
        featureState_.insert(QStringLiteral("closeFailed"), 0);
        closingConnections_ = true;
        closeNextConnection(targets, 0, epoch_);
        return;
    }
    if (command == QStringLiteral("backup") && args.value(0) == QStringLiteral("confirmed")) {
        backupProfiles();
        return;
    }
    if (command == QStringLiteral("restore_backup") &&
        args.value(1) == QStringLiteral("confirmed")) {
        restoreBackup(args.value(0));
        return;
    }
    if (command == QStringLiteral("cancel_test")) {
        testQueue_.clear();
        ++testGeneration_;
        if (testReply_) {
            const auto reply = testReply_;
            testReply_.clear();
            reply->abort();
        }
        featureState_.insert(QStringLiteral("websiteTest"),
                             QJsonObject{{QStringLiteral("state"), QStringLiteral("Cancelled")}});
        testResult_ = tr("Test cancelled");
        publish();
        return;
    }
    if (command == QStringLiteral("dns_mode")) {
        const auto mode = args.value(0);
        if (busy_ || selectedId_.isEmpty() ||
            !QStringList{QStringLiteral("profile"), QStringLiteral("fake-ip"),
                         QStringLiteral("redir-host")}
                 .contains(mode))
            return;
        previousPreferences_ = runtimePreferences_;
        runtimePreferences_.insert(QStringLiteral("dnsMode"), mode);
        applyProfile(selectedId_, engine_.ready());
        return;
    }
    if (command == QStringLiteral("clear_logs")) {
        logs_ = {};
        appLogs_ = {};
        droppedLogs_ = 0;
        publish();
        return;
    }
    if (command == QStringLiteral("diagnostics") || command == QStringLiteral("export_logs")) {
        QJsonArray safeLogs;
        const auto& sourceLogs =
            featureDemands_.contains(QStringLiteral("logs")) ? logs_ : appLogs_;
        for (const auto& value : sourceLogs) {
            auto entry = value.toObject();
            if (command != QStringLiteral("export_logs"))
                entry.remove(QStringLiteral("message"));
            safeLogs.append(entry);
        }
        const QJsonObject report{
            {QStringLiteral("application"), QStringLiteral("QuickClash")},
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("coreVersion"), QStringLiteral("v1.19.33")},
            {QStringLiteral("status"), status_},
            {QStringLiteral("error"), lastError_},
            {QStringLiteral("profileCount"), profiles_.size()},
            {QStringLiteral("platform"), snapshot().value(QStringLiteral("platform"))},
            {QStringLiteral("architecture"), snapshot().value(QStringLiteral("architecture"))},
            {QStringLiteral("memory"), featureState_.value(QStringLiteral("memory"))},
            {QStringLiteral("memoryComplete"),
             featureState_.value(QStringLiteral("memoryComplete"))},
            {QStringLiteral("logs"), safeLogs},
            {QStringLiteral("droppedLogs"), droppedLogs_}};
        const auto path = command == QStringLiteral("export_logs")
                              ? QStringLiteral("logs/redacted_logs.json")
                              : QStringLiteral("logs/diagnostics.json");
        const auto result = store_.write(path, report);
        if (const auto* failure = std::get_if<domain::Error>(&result))
            error(failure->code);
        else {
            featureState_.insert(QStringLiteral("exportUrl"),
                                 QUrl::fromLocalFile(store_.root() + '/' + path).toString());
            publish();
        }
        return;
    }
    if (!engine_.ready() || busy_) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    if (command == QStringLiteral("rules")) {
        requestPageData(QStringLiteral("rules"));
    } else if (command == QStringLiteral("providers")) {
        requestPageData(QStringLiteral("providers"));
    } else if (command == QStringLiteral("close_connection")) {
        if (args.value(0) != epoch_)
            return;
        const auto id = args.value(1);
        bool present = false;
        for (const auto& value : connections_)
            present |= value.toObject().value(QStringLiteral("id")) == id;
        if (!present)
            return;
        engine_.api("DELETE", "/connections/" + QUrl::toPercentEncoding(id), {},
                    [this](domain::Result<QJsonObject>) { pollFeatures(); });
    } else if (command == QStringLiteral("close_all_connections")) {
        if (args.value(0) != epoch_ || args.value(1) != QStringLiteral("confirmed"))
            return;
        engine_.api("DELETE", "/connections", {},
                    [this](domain::Result<QJsonObject>) { pollFeatures(); });
    } else if (command == QStringLiteral("cancel_delays")) {
        cancelDelayTests();
        publish();
    } else if (command == QStringLiteral("delay")) {
        startDelayTests({args.value(0)}, args.value(1));
    } else if (command == QStringLiteral("delay_batch")) {
        QStringList names;
        QSet<QString> visited;
        std::function<void(const QString&)> expand = [&](const QString& name) {
            if (visited.contains(name))
                return;
            visited.insert(name);
            for (const auto& value : groups_) {
                const auto group = value.toObject();
                if (group.value(QStringLiteral("name")) == name) {
                    for (const auto& member : group.value(QStringLiteral("members")).toArray())
                        expand(member.toString());
                    return;
                }
            }
            for (const auto& node : featureState_.value(QStringLiteral("nodes")).toArray())
                if (node.toObject().value(QStringLiteral("name")) == name) {
                    names.append(name);
                    return;
                }
        };
        if (args.value(0).isEmpty()) {
            for (const auto& node : featureState_.value(QStringLiteral("nodes")).toArray())
                expand(node.toObject().value(QStringLiteral("name")).toString());
        } else {
            expand(args.value(0));
        }
        startDelayTests(names, args.value(1));
    } else if (command == QStringLiteral("update_provider")) {
        const auto kind = args.value(0), name = args.value(1);
        if (kind == QStringLiteral("proxy-providers") || kind == QStringLiteral("rule-providers")) {
            const auto item = profile(selectedId_);
            if (!item.value(QStringLiteral("secretRef")).toString().isEmpty()) {
                updateProfile(selectedId_);
            } else {
                const auto bytes =
                    store_.readBytes(item.value(QStringLiteral("sourceRef")).toString());
                if (const auto* source = std::get_if<QByteArray>(&bytes))
                    resolveImport(*source, item.value(QStringLiteral("name")).toString(), {},
                                  selectedId_);
                else
                    error(QStringLiteral("SourceIntegrityFailed"));
            }
            return;
        }
        bool present = false;
        for (const auto& value : providers_) {
            const auto entry = value.toObject();
            present |= entry.value(QStringLiteral("kind")) == kind &&
                       entry.value(QStringLiteral("name")) == name;
        }
        if (!present || (kind != QStringLiteral("proxies") && kind != QStringLiteral("rules")))
            return;
        busy_ = true;
        engine_.api("PUT", "/providers/" + kind.toUtf8() + '/' + QUrl::toPercentEncoding(name), {},
                    [this](domain::Result<QJsonObject> result) {
                        busy_ = false;
                        if (const auto* failure = std::get_if<domain::Error>(&result))
                            error(failure->code);
                        featureCommand(QStringLiteral("providers"), {});
                        publish();
                    });
    } else if (command == QStringLiteral("flush_dns")) {
        engine_.api("POST", "/cache/dns/flush", {}, [this](domain::Result<QJsonObject> result) {
            featureState_.insert(QStringLiteral("dnsResult"),
                                 std::holds_alternative<QJsonObject>(result)
                                     ? QStringLiteral("Cleared")
                                     : QStringLiteral("Failed"));
            publish();
        });
    }
}
void DesktopRuntime::requestPageData(const QString& stream) {
    if (!engine_.ready() || busy_ || !featureDemands_.contains(stream))
        return;
    const auto ticket = beginFeatureRequest(stream);
    if (stream == QStringLiteral("rules")) {
        engine_.api("GET", "/rules", {}, [this, ticket](domain::Result<QJsonObject> result) {
            if (!acceptFeatureRequest(ticket))
                return;
            loadedFeatures_.insert(ticket.stream);
            if (const auto* value = std::get_if<QJsonObject>(&result))
                rules_ = value->value(QStringLiteral("rules")).toArray();
            else
                error(std::get<domain::Error>(result).code);
            publish();
        });
        return;
    }
    if (stream != QStringLiteral("providers"))
        return;
    engine_.api(
        "GET", "/providers/proxies", {}, [this, ticket](domain::Result<QJsonObject> result) {
            if (!ticket.matches(demandState_, epoch_, operationGeneration_,
                                featureRequests_.value(ticket.stream))) {
                (void)acceptFeatureRequest(ticket);
                return;
            }
            providerDraft_ =
                profile(selectedId_).value(QStringLiteral("managedProviders")).toArray();
            if (const auto* value = std::get_if<QJsonObject>(&result)) {
                const auto objects = value->value(QStringLiteral("providers")).toObject();
                for (auto it = objects.begin(); it != objects.end(); ++it) {
                    const auto provider = it.value().toObject();
                    providerDraft_.append(
                        QJsonObject{{QStringLiteral("name"), it.key()},
                                    {QStringLiteral("kind"), QStringLiteral("proxies")},
                                    {QStringLiteral("count"),
                                     provider.value(QStringLiteral("proxies")).toArray().size()},
                                    {QStringLiteral("updatedAt"),
                                     provider.value(QStringLiteral("updatedAt"))}});
                }
            }
            engine_.api(
                "GET", "/providers/rules", {}, [this, ticket](domain::Result<QJsonObject> result) {
                    if (!acceptFeatureRequest(ticket))
                        return;
                    loadedFeatures_.insert(ticket.stream);
                    if (const auto* value = std::get_if<QJsonObject>(&result)) {
                        const auto objects = value->value(QStringLiteral("providers")).toObject();
                        for (auto it = objects.begin(); it != objects.end(); ++it) {
                            const auto provider = it.value().toObject();
                            providerDraft_.append(
                                QJsonObject{{QStringLiteral("name"), it.key()},
                                            {QStringLiteral("kind"), QStringLiteral("rules")},
                                            {QStringLiteral("count"),
                                             provider.value(QStringLiteral("ruleCount"))},
                                            {QStringLiteral("updatedAt"),
                                             provider.value(QStringLiteral("updatedAt"))}});
                        }
                    }
                    providers_ = std::move(providerDraft_);
                    providerDraft_ = {};
                    publish();
                });
        });
}
void DesktopRuntime::startDelayTests(const QStringList& names, const QString& targetText) {
    const QUrl target(targetText);
    if (delayRunning_ || busy_ || !engine_.ready() || names.isEmpty() || names.size() > 500 ||
        !target.isValid() || target.scheme() != QStringLiteral("https") ||
        target.host().isEmpty() || !target.userInfo().isEmpty() || target.hasFragment() ||
        targetText.size() > 8192)
        return;
    for (const auto& name : names)
        if (!identities_.contains(name))
            return;
    ++delayGeneration_;
    delayTarget_ = targetText;
    delayQueue_ = names;
    delayQueue_.removeDuplicates();
    activeDelays_.clear();
    delayRunning_ = true;
    featureState_.insert(QStringLiteral("delayTest"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("Running")},
                                     {QStringLiteral("total"), delayQueue_.size()},
                                     {QStringLiteral("completed"), 0},
                                     {QStringLiteral("failed"), 0}});
    pumpDelayTests();
}
void DesktopRuntime::pumpDelayTests() {
    const auto generation = delayGeneration_;
    const auto epoch = epoch_;
    // Two in flight keeps large subscriptions responsive and bounds core/IPC demand.
    while (delayRunning_ && activeDelays_.size() < 2 && !delayQueue_.isEmpty()) {
        const auto name = delayQueue_.takeFirst();
        activeDelays_.insert(name);
        delays_.insert(name, QStringLiteral("Testing"));
        engine_.api(
            "GET",
            "/proxies/" + QUrl::toPercentEncoding(name) +
                "/delay?timeout=2500&url=" + QUrl::toPercentEncoding(delayTarget_),
            {}, [this, name, generation, epoch](domain::Result<QJsonObject> result) {
                if (generation != delayGeneration_ || epoch != epoch_)
                    return;
                activeDelays_.remove(name);
                const auto* value = std::get_if<QJsonObject>(&result);
                const bool valid = value && value->value(QStringLiteral("delay")).isDouble() &&
                                   value->value(QStringLiteral("delay")).toInt(-1) >= 0;
                delays_.insert(
                    name, valid ? QString::number(value->value(QStringLiteral("delay")).toInt()) +
                                      QStringLiteral(" ms")
                                : QStringLiteral("Failed"));
                auto progress = featureState_.value(QStringLiteral("delayTest")).toObject();
                progress.insert(QStringLiteral("completed"),
                                progress.value(QStringLiteral("completed")).toInt() + 1);
                if (!valid)
                    progress.insert(QStringLiteral("failed"),
                                    progress.value(QStringLiteral("failed")).toInt() + 1);
                featureState_.insert(QStringLiteral("delayTest"), progress);
                pumpDelayTests();
            });
    }
    if (delayQueue_.isEmpty() && activeDelays_.isEmpty()) {
        delayRunning_ = false;
        auto progress = featureState_.value(QStringLiteral("delayTest")).toObject();
        progress.insert(QStringLiteral("state"), QStringLiteral("Completed"));
        featureState_.insert(QStringLiteral("delayTest"), progress);
    }
    publish();
}
void DesktopRuntime::cancelDelayTests() {
    ++delayGeneration_;
    delayRunning_ = false;
    for (const auto& name : activeDelays_)
        delays_.insert(name, QStringLiteral("Cancelled"));
    activeDelays_.clear();
    delayQueue_.clear();
    auto progress = featureState_.value(QStringLiteral("delayTest")).toObject();
    if (!progress.isEmpty()) {
        progress.insert(QStringLiteral("state"), QStringLiteral("Cancelled"));
        featureState_.insert(QStringLiteral("delayTest"), progress);
    }
}
void DesktopRuntime::closeNextConnection(const QJsonArray& targets, qsizetype index,
                                         const QString& epoch) {
    if (epoch != epoch_ || !engine_.ready()) {
        closingConnections_ = false;
        return;
    }
    if (index == targets.size()) {
        closingConnections_ = false;
        pollFeatures();
        return;
    }
    const auto id = targets[index].toObject().value(QStringLiteral("id")).toString();
    engine_.api("DELETE", "/connections/" + QUrl::toPercentEncoding(id), {},
                [this, targets, index, epoch](domain::Result<QJsonObject> result) {
                    if (epoch != epoch_)
                        return;
                    const auto counter = std::holds_alternative<QJsonObject>(result)
                                             ? QStringLiteral("closeSucceeded")
                                             : QStringLiteral("closeFailed");
                    featureState_.insert(counter, featureState_.value(counter).toInt() + 1);
                    featureState_.insert(QStringLiteral("closeRemaining"),
                                         targets.size() - index - 1);
                    publish();
                    closeNextConnection(targets, index + 1, epoch);
                });
}
} // namespace quickclash::application
