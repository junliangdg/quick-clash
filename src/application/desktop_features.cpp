#include "application/desktop_runtime.h"
#include "domain/connection_snapshot.h"
#include "infra/core/delay_policy.h"
#include "infra/core/listener_ports.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <chrono>
#include <utility>

namespace quickclash::application {
namespace {
constexpr int kDelayConcurrencyService = 32;
// QNetworkAccessManager opens at most six HTTP/1.1 connections per host. Keeping the
// direct-mode batch at that ceiling avoids a long local queue behind the control API.
constexpr int kDelayConcurrencyDirect = 6;
} // namespace
void DesktopRuntime::loadOfflineGroups() {
    if (engine_.ready())
        return;
    proxyLatency_.clear();
    groupDelayTargets_.clear();
    syncDelayViews();
    groups_ = {};
    configuredGroups_ = {};
    defaultPolicy_.clear();
    featureState_.remove(QStringLiteral("groupsStale"));
    featureState_.remove(QStringLiteral("groupsError"));
    const auto selection = repository_.manifest().value(QStringLiteral("selection")).toObject();
    if (selection.isEmpty())
        return;
    const auto bytes = store_.readBytes(selection.value(QStringLiteral("artifactRef")).toString());
    if (!std::holds_alternative<QByteArray>(bytes))
        return;
    const auto keyId = QStringLiteral("c137006a-58f4-4d6c-a795-b987e368c820");
    auto key = credentials_->read(keyId);
    const auto keyPath = store_.path(QStringLiteral("data/credentials/%1.blob").arg(keyId));
    if (!std::holds_alternative<QByteArray>(key) && std::holds_alternative<QString>(keyPath) &&
        !QFileInfo::exists(std::get<QString>(keyPath))) {
        const auto generated =
            QUuid::createUuid().toByteArray() + QUuid::createUuid().toByteArray();
        if (!std::holds_alternative<std::monostate>(credentials_->write(keyId, generated)))
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
        if (group.value(QStringLiteral("canSelect")).toBool() &&
            saved.value(QStringLiteral("profileId")) == selectedId_ &&
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
            !previous.active.contains(stream)) {
            for (const auto& entry : appLogs_.entries())
                logs_.append(entry.toObject());
        }
        if (stream == QStringLiteral("log_feed") && previous.active.contains(stream) &&
            !featureDemands_.contains(stream) && featureDemands_.contains(QStringLiteral("logs")))
            logs_.markGap();
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
        featureState_.remove(QStringLiteral("connectionsPaused"));
        featureState_.remove(QStringLiteral("connectionError"));
    } else if (stream == QStringLiteral("rules")) {
        rules_ = {};
        rulePageState_ = {};
    } else if (stream == QStringLiteral("providers")) {
        providers_ = {};
        providerDraft_ = {};
        providerPageState_ = {};
        providerResults_ = {};
    } else if (stream == QStringLiteral("logs")) {
        engine_.setStream(QStringLiteral("logs"), false);
        logs_.clear();
        logPublicationTimer_.stop();
        clearLogExport();
    } else if (stream == QStringLiteral("log_feed")) {
        engine_.setStream(QStringLiteral("logs"), false);
    } else if (stream == QStringLiteral("traffic")) {
        engine_.setStream(QStringLiteral("traffic"), false);
        featureState_.remove(QStringLiteral("traffic"));
    } else if (stream == QStringLiteral("resources")) {
        resourceSampleClock_.invalidate();
        for (const auto* key : {"memory", "memorySample", "memoryComplete"})
            featureState_.remove(QLatin1String(key));
    } else if (stream == QStringLiteral("custom_groups")) {
        customGroupLiveProviders_ = {};
        customGroupState_ = {};
        customGroupDraft_ = {};
        if (configTransaction_ && configTransaction_->previewOnly &&
            !configTransaction_->customGroupToken.isEmpty()) {
            discardConfigTransaction();
            // The validator is exclusively owned by this cancelled preview. Stop it before
            // releasing busy, so a new operation cannot reuse its callback or staging files.
            validationDeadline_.stop();
            validationCompletion_ = {};
            validator_->stop();
            staging_.reset();
            busy_ = false;
        }
    } else if (stream == QStringLiteral("proxies")) {
        cancelDelayTests();
    } else if (stream == QStringLiteral("editor")) {
        editor_ = {};
    } else if (stream == QStringLiteral("user_rules")) {
        userRuleState_ = {};
        userRuleTargets_ = {};
        userRuleContextRevision_.clear();
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
    if (!accepted) {
        loadedFeatures_.remove(request.stream);
        if (request.stream == QStringLiteral("rules"))
            rulePageState_ = {};
        else if (request.stream == QStringLiteral("providers"))
            providerPageState_ = {};
    }
    return accepted;
}
void DesktopRuntime::appendLog(const QString& source, const QString& level, const QString& message,
                               bool truncated) {
    auto entry = domain::redactedLogEntry(++logSequence_, clock_().toString(Qt::ISODate), source,
                                          level, message);
    if (truncated)
        entry.insert(QStringLiteral("truncated"), true);
    // Small App diagnostics survive page release; Core history is strictly page-owned.
    if (source == QStringLiteral("App"))
        appLogs_.append(entry);
    if (!featureDemands_.contains(QStringLiteral("logs")) ||
        !featureDemands_.contains(QStringLiteral("log_feed")))
        return;
    logs_.append(entry);
    if (!logPublicationTimer_.isActive())
        logPublicationTimer_.start();
}
void DesktopRuntime::syncDelayViews() {
    const auto now = clock_();
    delayViewsAt_ = now;
    delayDetails_ = proxyLatency_.details(now);
    delayExpiry_ = {};
    delays_ = {};
    delayTimes_ = {};
    for (auto it = delayDetails_.begin(); it != delayDetails_.end(); ++it) {
        auto records = it.value().toObject().value("records").toArray();
        const auto latest = it.value().toObject().value("latest").toObject();
        records.append(latest);
        for (const auto& value : records) {
            const auto record = value.toObject();
            const auto expiry =
                QDateTime::fromString(record.value("time").toString(), Qt::ISODateWithMs)
                    .addSecs(600);
            if (record.value("state") != "Queued" && record.value("state") != "Testing" &&
                expiry > now && (!delayExpiry_.isValid() || expiry < delayExpiry_))
                delayExpiry_ = expiry;
        }
        delays_.insert(it.key(), domain::ProxyLatency::text(latest));
        if (latest.value("state") != "Stale" && latest.value("state") != "Queued" &&
            latest.value("state") != "Testing")
            delayTimes_.insert(
                it.key(), QDateTime::fromString(latest.value("time").toString(), Qt::ISODateWithMs)
                              .toString(Qt::ISODate));
    }
}
void DesktopRuntime::pollFeatures() {
    if (delayExpiry_.isValid() && clock_() >= delayExpiry_)
        syncDelayViews();
    if (featureDemands_.contains(QStringLiteral("resources")) &&
        (!resourceSampleClock_.isValid() || resourceSampleClock_.elapsed() >= 5000)) {
        resourceSampleClock_.start();
        sampleResources();
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
        !pendingFeatureRequests_.contains(QStringLiteral("connections")) && !busy_ &&
        !featureState_.value(QStringLiteral("connectionsPaused")).toBool()) {
        const auto ticket = beginFeatureRequest(QStringLiteral("connections"));
        engine_.api("GET", "/connections", {}, [this, ticket](domain::Result<QJsonObject> result) {
            if (!acceptFeatureRequest(ticket))
                return;
            if (const auto* value = std::get_if<QJsonObject>(&result)) {
                const auto now = std::chrono::steady_clock::now().time_since_epoch();
                const auto milliseconds =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                const auto parsed = domain::connectionSnapshot(*value, connections_, epoch_,
                                                               milliseconds - connectionSampleMs_);
                if (const auto* next = std::get_if<QJsonArray>(&parsed)) {
                    connectionSampleMs_ = milliseconds;
                    connections_ = *next;
                    featureState_.insert(QStringLiteral("connectionSample"),
                                         QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
                    featureState_.insert(QStringLiteral("connectionsStale"), false);
                    featureState_.remove(QStringLiteral("connectionError"));
                } else {
                    result = std::get<domain::Error>(parsed);
                }
            }
            if (const auto* failure = std::get_if<domain::Error>(&result)) {
                featureState_.insert(QStringLiteral("connectionsStale"), true);
                featureState_.insert(QStringLiteral("connectionError"), failure->code);
                if (failure->code == QStringLiteral("CoreResponseTooLarge") ||
                    failure->code == QStringLiteral("CoreResponseInvalid"))
                    featureState_.insert(QStringLiteral("connectionsPaused"), true);
            }
            publish();
        });
    }
    publish();
}
void DesktopRuntime::featureCommand(const QString& command, const QStringList& args) {
    if (command.startsWith(QStringLiteral("user_rules_"))) {
        userRuleCommand(command, args);
        return;
    }
    if (command.startsWith(QStringLiteral("custom_group_"))) {
        customGroupCommand(command, args);
        return;
    }
    if (command.startsWith(QStringLiteral("core_"))) {
        coreUpdateCommand(command, args);
        return;
    }
    if (command == QStringLiteral("startup_enabled") ||
        command == QStringLiteral("startup_hidden") ||
        command == QStringLiteral("startup_refresh")) {
        configureStartup(command, args);
        return;
    }
    if (command == QStringLiteral("query_ip")) {
        queryIp(args.value(0));
        return;
    }
    if (command == QStringLiteral("cancel_ip")) {
        cancelIpQueries(false);
        publish();
        return;
    }
    if (command == QStringLiteral("refresh_groups")) {
        if (!busy_ && engine_.ready())
            engine_.refreshGroups();
        return;
    }
    if (command == QStringLiteral("cancel_listener_ports")) {
        if (featureState_.contains(QStringLiteral("listenerPortCandidate"))) {
            featureState_.remove(QStringLiteral("listenerPortCandidate"));
            publish();
        }
        return;
    }
    if (command == QStringLiteral("suggest_listener_ports")) {
        if (busy_ || store_.readOnly())
            return;
        const auto effectiveMixed =
            engine_.ready() ? effectivePreferences_.value("mixedPort").toInt() : 0;
        const auto effectiveController = engine_.ready() && !engine_.usingService()
                                             ? effectivePreferences_.value("controllerPort").toInt()
                                             : 0;
        auto candidate = infra::suggestListenerPorts(
            preferences().mixedPort, preferences().controllerPort,
            static_cast<quint16>(effectiveMixed), static_cast<quint16>(effectiveController));
        if (candidate.isEmpty()) {
            error(QStringLiteral("NoAvailableListenerPorts"));
            return;
        }
        candidate.insert(QStringLiteral("token"),
                         QUuid::createUuid().toString(QUuid::WithoutBraces));
        featureState_.insert(QStringLiteral("listenerPortCandidate"), candidate);
        publish();
        return;
    }
    if (command == QStringLiteral("diagnostics") || command == QStringLiteral("storage_preview") ||
        command == QStringLiteral("repair_preview") ||
        command == QStringLiteral("confirm_maintenance") ||
        command == QStringLiteral("cancel_maintenance")) {
        maintenanceCommand(command, args);
        return;
    }
    if (command.startsWith(QStringLiteral("editor_"))) {
        editorCommand(command, args);
        return;
    }
    if (command == QStringLiteral("update_geo")) {
        geoScheduled_ = false;
        updateGeo();
        return;
    }
    if (command == QStringLiteral("geo_settings") ||
        command == QStringLiteral("apply_pending_geo")) {
        configureGeo(command, args);
        return;
    }
    if (command == QStringLiteral("rollback_geo")) {
        if (busy_ || store_.readOnly() || selectedId_.isEmpty() ||
            !runtimePreferences_.contains(QStringLiteral("previousGeoSnapshot")) ||
            args.value(0) != QStringLiteral("confirmed"))
            return;
        applyGeoSnapshot(
            runtimePreferences_.value(QStringLiteral("previousGeoSnapshot")).toObject());
        return;
    }
    if (command == QStringLiteral("cancel_geo")) {
        cancelGeo();
        return;
    }
    if (command == QStringLiteral("bundled_geo")) {
        if (busy_ || store_.readOnly() || selectedId_.isEmpty() ||
            args.value(0) != QStringLiteral("confirmed"))
            return;
        applyGeoSnapshot({});
        return;
    }
    if (command == QStringLiteral("rollback_profile")) {
        if (busy_ || store_.readOnly() || args.value(1) != QStringLiteral("confirmed"))
            return;
        bool valid = true;
        const auto index = args.size() > 2 ? args.value(2).toInt(&valid) : 0;
        if (valid)
            restoreProfileVersion(args.value(0), index, args.value(3));
        return;
    }
    if (command == QStringLiteral("test_batch")) {
        if (busy_ || testReply_ || !testQueue_.isEmpty() || args.size() < 2 || args.size() > 9) {
            error(QStringLiteral("WebsiteTestBusy"));
            return;
        }
        if (!QStringList{QStringLiteral("CurrentProxy"), QStringLiteral("Direct"),
                         QStringLiteral("SystemDefault")}
                 .contains(args.first()) ||
            (args.first() == QStringLiteral("CurrentProxy") && !engine_.ready())) {
            error(QStringLiteral("ReadyProxyAndHttpsTargetRequired"));
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
    if (command == QStringLiteral("resume_connections")) {
        featureState_.remove(QStringLiteral("connectionsPaused"));
        featureState_.remove(QStringLiteral("connectionError"));
        pollFeatures();
        return;
    }
    if (command == QStringLiteral("close_filtered_connections")) {
        if (!engine_.ready() || busy_ || closingConnections_ || args.value(0) != epoch_ ||
            args.size() > 502 || args.value(1) != QStringLiteral("confirmed"))
            return;
        QJsonArray targets;
        QSet<QString> seen;
        for (const auto& id : args.mid(2)) {
            if (id.isEmpty() || id.size() > 256 || seen.contains(id))
                continue;
            seen.insert(id);
            targets.append(QJsonObject{{QStringLiteral("id"), id}});
        }
        featureState_.insert(QStringLiteral("closeRemaining"), targets.size());
        featureState_.insert(QStringLiteral("closeSucceeded"), 0);
        featureState_.insert(QStringLiteral("closeFailed"), 0);
        featureState_.insert(QStringLiteral("closeEnded"), 0);
        featureState_.insert(QStringLiteral("closeUnknown"), 0);
        featureState_.remove(QStringLiteral("closeAllResult"));
        featureState_.insert(QStringLiteral("closeRunning"), true);
        closingConnections_ = true;
        closeNextConnection(targets, 0, epoch_);
        return;
    }
    if (command == QStringLiteral("backup") && args.value(0) == QStringLiteral("confirmed")) {
        backupProfiles(args.value(1) == QStringLiteral("source"));
        return;
    }
    if (command == QStringLiteral("restore_backup") &&
        args.value(1) == QStringLiteral("confirmed")) {
        restoreBackup(args.value(0), args.value(2) == QStringLiteral("global_rules"));
        return;
    }
    if (command == QStringLiteral("preview_backup")) {
        previewBackup(args.value(0));
        return;
    }
    if (command == QStringLiteral("cancel_backup_restore")) {
        cancelBackupRestore();
        publish();
        return;
    }
    if (command == QStringLiteral("discard_backup_preview")) {
        if (backupRestore_ && !backupRestore_->active) {
            cancelBackupRestore();
            publish();
        }
        return;
    }
    if (command == QStringLiteral("clear_preview")) {
        if (!effectivePreview_.isEmpty()) {
            effectivePreview_.clear();
            publish();
        }
        return;
    }
    if (command == QStringLiteral("cancel_test")) {
        if (!testReply_ && testQueue_.isEmpty() &&
            featureState_.value(QStringLiteral("websiteTest"))
                    .toObject()
                    .value(QStringLiteral("state")) != QStringLiteral("Running"))
            return;
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
    if (command == QStringLiteral("delay_target")) {
        const auto receipt = [this, &args](const QString& state) {
            if (args.size() == 2 && args[1].size() <= 64)
                featureState_.insert(QStringLiteral("settingsTargetRequest"),
                                     QJsonObject{{"id", args[1]}, {"state", state}});
        };
        const auto targetText = args.value(0).trimmed();
        const QUrl target(targetText);
        if (store_.readOnly() || delayRunning_ || !target.isValid() ||
            target.scheme() != QStringLiteral("https") || target.host().isEmpty() ||
            !target.userInfo().isEmpty() || target.hasFragment() || targetText.size() > 8192) {
            receipt(QStringLiteral("Failed"));
            error(QStringLiteral("InvalidDelayTarget"));
            return;
        }
        const auto saved = credentials_->write(
            QStringLiteral("6c7a5e7f-56c4-4f6e-a2c1-15c0a2f2ef31"), targetText.toUtf8());
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            receipt(QStringLiteral("Failed"));
            error(failure->code);
            return;
        }
        proxyLatency_.invalidateManual(delayTarget_);
        delayTarget_ = targetText;
        syncDelayViews();
        featureState_.remove(QStringLiteral("delayTest"));
        receipt(QStringLiteral("Saved"));
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
    if (command == QStringLiteral("network_settings")) {
        const auto receipt = [this, &args](const QString& state) {
            if (args.size() == 7 && args[6].size() <= 64)
                featureState_.insert(QStringLiteral("settingsNetworkRequest"),
                                     QJsonObject{{"id", args[6]}, {"state", state}});
        };
        if (busy_ || store_.readOnly() || (args.size() != 6 && args.size() != 7) ||
            args[4] != epoch_ || args[5] != QString::number(selectionGeneration())) {
            receipt(QStringLiteral("Rejected"));
            error(QStringLiteral("OperationUnavailable"));
            return;
        }
        bool mixedValid = false, controllerValid = false;
        const auto mixed = args[1].toInt(&mixedValid);
        const auto controller = args[2].toInt(&controllerValid);
        if (!mixedValid || !controllerValid || mixed < 1024 || mixed > 65535 || controller < 1024 ||
            controller > 65535 || mixed == controller ||
            !QStringList{QStringLiteral("rule"), QStringLiteral("global"), QStringLiteral("direct")}
                 .contains(args[0]) ||
            !QStringList{QStringLiteral("profile"), QStringLiteral("fake-ip"),
                         QStringLiteral("redir-host")}
                 .contains(args[3])) {
            receipt(QStringLiteral("Rejected"));
            error(QStringLiteral("InvalidNetworkSettings"));
            return;
        }
        receipt(QStringLiteral("Accepted"));
        auto candidate = runtimePreferences_;
        candidate.insert(QStringLiteral("mode"), args[0]);
        candidate.insert(QStringLiteral("mixedPort"), mixed);
        candidate.insert(QStringLiteral("controllerPort"), controller);
        candidate.insert(QStringLiteral("dnsMode"), args[3]);
        featureState_.remove(QStringLiteral("listenerPortCandidate"));
        if (candidate == runtimePreferences_) {
            publish();
            return;
        }
        listenerRecoveryBlocked_ = false;
        listenerIssue_ = {};
        previousPreferences_ = runtimePreferences_;
        runtimePreferences_ = candidate;
        if (!selectedId_.isEmpty()) {
            applyProfile(selectedId_, engine_.ready());
        } else {
            const auto result =
                store_.write(QStringLiteral("config/runtime_defaults.json"), runtimePreferences_);
            if (const auto* failure = std::get_if<domain::Error>(&result)) {
                restorePreferences();
                error(failure->code);
            } else {
                previousPreferences_ = {};
                savedPreferences_ = runtimePreferences_;
            }
            publish();
        }
        return;
    }
    if (command == QStringLiteral("clear_logs")) {
        logs_.clear();
        appLogs_.clear();
        clearLogExport();
        publish();
        return;
    }
    if (command == QStringLiteral("preview_logs")) {
        prepareLogExport(args.value(0) != QStringLiteral("include_messages"), args.value(1));
        return;
    }
    if (command == QStringLiteral("export_logs")) {
        exportLogs(args.value(0));
        return;
    }
    if (command == QStringLiteral("cancel_log_export")) {
        clearLogExport();
        publish();
        return;
    }
    if (command == QStringLiteral("provider_mode")) {
        const auto item = profile(args.value(0));
        const auto mode = args.value(1);
        if (busy_ || item.isEmpty() ||
            (mode != QStringLiteral("snapshot") && mode != QStringLiteral("runtime"))) {
            error(QStringLiteral("OperationUnavailable"));
            return;
        }
        const auto source = store_.readBytes(item.value(QStringLiteral("sourceRef")).toString());
        if (!std::holds_alternative<QByteArray>(source) ||
            std::holds_alternative<domain::Error>(
                store_.verifyBytes(item.value(QStringLiteral("sourceRef")).toString(),
                                   item.value(QStringLiteral("sourceHash")).toString()))) {
            error(QStringLiteral("SourceIntegrityFailed"));
            return;
        }
        resolveImport(std::get<QByteArray>(source), item.value(QStringLiteral("name")).toString(),
                      item.value(QStringLiteral("secretRef")).toString(), args.value(0),
                      {{QStringLiteral("providerMode"), mode}});
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
        if (args.value(0) != epoch_ || closingConnections_)
            return;
        const auto id = args.value(1);
        bool present = false;
        for (const auto& value : connections_)
            present |= value.toObject().value(QStringLiteral("id")) == id;
        if (!present)
            return;
        featureCommand(QStringLiteral("close_filtered_connections"),
                       {epoch_, QStringLiteral("confirmed"), id});
    } else if (command == QStringLiteral("close_all_connections")) {
        if (args.value(0) != epoch_ || args.value(1) != QStringLiteral("confirmed") ||
            closingConnections_)
            return;
        closingConnections_ = true;
        featureState_.insert(QStringLiteral("closeRunning"), true);
        featureState_.insert(QStringLiteral("closeAllResult"), QStringLiteral("Running"));
        publish();
        const auto epoch = epoch_;
        engine_.api(
            "DELETE", "/connections", {}, [this, epoch](domain::Result<QJsonObject> result) {
                if (epoch != epoch_)
                    return;
                closingConnections_ = false;
                featureState_.insert(QStringLiteral("closeRunning"), false);
                const auto* failure = std::get_if<domain::Error>(&result);
                featureState_.insert(QStringLiteral("closeAllResult"),
                                     !failure ? QStringLiteral("Completed")
                                     : failure->code == QStringLiteral("CoreRequestTimeout")
                                         ? QStringLiteral("Unknown")
                                         : QStringLiteral("Failed"));
                if (failure && (failure->code == QStringLiteral("CoreRequestTimeout") ||
                                failure->code == QStringLiteral("ServiceTimeout"))) {
                    featureState_.insert(QStringLiteral("closeAllResult"),
                                         QStringLiteral("Unknown"));
                    // A lost reply does not authorize sending DELETE again. Read once
                    // even if the page has released or paused its polling demand.
                    engine_.api(
                        "GET", "/connections", {},
                        [this, epoch](domain::Result<QJsonObject> checked) {
                            if (epoch == epoch_) {
                                if (const auto* value = std::get_if<QJsonObject>(&checked)) {
                                    const auto parsed =
                                        domain::connectionSnapshot(*value, {}, epoch, 0);
                                    if (const auto* entries = std::get_if<QJsonArray>(&parsed))
                                        featureState_.insert(
                                            QStringLiteral("closeAllObservedRemaining"),
                                            entries->size());
                                }
                                pollFeatures();
                                publish();
                            }
                        });
                }
                pollFeatures();
                publish();
            });
    } else if (command == QStringLiteral("cancel_delays")) {
        cancelDelayTests();
        publish();
    } else if (command == QStringLiteral("delay")) {
        if (const auto probe = delayProbe(args.value(0), args.value(1)))
            startDelayTests({*probe});
    } else if (command == QStringLiteral("delay_batch")) {
        QList<DelayProbe> probes;
        QSet<QString> visited;
        bool valid = true;
        std::function<void(const QString&, const QString&, int)> expand =
            [&](const QString& name, const QString& inheritedTarget, int depth) {
                if (depth > 32 || probes.size() > 500 || visited.size() > 16000) {
                    valid = false;
                    return;
                }
                auto target = inheritedTarget;
                for (const auto& value : groups_) {
                    const auto group = value.toObject();
                    if (group.value(QStringLiteral("name")) == name) {
                        if (!group.value("testTargetHash").toString().isEmpty()) {
                            target = groupDelayTargets_.value(name);
                            if (target.isEmpty()) {
                                valid = false;
                                return;
                            }
                        }
                        const auto key = DelayProbe{name, target}.key();
                        if (visited.contains(key))
                            return;
                        visited.insert(key);
                        for (const auto& member : group.value(QStringLiteral("members")).toArray())
                            expand(member.toString(), target, depth + 1);
                        return;
                    }
                }
                if (name == "DIRECT" || name == "REJECT")
                    return;
                for (const auto& node : featureState_.value(QStringLiteral("nodes")).toArray())
                    if (node.toObject().value(QStringLiteral("name")) == name) {
                        const DelayProbe probe{name, target};
                        if (!visited.contains(probe.key())) {
                            visited.insert(probe.key());
                            probes.append(probe);
                        }
                        return;
                    }
            };
        if (args.value(0).isEmpty()) {
            for (const auto& node : featureState_.value(QStringLiteral("nodes")).toArray())
                expand(node.toObject().value(QStringLiteral("name")).toString(), delayTarget_, 0);
        } else {
            expand(args.value(0), delayTarget_, 0);
        }
        if (valid)
            startDelayTests(probes);
    } else if (command == QStringLiteral("update_provider")) {
        updateProvider(args.value(0), args.value(1));
    } else if (command == QStringLiteral("flush_dns")) {
        if (featureState_.value(QStringLiteral("dnsResult")) == QStringLiteral("Clearing"))
            return;
        featureState_.insert(QStringLiteral("dnsResult"), QStringLiteral("Clearing"));
        publish();
        engine_.api("POST", "/cache/dns/flush", {},
                    [this, epoch = epoch_,
                     generation = operationGeneration_](domain::Result<QJsonObject> result) {
                        if (epoch != epoch_ || generation != operationGeneration_)
                            return;
                        featureState_.insert(QStringLiteral("dnsResult"),
                                             std::holds_alternative<QJsonObject>(result)
                                                 ? QStringLiteral("Cleared")
                                                 : QStringLiteral("Failed"));
                        publish();
                    });
    }
}
std::optional<DesktopRuntime::DelayProbe> DesktopRuntime::delayProbe(const QString& name,
                                                                     const QString& context) const {
    auto target = delayTarget_;
    const auto findGroup = [this](const QString& groupName) {
        for (const auto& value : groups_)
            if (value.toObject().value("name") == groupName)
                return value.toObject();
        return QJsonObject{};
    };
    if (!context.isEmpty()) {
        const auto group = findGroup(context);
        if (!group.value("members").toArray().contains(name))
            return std::nullopt;
        if (!group.value("testTargetHash").toString().isEmpty()) {
            target = groupDelayTargets_.value(context);
            if (target.isEmpty())
                return std::nullopt;
        }
    }
    auto current = name;
    QSet<QString> visited;
    while (true) {
        const auto group = findGroup(current);
        if (group.isEmpty())
            break;
        if (visited.contains(current) || visited.size() >= 32)
            return std::nullopt;
        visited.insert(current);
        if (group.value("type") == "LoadBalance")
            return std::nullopt;
        if (!group.value("testTargetHash").toString().isEmpty()) {
            target = groupDelayTargets_.value(current);
            if (target.isEmpty())
                return std::nullopt;
        }
        const auto members = group.value("members").toArray();
        const auto next = group.value("current").toString();
        if (next.isEmpty() || !members.contains(next))
            return std::nullopt;
        current = next;
    }
    return DelayProbe{current, target};
}
void DesktopRuntime::startDelayTests(const QList<DelayProbe>& probes) {
    if (delayRunning_ || busy_ || !engine_.ready() || probes.isEmpty() || probes.size() > 500)
        return;
    QSet<QString> testable;
    for (const auto& node : featureState_.value(QStringLiteral("nodes")).toArray())
        testable.insert(node.toObject().value(QStringLiteral("name")).toString());
    QList<DelayProbe> queue;
    QSet<QString> keys;
    QHash<QString, int> targetsPerNode;
    QSet<QString> targets;
    for (const auto& probe : probes) {
        const QUrl target(probe.target);
        if (!identities_.contains(probe.name) || !testable.contains(probe.name) ||
            !target.isValid() || (target.scheme() != "https" && target.scheme() != "http") ||
            target.host().isEmpty() || !target.userInfo().isEmpty() || target.hasFragment() ||
            probe.target.size() > 8192)
            return;
        if (keys.contains(probe.key()))
            continue;
        keys.insert(probe.key());
        if (++targetsPerNode[probe.name] > 8)
            return;
        targets.insert(domain::ProxyLatency::targetHash(probe.target));
        queue.append(probe);
    }
    ++delayGeneration_;
    delayQueue_ = std::move(queue);
    for (const auto& probe : delayQueue_) {
        proxyLatency_.manual(probe.name, probe.target, QStringLiteral("Queued"), clock_());
    }
    activeDelays_.clear();
    delayRunning_ = true;
    featureState_.insert(
        QStringLiteral("delayTest"),
        QJsonObject{
            {QStringLiteral("state"), QStringLiteral("Running")},
            {QStringLiteral("total"), delayQueue_.size()},
            {QStringLiteral("completed"), 0},
            {QStringLiteral("failed"), 0},
            {QStringLiteral("targetHash"), targets.size() == 1 ? *targets.begin() : QString{}},
            {QStringLiteral("startedAt"), clock_().toString(Qt::ISODate)}});
    pumpDelayTests();
}
void DesktopRuntime::pumpDelayTests() {
    const auto generation = delayGeneration_;
    const auto epoch = epoch_;
    const auto concurrency =
        engine_.usingService() ? kDelayConcurrencyService : kDelayConcurrencyDirect;
    while (delayRunning_ && activeDelays_.size() < concurrency && !delayQueue_.isEmpty()) {
        const auto probe = delayQueue_.takeFirst();
        activeDelays_.insert(probe.key(), probe);
        proxyLatency_.manual(probe.name, probe.target, QStringLiteral("Testing"), clock_());
        engine_.api("GET",
                    "/proxies/" + QUrl::toPercentEncoding(probe.name) +
                        "/delay?timeout=" + QByteArray::number(infra::kNodeDelayTimeoutMs) +
                        "&url=" + QUrl::toPercentEncoding(probe.target),
                    {}, [this, probe, generation, epoch](domain::Result<QJsonObject> result) {
                        if (generation != delayGeneration_ || epoch != epoch_)
                            return;
                        activeDelays_.remove(probe.key());
                        const auto* value = std::get_if<QJsonObject>(&result);
                        const bool valid = value &&
                                           value->value(QStringLiteral("delay")).isDouble() &&
                                           value->value(QStringLiteral("delay")).toInt(-1) > 0;
                        const auto* failure = std::get_if<domain::Error>(&result);
                        const bool timeout =
                            failure && (failure->code == QStringLiteral("CoreRequestTimeout") ||
                                        failure->code == QStringLiteral("ServiceTimeout"));
                        proxyLatency_.manual(probe.name, probe.target,
                                             valid     ? QStringLiteral("Success")
                                             : timeout ? QStringLiteral("Timeout")
                                                       : QStringLiteral("Failed"),
                                             clock_(), valid ? value->value("delay").toInt() : -1);
                        auto progress = featureState_.value(QStringLiteral("delayTest")).toObject();
                        progress.insert(QStringLiteral("completed"),
                                        progress.value(QStringLiteral("completed")).toInt() + 1);
                        if (!valid)
                            progress.insert(QStringLiteral("failed"),
                                            progress.value(QStringLiteral("failed")).toInt() + 1);
                        featureState_.insert(QStringLiteral("delayTest"), progress);
                        QTimer::singleShot(0, this, [this, generation, epoch] {
                            if (generation == delayGeneration_ && epoch == epoch_)
                                pumpDelayTests();
                        });
                    });
    }
    if (delayRunning_ && delayQueue_.isEmpty() && activeDelays_.isEmpty()) {
        delayRunning_ = false;
        auto progress = featureState_.value(QStringLiteral("delayTest")).toObject();
        progress.insert(QStringLiteral("state"), QStringLiteral("Completed"));
        featureState_.insert(QStringLiteral("delayTest"), progress);
        engine_.refreshGroups();
    }
    syncDelayViews();
    publish();
}
void DesktopRuntime::cancelDelayTests() {
    ++delayGeneration_;
    if (!delayRunning_)
        return;
    delayRunning_ = false;
    for (const auto& probe : activeDelays_)
        proxyLatency_.manual(probe.name, probe.target, QStringLiteral("Cancelled"), clock_());
    for (const auto& probe : delayQueue_)
        proxyLatency_.manual(probe.name, probe.target, QStringLiteral("Cancelled"), clock_());
    activeDelays_.clear();
    delayQueue_.clear();
    syncDelayViews();
    auto progress = featureState_.value(QStringLiteral("delayTest")).toObject();
    if (!progress.isEmpty()) {
        progress.insert(QStringLiteral("state"), QStringLiteral("Cancelled"));
        featureState_.insert(QStringLiteral("delayTest"), progress);
    }
}
void DesktopRuntime::resetConnectionClose() {
    closingConnections_ = false;
    for (const auto* field :
         {"closeRunning", "closeRemaining", "closeSucceeded", "closeEnded", "closeFailed",
          "closeUnknown", "closeAllResult", "closeAllObservedRemaining"})
        featureState_.remove(QLatin1String(field));
}
void DesktopRuntime::closeNextConnection(const QJsonArray& targets, qsizetype index,
                                         const QString& epoch) {
    if (epoch != epoch_)
        return;
    if (!engine_.ready()) {
        closingConnections_ = false;
        featureState_.insert(QStringLiteral("closeRunning"), false);
        return;
    }
    if (index == targets.size()) {
        closingConnections_ = false;
        featureState_.insert(QStringLiteral("closeRunning"), false);
        pollFeatures();
        publish();
        return;
    }
    const auto id = targets[index].toObject().value(QStringLiteral("id")).toString();
    closeConnectionTarget(id, epoch, [this, targets, index, epoch](const QString& counter) {
        if (epoch != epoch_)
            return;
        featureState_.insert(counter, featureState_.value(counter).toInt() + 1);
        featureState_.insert(QStringLiteral("closeRemaining"), targets.size() - index - 1);
        publish();
        QTimer::singleShot(0, this, [this, targets, index, epoch] {
            closeNextConnection(targets, index + 1, epoch);
        });
    });
}
void DesktopRuntime::closeConnectionTarget(const QString& id, const QString& epoch,
                                           std::function<void(const QString&)> completion) {
    if (featureDemands_.contains(QStringLiteral("connections")) &&
        featureState_.contains(QStringLiteral("connectionSample")) &&
        !featureState_.value(QStringLiteral("connectionsStale")).toBool()) {
        bool present = false;
        for (const auto& entry : connections_)
            present |= entry.toObject().value(QStringLiteral("id")) == id;
        if (!present) {
            completion(QStringLiteral("closeEnded"));
            return;
        }
    }
    engine_.api(
        "DELETE", "/connections/" + QUrl::toPercentEncoding(id), {},
        [this, epoch, id, completion = std::move(completion)](domain::Result<QJsonObject> result) {
            if (epoch != epoch_)
                return;
            const auto* failure = std::get_if<domain::Error>(&result);
            if (!failure || failure->code == QStringLiteral("CoreTargetNotFound")) {
                completion(failure ? QStringLiteral("closeEnded")
                                   : QStringLiteral("closeSucceeded"));
                return;
            }
            if (failure->code != QStringLiteral("CoreRequestTimeout") &&
                failure->code != QStringLiteral("ServiceTimeout")) {
                completion(QStringLiteral("closeFailed"));
                return;
            }
            engine_.api("GET", "/connections", {},
                        [this, epoch, id, completion](domain::Result<QJsonObject> checked) {
                            if (epoch != epoch_)
                                return;
                            if (const auto* value = std::get_if<QJsonObject>(&checked)) {
                                const auto parsed =
                                    domain::connectionSnapshot(*value, {}, epoch, 0);
                                if (const auto* entries = std::get_if<QJsonArray>(&parsed)) {
                                    for (const auto& entry : *entries)
                                        if (entry.toObject().value(QStringLiteral("id")) == id) {
                                            completion(QStringLiteral("closeFailed"));
                                            return;
                                        }
                                    completion(QStringLiteral("closeEnded"));
                                    return;
                                }
                            }
                            completion(QStringLiteral("closeUnknown"));
                        });
        });
}
} // namespace quickclash::application
