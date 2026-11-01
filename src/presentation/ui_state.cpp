#include "presentation/ui_state.h"
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace quickclash::presentation {
namespace {
// 60 samples at ~1 Hz cover 60 seconds of traffic history. The ring keeps a
// fixed footprint; page switches retain it, epoch changes and hide reset it.
constexpr int kTrafficHistoryLimit = 60;
double replayRate(int sample, bool upload) {
    const double phase = static_cast<double>(sample);
    return upload ? 48.0 + 8.0 * std::sin(phase * 0.37) + 4.0 * std::sin(phase * 0.13)
                  : 2.1 + 0.35 * std::sin(phase * 0.29) + 0.15 * std::sin(phase * 0.11);
}
} // namespace
UiState::UiState(QString settingsFile, QObject* parent) : QObject(parent) {
    liveUpload_.fill(QVariant{}, kTrafficHistoryLimit);
    liveDownload_.fill(QVariant{}, kTrafficHistoryLimit);
    if (!settingsFile.isEmpty()) {
        settings_ =
            std::make_unique<infra::SettingsService>(QFileInfo(settingsFile).absolutePath());
        connect(settings_.get(), &infra::SettingsService::loaded, this, &UiState::applyPreferences);
        connect(settings_.get(), &infra::SettingsService::saved, this, &UiState::applyPreferences);
        connect(settings_.get(), &infra::SettingsService::runtimeChanged, this,
                &UiState::applyControl);
        connect(settings_.get(), &infra::SettingsService::dataChanged, this, &UiState::applyData);
        connect(settings_.get(), &infra::SettingsService::operationCompleted, this,
                &UiState::operationCompleted);
        connect(settings_.get(), &infra::SettingsService::failed, this,
                [this](const QString& code) {
                    storageError_ = code;
                    settingsReady_ = false;
                    emit storageChanged();
                });
    } else {
        settingsReady_ = true;
    }
    replayTimer_.setInterval(1000);
    connect(&replayTimer_, &QTimer::timeout, this, [this] {
        sample_ = (sample_ + 1) % 10000;
        if (demands_.values().contains(QStringLiteral("connections"))) {
            connectionModel_.tick();
        }
        emit sampleChanged();
    });
}
bool UiState::acceptGeneration(quint64 generation, const QString& epoch) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation == 0 || generation < publicationGeneration_)
        return false;
    if (generation == publicationGeneration_)
        return epoch == liveEpoch_;
    publicationGeneration_ = generation;
    liveEpoch_ = epoch;
    for (const auto& channel : application::scopedChannels())
        clearChannel(channel);
    // A new session epoch invalidates the accumulated traffic curve; demand
    // generation changes alone must not clear it (page switches keep it).
    clearTrafficHistory();
    appliedData_.remove(QStringLiteral("samples"));
    refreshTelemetry();
    return true;
}
void UiState::applyControl(const QJsonObject& control) {
    if (!acceptGeneration(
            control.value(QStringLiteral("publicationGeneration")).toString().toULongLong(),
            control.value(QStringLiteral("epoch")).toString()))
        return;
    const auto value = control.toVariantMap();
    if (runtime_ == value)
        return;
    runtime_ = value;
    emit runtimeChanged();
}
void UiState::applyData(const application::RuntimeUpdate& update) {
    if (!acceptGeneration(update.generation, update.epoch))
        return;
    for (const auto& channel : update.invalidated) {
        const auto stream = application::channelStream(channel);
        if (!stream.isEmpty() && update.demandGenerations.value(stream) != 0 &&
            update.demandGenerations.value(stream) == dataDemands_.generations.value(stream))
            clearChannel(channel);
    }
    bool telemetryChanged = false;
    for (auto it = update.values.constBegin(); it != update.values.constEnd(); ++it) {
        const auto stream = application::channelStream(it.key());
        if (!stream.isEmpty() &&
            !dataDemands_.accepts(stream, update.demandGenerations.value(stream)))
            continue;
        if (appliedData_.contains(it.key()) && appliedData_.value(it.key()) == it.value())
            continue;
        if (it.key() == QStringLiteral("profiles") && !profileModel_.apply(it.value().toArray()))
            continue;
        appliedData_.insert(it.key(), it.value());
        if (it.key() == QStringLiteral("samples") || it.key() == QStringLiteral("traffic") ||
            it.key() == QStringLiteral("resources") ||
            it.key() == QStringLiteral("connection_status") ||
            it.key() == QStringLiteral("log_status") || it.key() == QStringLiteral("rule_status") ||
            it.key() == QStringLiteral("provider_status")) {
            telemetryChanged = true;
        } else if (it.key() == QStringLiteral("profiles")) {
            emit profilesChanged();
        } else if (it.key() == QStringLiteral("editor")) {
            emit editorChanged();
        } else if (it.key() == QStringLiteral("groups")) {
            groups_ = it.value().toArray().toVariantList();
            emit groupsChanged();
        } else if (it.key() == QStringLiteral("nodes")) {
            nodes_ = it.value().toArray().toVariantList();
            emit nodesChanged();
        } else if (it.key() == QStringLiteral("delays")) {
            delays_ = it.value().toObject().toVariantMap();
            emit delaysChanged();
        } else if (it.key() == QStringLiteral("connections")) {
            connectionRows_.apply(it.value().toArray());
        } else if (it.key() == QStringLiteral("rules")) {
            ruleRows_.apply(it.value().toArray());
        } else if (it.key() == QStringLiteral("providers")) {
            providerRows_.apply(it.value().toArray());
        } else if (it.key() == QStringLiteral("logs")) {
            pendingLogs_ = it.value().toArray();
            if (!logsPaused_)
                logRows_.apply(pendingLogs_);
        }
    }
    if (telemetryChanged)
        refreshTelemetry();
}
void UiState::clearChannel(const QString& channel) {
    appliedData_.remove(channel);
    if (channel == QStringLiteral("editor"))
        emit editorChanged();
    if (channel == QStringLiteral("connections"))
        liveConnections_.release();
    else if (channel == QStringLiteral("rules"))
        liveRules_.release();
    else if (channel == QStringLiteral("providers"))
        liveProviders_.release();
    else if (channel == QStringLiteral("logs")) {
        pendingLogs_ = {};
        liveLogs_.release();
    } else if (channel == QStringLiteral("traffic")) {
        // Demand generation changes only drop the lease; the bounded history
        // and the last-sample dedup marker survive so a page switch resumes
        // the curve and a re-sent identical sample is not appended twice.
        // Epoch changes and hide reset both via clearTrafficHistory().
    }
    refreshTelemetry();
    emit channelInvalidated(channel);
}
void UiState::clearTrafficHistory() {
    const bool hadHistory = !trafficSample_.isEmpty();
    liveUpload_.fill(QVariant{}, kTrafficHistoryLimit);
    liveDownload_.fill(QVariant{}, kTrafficHistoryLimit);
    trafficSample_.clear();
    if (hadHistory)
        emit sampleChanged();
}
void UiState::refreshTelemetry() {
    QJsonObject combined = appliedData_.value(QStringLiteral("samples")).toObject();
    for (const auto* key : {"traffic", "resources", "connection_status", "log_status",
                            "rule_status", "provider_status"}) {
        const auto fields = appliedData_.value(QLatin1String(key)).toObject();
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
            combined.insert(it.key(), it.value());
    }
    const auto next = combined.toVariantMap();
    if (next == telemetry_)
        return;
    const auto previous = telemetry_.value(QStringLiteral("traffic"));
    telemetry_ = next;
    const auto sample = telemetry_.value(QStringLiteral("traffic")).toMap();
    const auto timestamp = sample.value(QStringLiteral("time")).toString();
    if (!timestamp.isEmpty() && timestamp != trafficSample_) {
        const auto previousTime = trafficSample_.toLongLong();
        const auto nextTime = timestamp.toLongLong();
        const auto gap = previousTime > 0 && nextTime > previousTime
                             ? std::clamp<qint64>(qRound64((nextTime - previousTime) / 1000.0) - 1,
                                                  0, kTrafficHistoryLimit)
                             : 0;
        for (qint64 i = 0; i < gap; ++i) {
            liveUpload_.append(QVariant{});
            liveDownload_.append(QVariant{});
        }
        trafficSample_ = timestamp;
        // Bounded ring: 60 samples at ~1 Hz cover 60 seconds; the list stays
        // at a fixed size so the history never grows with uptime.
        liveUpload_.append(sample.value(QStringLiteral("up")).toDouble() / 1024.0);
        liveDownload_.append(sample.value(QStringLiteral("down")).toDouble() / 1024.0);
        while (liveUpload_.size() > kTrafficHistoryLimit)
            liveUpload_.removeFirst();
        while (liveDownload_.size() > kTrafficHistoryLimit)
            liveDownload_.removeFirst();
    }
    emit telemetryChanged();
    if (previous != telemetry_.value(QStringLiteral("traffic")))
        emit sampleChanged();
}
void UiState::setLogsPaused(bool paused) {
    if (logsPaused_ == paused)
        return;
    logsPaused_ = paused;
    if (!paused && dataDemands_.active.contains(QStringLiteral("logs")))
        logRows_.apply(pendingLogs_);
}
QJsonObject UiState::diagnosticSnapshot() const {
    auto result = QJsonObject::fromVariantMap(runtime_);
    auto features = result.value(QStringLiteral("features")).toObject();
    auto samples = QJsonObject::fromVariantMap(telemetry_);
    result.insert(QStringLiteral("coreUptimeSeconds"),
                  samples.take(QStringLiteral("coreUptimeSeconds")));
    result.insert(QStringLiteral("droppedLogs"), samples.take(QStringLiteral("droppedLogs")));
    for (auto it = samples.constBegin(); it != samples.constEnd(); ++it)
        features.insert(it.key(), it.value());
    features.insert(QStringLiteral("nodes"), appliedData_.value(QStringLiteral("nodes")));
    result.insert(QStringLiteral("features"), features);
    for (auto it = appliedData_.constBegin(); it != appliedData_.constEnd(); ++it)
        if (it.key() != QStringLiteral("samples") && it.key() != QStringLiteral("nodes") &&
            it.key() != QStringLiteral("traffic") && it.key() != QStringLiteral("resources") &&
            it.key() != QStringLiteral("connection_status") &&
            it.key() != QStringLiteral("log_status") && it.key() != QStringLiteral("rule_status") &&
            it.key() != QStringLiteral("provider_status") && it.key() != QStringLiteral("editor"))
            result.insert(it.key(), it.value());
    result.insert(
        QStringLiteral("dataLifecycle"),
        QJsonObject{
            {QStringLiteral("active"), QJsonArray::fromStringList(dataDemands_.active.values())},
            {QStringLiteral("appliedChannels"), QJsonArray::fromStringList(appliedData_.keys())},
            {QStringLiteral("pendingLogCount"), pendingLogs_.size()}});
    return result;
}
QVariantList UiState::navigation() const {
    QVariantList items;
    for (const auto& feature : registry_.project({"home", "proxies", "subscriptions", "connections",
                                                  "rules", "logs", "tests", "settings"})) {
        items.append(QVariantMap{{QStringLiteral("pageId"), feature.id},
                                 {QStringLiteral("title"), feature.title},
                                 {QStringLiteral("icon"), feature.icon},
                                 {QStringLiteral("availability"), feature.availability}});
    }
    return items;
}
QString UiState::pageTitle() const {
    return registry_.resolve(currentPage_)->title;
}
QString UiState::hostStatus() const {
    if (hostState_ == QStringLiteral("Visible"))
        return tr("Visible");
    if (hostState_ == QStringLiteral("Hidden"))
        return tr("Hidden");
    if (hostState_ == QStringLiteral("Minimized"))
        return tr("Minimized");
    if (hostState_ == QStringLiteral("Unloading"))
        return tr("Unloading");
    if (hostState_ == QStringLiteral("Unloaded"))
        return tr("Unloaded");
    return tr("Loading");
}
QVariantList UiState::compactNavigation() const {
    QVariantList items;
    for (const auto& feature :
         registry_.project({"home", "proxies", "subscriptions", "settings"})) {
        items.append(QVariantMap{{QStringLiteral("pageId"), feature.id},
                                 {QStringLiteral("title"), feature.title},
                                 {QStringLiteral("icon"), feature.icon}});
    }
    return items;
}
QString UiState::runtimeStatus() const {
    const auto status = runtime_.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("Ready"))
        return tr("Ready");
    if (status == QStringLiteral("Stopped"))
        return tr("Stopped");
    if (status == QStringLiteral("Starting"))
        return tr("Starting");
    if (status == QStringLiteral("Validating"))
        return tr("Validating");
    if (status == QStringLiteral("RestoringSelections"))
        return tr("Restoring selections");
    if (status == QStringLiteral("RecoveryBackoff"))
        return tr("Waiting to recover");
    if (status == QStringLiteral("Failed"))
        return tr("Failed");
    return tr("Loading");
}
QString UiState::runtimeError() const {
    const auto code = runtime_.value(QStringLiteral("error")).toString();
    if (code == QStringLiteral("CorePlatformUnsupported") ||
        code == QStringLiteral("CredentialStoreUnsupported") ||
        code == QStringLiteral("SystemProxyUnsupported") ||
        code == QStringLiteral("ServicePlatformUnsupported"))
        return tr("This feature is not available on this platform yet.");
    if (code == QStringLiteral("ServiceSessionBusy"))
        return tr("The Windows service is in use by another client. Close that session first.");
    if (code == QStringLiteral("ServiceBusy"))
        return tr("The Windows service is still finishing its last request. Try again in a "
                  "moment.");
    if (code == QStringLiteral("ServiceProtocolMismatch") ||
        code == QStringLiteral("ServiceHandshakeRequired"))
        return tr("The Windows service belongs to a different version. Enable TUN again to update "
                  "it from this copy.");
    if (code == QStringLiteral("ServiceUnavailable"))
        return tr("The Windows service could not be reached. Enable TUN again to retry; if it "
                  "persists, check the service in Windows Services.");
    if (code == QStringLiteral("CoreExited"))
        return tr("The proxy core exited unexpectedly. TUN is no longer active. Retry; if it "
                  "repeats, review the app log for the core exit code.");
    if (code == QStringLiteral("CoreExitedDuringStart"))
        return tr("The proxy core exited while starting. Review the configuration and app log.");
    if (code == QStringLiteral("CoreExitedDuringApply"))
        return tr("The proxy core stopped while applying the profile. The previous profile was "
                  "restored.");
    if (code == QStringLiteral("CoreRecoveryExhausted"))
        return tr("The proxy core stopped repeatedly in a short time. Automatic restart is "
                  "paused. Review the app log, then start it again.");
    if (code == QStringLiteral("TunRecoveryExhausted"))
        return tr("TUN stopped repeatedly in a short time. QuickClash is running in proxy-only "
                  "mode. Turn TUN on again to retry after checking other VPN or TUN software.");
    if (code == QStringLiteral("TunNetworkUnavailable"))
        return tr("TUN is enabled but its virtual adapter is no longer observable. The core is "
                  "still running; turn TUN off and on to recreate it.");
    if (code == QStringLiteral("ServiceIdentityRejected"))
        return tr("The Windows service identity could not be verified. Check the protected "
                  "installation.");
    if (code == QStringLiteral("ServiceClientUntrusted"))
        return tr("TUN is not authorized for this QuickClash folder. Enable TUN again and approve "
                  "the single administrator request. Moving or renaming the folder requires "
                  "authorization again.");
    if (code == QStringLiteral("ServiceInstallationRequired"))
        return tr("TUN requires the Windows service. Enable TUN again to approve one "
                  "administrator request that installs it from this package.");
    if (code == QStringLiteral("ServiceNotRunning"))
        return tr("The Windows service is stopped. Enable TUN again to approve one administrator "
                  "request that starts it.");
    if (code == QStringLiteral("ServiceSetupDeclined"))
        return tr("Administrator confirmation was cancelled, so TUN stays off. Enable TUN again "
                  "when you are ready.");
    if (code == QStringLiteral("ServiceSetupClientRootRejected"))
        return tr("This folder cannot be authorized for TUN. Run QuickClash from a normal folder "
                  "without junctions or links.");
    if (code == QStringLiteral("ServiceSetupPackageMissing"))
        return tr("This copy is missing quickclash_service.exe. Extract the complete package and "
                  "enable TUN again.");
    if (code == QStringLiteral("ServiceSetupLaunchFailed"))
        return tr("The administrator helper could not start. Try again from a normal folder.");
    if (code == QStringLiteral("ServiceSetupTimeout"))
        return tr("The administrator helper did not finish. Review its window, then enable TUN "
                  "again.");
    if (code == QStringLiteral("ServiceSetupFailed"))
        return tr("Administrator setup did not complete. Review the setup log and try again.");
    if (code == QStringLiteral("CoreListenersNotOwned") ||
        code == QStringLiteral("CoreListenerPortUnavailable"))
        return tr("The core could not bind its local ports. Close conflicting applications or "
                  "change the proxy and controller ports in Settings, then retry.");
    if (code == QStringLiteral("CoreListenerAddressInUse"))
        return tr("Local TCP port %1 is already in use. Choose available ports in Settings or "
                  "close the conflicting application, then retry.")
            .arg(runtime_.value(QStringLiteral("listenerIssue")).toMap().value("port").toInt());
    if (code == QStringLiteral("CoreListenerAccessDenied"))
        return tr("Windows denied binding local TCP port %1. Check system restrictions or "
                  "choose available ports in Settings, then retry.")
            .arg(runtime_.value(QStringLiteral("listenerIssue")).toMap().value("port").toInt());
    if (code == QStringLiteral("NoAvailableListenerPorts"))
        return tr("No available ports were found nearby. Enter different ports and retry.");
    if (code == QStringLiteral("ServiceTunRouteConflict"))
        return tr("Another virtual adapter is handling default traffic. Turn off its VPN or TUN "
                  "mode before enabling QuickClash TUN, then retry.");
    if (code == QStringLiteral("ServiceTunNameExhausted") ||
        code == QStringLiteral("ServiceTunNameChanged"))
        return tr("No unused TUN adapter name was found. Review virtual adapters and retry.");
    if (code == QStringLiteral("ServiceNetworkInspectionFailed"))
        return tr("Windows network state could not be verified. Keep the current mode and retry.");
    if (code == QStringLiteral("ServiceCleanupUncertain"))
        return tr("QuickClash could not confirm TUN cleanup. A second session is blocked. Check "
                  "the service and its recorded adapter, then retry.");
    if (code == QStringLiteral("CoreConfigReadbackFailed"))
        return tr("The core configuration does not match the requested local session. Stop the "
                  "core and retry.");
    if (code == QStringLiteral("StoragePreviewStale"))
        return tr("Data changed or the confirmation expired. Preview the operation again.");
    if (code == QStringLiteral("MaintenanceUnavailable"))
        return tr("Finish active operations, stop the core and resolve the system proxy lease "
                  "before repairing data.");
    if (code == QStringLiteral("StorageBudgetExceeded"))
        return tr("The log storage budget is full. Review storage cleanup before exporting again.");
    if (code == QStringLiteral("RecoveryArchiveIncomplete"))
        return tr("Data archival was interrupted. Keep the data and recovery folders, and follow "
                  "the recovery guide before restarting normally.");
    if (code == QStringLiteral("RecoveryArchiveFailed"))
        return tr("Data could not be archived. Original data was restored; check folder access and "
                  "try again.");
    if (code == QStringLiteral("SchemaTooNew"))
        return tr("These data require a newer QuickClash version. Keep the originals or archive "
                  "them before importing compatible data.");
    if (code == QStringLiteral("InvalidBackup"))
        return tr(
            "The backup format, size or checksum is invalid. Existing profiles are preserved.");
    if (code == QStringLiteral("BackupPreviewRequired"))
        return tr("Preview this backup again before confirming the restore.");
    if (code == QStringLiteral("BackupValidationFailed"))
        return tr("Core validation failed. No profiles were restored.");
    if (code == QStringLiteral("BackupLimitExceeded"))
        return tr("The backup exceeds the supported size limit.");
    if (code == QStringLiteral("BackupLocalFileRequired"))
        return tr("Choose a readable local backup file.");
    if (code == QStringLiteral("EditorConflict"))
        return tr(
            "The profile changed. Compare the draft, save a copy, or load the latest version.");
    if (code == QStringLiteral("InvalidOverlay"))
        return tr("Invalid override operations or managed fields. Check set, remove and rules.");
    if (code == QStringLiteral("EditorPreviewRequired"))
        return tr("Validate and review a fresh preview before saving and applying.");
    if (code == QStringLiteral("EditorLimitExceeded"))
        return tr("The editor supports up to 1 MiB. Import larger profiles from a file.");
    if (code == QStringLiteral("EditorUtf8Required"))
        return tr("The editor requires UTF-8. Convert the source file before editing.");
    if (code == QStringLiteral("InvalidEditorDraft"))
        return tr("The saved draft is invalid. Load the latest version to replace it.");
    const auto issues = runtime_.value(QStringLiteral("configIssues")).toList();
    if (!issues.isEmpty()) {
        QStringList lines;
        for (const auto& value : issues) {
            const auto issue = value.toMap();
            const auto kind = issue.value(QStringLiteral("code")).toString();
            const auto reason =
                kind == QStringLiteral("UnsupportedProtocol")    ? tr("Unsupported protocol")
                : kind == QStringLiteral("UnsupportedTransport") ? tr("Unsupported transport")
                : kind == QStringLiteral("UnsupportedGroup")     ? tr("Unsupported group policy")
                : kind == QStringLiteral("UnsupportedRule")      ? tr("Unsupported rule")
                : kind == QStringLiteral("UnsupportedDns")
                    ? tr("DNS management is not available yet")
                : kind == QStringLiteral("UnsupportedResource")
                    ? tr("External resource is not supported yet")
                : kind == QStringLiteral("InvalidConfigField")
                    ? tr("Invalid configuration field")
                    : tr("Unsupported configuration field");
            lines.append(tr("%1: %2 (%3; %4)")
                             .arg(issue.value(QStringLiteral("path")).toString(), reason,
                                  issue.value(QStringLiteral("feature")).toString(),
                                  issue.value(QStringLiteral("phase")).toString()));
        }
        return tr("Profile compatibility issues (up to 32):") + '\n' + lines.join('\n');
    }
    if (code == QStringLiteral("EmptySubscription"))
        return tr("The subscription returned no content. Check the URL or account status.");
    if (code == QStringLiteral("UnsupportedSubscriptionFormat"))
        return tr("The server did not return a Clash Meta (mihomo) YAML profile. Use a standard "
                  "subscription URL; Base64 and share links are not supported yet.");
    if (code == QStringLiteral("UnsupportedConfigFeature"))
        return tr("This profile uses unsupported features. Inline HTTP, SOCKS5, Shadowsocks, "
                  "Hysteria2, Trojan, VLESS, VMess, TUIC and select/url-test/fallback/load-balance "
                  "groups are supported. Check the feature details for unsupported fields.");
    if (code == QStringLiteral("GeoResourceIntegrityFailed"))
        return tr("The bundled Geo data is missing or damaged. Restore the complete application "
                  "package.");
    if (code == QStringLiteral("GeoResourceDownloadFailed"))
        return tr("Unable to download both Geo databases. The previous snapshot is kept.");
    if (code == QStringLiteral("UpdateProxyUnavailable"))
        return tr("Start the core before downloading through the current proxy, or select direct "
                  "download.");
    if (code == QStringLiteral("StartupRegistrationFailed"))
        return tr("Unable to save the Windows startup registration. Refresh its status and retry.");
    if (code == QStringLiteral("StartupUnsupported"))
        return tr("Login startup is not supported on this platform.");
    if (code == QStringLiteral("CoreDownloadFailed"))
        return tr("The core download failed. The confirmed core is kept.");
    if (code == QStringLiteral("CoreUpdateWriteFailed"))
        return tr("Unable to write the managed core. Check free space and write access to the "
                  "portable core directory.");
    if (code == QStringLiteral("CoreArchiveUnavailable") ||
        code == QStringLiteral("CoreArchiveFailed"))
        return tr("Unable to extract the verified core archive with Windows tar. The confirmed "
                  "core is kept.");
    if (code == QStringLiteral("ProviderCacheUnavailable"))
        return tr("A saved provider resource is missing or damaged. Update the full subscription "
                  "to repair it.");
    if (code == QStringLiteral("ProviderModeMismatch"))
        return tr("This provider uses core runtime mode. Refresh it from the providers page.");
    if (code == QStringLiteral("ProviderRefreshUncertain"))
        return tr("The provider refresh response timed out. The core may still complete it. Check "
                  "the provider state before retrying.");
    if (code == QStringLiteral("ProviderDownloadFailed") ||
        code == QStringLiteral("InvalidProviderPayload") ||
        code == QStringLiteral("InvalidProviderConfig"))
        return tr(
            "The provider could not be downloaded or validated. The previous version is kept.");
    if (code == QStringLiteral("ProviderLimitExceeded") ||
        code == QStringLiteral("ProviderHttpsRequired") ||
        code == QStringLiteral("UnsupportedProviderField") ||
        code == QStringLiteral("UnsupportedProviderFormat"))
        return tr("Use at most 32 HTTPS providers, 4 MiB each and 16 MiB total, with supported "
                  "YAML or text payloads.");
    if (code == QStringLiteral("ProxyGroupAutomatic"))
        return tr("Clash Meta (mihomo) manages this group's current node automatically.");
    if (code == QStringLiteral("SubscriptionTooLarge"))
        return tr("The subscription exceeds the 16 MiB limit.");
    if (code == QStringLiteral("InvalidSubscriptionSchedule"))
        return tr("Choose a subscription update interval between 15 minutes and 7 days, or manual "
                  "updates.");
    if (code == QStringLiteral("SubscriptionRedirectBlocked"))
        return tr("The subscription redirect is unsafe, loops or exceeds five redirects.");
    if (code == QStringLiteral("SubscriptionProxyUnavailable"))
        return tr("Start the core before downloading through the running proxy, or choose Direct.");
    if (code == QStringLiteral("InvalidSubscriptionRoute"))
        return tr("Choose Direct, Running proxy or Auto for subscription downloads.");
    if (code == QStringLiteral("SubscriptionCacheInvalid"))
        return tr(
            "The server returned no content after an unconditional request. Try again later.");
    if (code == QStringLiteral("SubscriptionDownloadFailed"))
        return tr("Unable to download the subscription. Check the network, HTTPS certificate and "
                  "subscription validity.");
    if (code == QStringLiteral("SubscriptionAuthenticationFailed"))
        return tr(
            "The subscription server denied access. Check the URL, account or request headers.");
    if (code == QStringLiteral("HttpsSubscriptionRequired"))
        return tr(
            "Enter a valid HTTPS subscription URL without embedded login details or a fragment.");
    if (code == QStringLiteral("InvalidDelayTarget"))
        return tr("Enter a valid HTTPS node delay target without login details or a fragment.");
    if (code == QStringLiteral("InvalidNetworkSettings"))
        return tr(
            "Choose different proxy and controller ports from 1024 to 65535, a supported mode "
            "and DNS policy.");
    return code;
}
QUrl UiState::pageSource() const {
    return registry_.resolve(currentPage_)->page;
}
bool UiState::navigate(const QString& pageId, const QVariantMap& context) {
    if (!registry_.resolve(pageId, context)) {
        routeError_ = tr("Unable to open page: invalid route or parameters");
        emit routeErrorChanged();
        return false;
    }
    routeError_.clear();
    emit routeErrorChanged();
    if (context.contains(QStringLiteral("filter"))) {
        setFilter(pageId, context.value(QStringLiteral("filter")).toString());
    }
    if (currentPage_ != pageId) {
        currentPage_ = pageId;
        emit routeChanged();
    }
    return true;
}
QUrl UiState::embeddedSource(const QString& contentId, const QVariantMap& context) const {
    const auto* descriptor = registry_.resolveEmbedded(contentId, context);
    return descriptor ? descriptor->component : QUrl{};
}
bool UiState::dark() const {
    return themeMode_ == QStringLiteral("dark") ||
           (themeMode_ == QStringLiteral("system") && systemDark_);
}
void UiState::setThemeMode(const QString& mode) {
    if ((mode != QStringLiteral("system") && mode != QStringLiteral("light") &&
         mode != QStringLiteral("dark")) ||
        mode == themeMode_) {
        return;
    }
    persist(QStringLiteral("theme"), mode);
}
void UiState::setSystemDark(bool dark) {
    if (systemDark_ == dark) {
        return;
    }
    systemDark_ = dark;
    if (themeMode_ == QStringLiteral("system")) {
        emit themeChanged();
    }
}
void UiState::setUiVisible(bool visible, bool minimized) {
    minimized = minimized && !visible;
    if (uiVisible_ == visible && uiMinimized_ == minimized) {
        return;
    }
    if (uiVisible_ && !visible)
        emit uiAboutToHide();
    uiVisible_ = visible;
    uiMinimized_ = minimized;
    if (!visible) {
        demands_.clear();
        if (!minimized)
            clearTrafficHistory();
    }
    emit uiVisibleChanged();
    updateTimer();
    emit demandsChanged();
}
void UiState::setReplay(bool replay) {
    if (replay_ == replay) {
        return;
    }
    replay_ = replay;
    proxyModel_.setRows(replay ? 500 : 0);
    connectionModel_.setRows(replay ? replayConnections_ : 0);
    updateTimer();
    emit replayChanged();
    emit sampleChanged();
    emit demandsChanged();
}
bool UiState::setReplayConnections(int rows) {
    if (rows != 1000 && rows != 10000) {
        return false;
    }
    if (replayConnections_ != rows) {
        replayConnections_ = rows;
        if (replay_) {
            connectionModel_.setRows(rows);
        }
        emit replayChanged();
    }
    return true;
}
int UiState::activeStreamCount() const {
    if (!replay_)
        return static_cast<int>(dataDemands_.active.size());
    const auto values = demands_.values();
    return static_cast<int>(QSet<QString>(values.cbegin(), values.cend()).size());
}
QString UiState::createDemandKey(const QString& scope) const {
    return scope.left(64) + QLatin1Char(':') + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
void UiState::setDemand(const QString& instanceKey, const QString& stream, bool active) {
    const auto streams = application::RuntimeDemands::streams();
    if (instanceKey.isEmpty() || instanceKey.size() > 128 || !streams.contains(stream)) {
        return;
    }
    if (active && uiVisible_ && (demands_.contains(instanceKey) || demands_.size() < 32)) {
        demands_.insert(instanceKey, stream);
    } else {
        demands_.remove(instanceKey);
    }
    updateTimer();
    emit demandsChanged();
}
void UiState::updateTimer() {
    auto streams = demands_.values();
    // Keep one shared ~1 Hz stream across routes and taskbar minimization.
    // Page consumers still release their heavier data when not visible.
    if (!replay_ && (uiVisible_ || uiMinimized_))
        streams.append(QStringLiteral("traffic"));
    const auto previous = dataDemands_;
    dataDemands_.setActive(replay_ ? QStringList{} : streams);
    if (settings_)
        settings_->setDemands(dataDemands_);
    for (const auto& channel : application::scopedChannels()) {
        const auto stream = application::channelStream(channel);
        if (previous.generations.value(stream) != dataDemands_.generations.value(stream))
            clearChannel(channel);
    }
    const bool timed = streams.contains(QStringLiteral("traffic")) ||
                       streams.contains(QStringLiteral("connections"));
    if (replay_ && uiVisible_ && timed) {
        if (!replayTimer_.isActive()) {
            replayTimer_.start();
        }
    } else {
        replayTimer_.stop();
    }
}
QString UiState::upload() const {
    const auto traffic = telemetry_.value(QStringLiteral("traffic")).toMap();
    if (!replay_ && traffic.isEmpty())
        return QStringLiteral("—");
    const double bytes = replay_ ? replayRate(sample_, true) * 1024.0
                                 : traffic.value(QStringLiteral("up")).toDouble();
    return bytes >= 1048576 ? QStringLiteral("%1 MiB/s").arg(bytes / 1048576.0, 0, 'f', 2)
                            : QStringLiteral("%1 KiB/s").arg(bytes / 1024.0, 0, 'f', 1);
}
QString UiState::download() const {
    const auto traffic = telemetry_.value(QStringLiteral("traffic")).toMap();
    if (!replay_ && traffic.isEmpty())
        return QStringLiteral("—");
    const double bytes = replay_ ? replayRate(sample_, false) * 1048576.0
                                 : traffic.value(QStringLiteral("down")).toDouble();
    return bytes >= 1048576 ? QStringLiteral("%1 MiB/s").arg(bytes / 1048576.0, 0, 'f', 2)
                            : QStringLiteral("%1 KiB/s").arg(bytes / 1024.0, 0, 'f', 1);
}
QVariantList UiState::uploadHistory() const {
    return history(true);
}
QVariantList UiState::downloadHistory() const {
    return history(false);
}
QVariantList UiState::history(bool upload) const {
    if (!replay_)
        return upload ? liveUpload_ : liveDownload_;
    QVariantList result;
    if (replay_) {
        result.reserve(kTrafficHistoryLimit);
        for (int offset = kTrafficHistoryLimit - 1; offset >= 0; --offset) {
            result.append(replayRate(sample_ - offset, upload));
        }
    }
    return result;
}
QVariantMap UiState::readState(const QString& instanceKey) const {
    return states_.value(instanceKey);
}
void UiState::saveState(const QString& instanceKey, const QVariantMap& state) {
    if (instanceKey.isEmpty() || instanceKey.size() > 128 || state.size() > 8 ||
        (!states_.contains(instanceKey) && states_.size() >= 32)) {
        return;
    }
    QVariantMap bounded;
    for (auto it = state.cbegin(); it != state.cend(); ++it) {
        if (it.key() == QStringLiteral("filter")) {
            bounded.insert(it.key(), it.value().toString().left(128));
        } else if (it.key() == QStringLiteral("position")) {
            bounded.insert(it.key(), qBound(0.0, it.value().toDouble(), 10000000.0));
        } else if (it.key() == QStringLiteral("groups")) {
            // Expanded proxy group names; bounded and deduplicated.
            QStringList accepted;
            const auto names = it.value().toStringList();
            for (const auto& name : names) {
                if (name.size() <= 128 && !name.contains(QChar::Null) && !accepted.contains(name)) {
                    accepted.append(name);
                }
                if (accepted.size() >= 32) {
                    break;
                }
            }
            bounded.insert(it.key(), accepted);
        }
    }
    states_.insert(instanceKey, bounded);
}
void UiState::setFilter(const QString& pageId, const QString& text) {
    if (pageId != QStringLiteral("proxies") && pageId != QStringLiteral("connections")) {
        return;
    }
    (pageId == QStringLiteral("proxies") ? proxyModel_ : connectionModel_).setFilter(text);
    auto state = readState(pageId);
    const auto previous = state.value(QStringLiteral("filter")).toString();
    state.insert(QStringLiteral("filter"), text.left(128));
    saveState(pageId, state);
    if (previous != text.left(128)) {
        emit filterChanged(pageId, text.left(128));
    }
}
void UiState::setTrayAvailable(bool available) {
    if (trayAvailable_ != available) {
        trayAvailable_ = available;
        emit hostChanged();
    }
}
void UiState::setHostState(const QString& state) {
    if (hostState_ != state) {
        hostState_ = state;
        emit hostChanged();
    }
}
void UiState::setUnloadWhenHidden(bool enabled) {
    if (unloadWhenHidden_ == enabled) {
        return;
    }
    persist(QStringLiteral("unload_when_hidden"), enabled);
}
void UiState::acknowledgeClose() {
    persist(QStringLiteral("close_acknowledged"), true);
}
void UiState::persist(const QString& key, const QVariant& value) {
    if (!settingsReady_) {
        return;
    }
    infra::UiPreferences preferences{themeMode_, unloadWhenHidden_, closeAcknowledged_, dashboard_};
    if (key == QStringLiteral("dashboard"))
        preferences.dashboard = QJsonObject::fromVariantMap(value.toMap());
    if (key == QStringLiteral("theme")) {
        preferences.theme = value.toString();
    }
    if (key == QStringLiteral("unload_when_hidden")) {
        preferences.unloadWhenHidden = value.toBool();
    }
    if (key == QStringLiteral("close_acknowledged")) {
        preferences.closeAcknowledged = value.toBool();
    }
    if (settings_) {
        settingsReady_ = false;
        emit storageChanged();
        settings_->save(preferences);
    } else {
        applyPreferences(preferences.theme, preferences.unloadWhenHidden,
                         preferences.closeAcknowledged, preferences.dashboard);
    }
}
void UiState::applyPreferences(const QString& theme, bool unload, bool acknowledged,
                               const QJsonObject& dashboard) {
    themeMode_ = theme;
    unloadWhenHidden_ = unload;
    closeAcknowledged_ = acknowledged;
    dashboard_ = dashboard;
    storageError_.clear();
    settingsReady_ = true;
    emit themeChanged();
    emit hostChanged();
    emit storageChanged();
}
void UiState::runtimeCommand(const QString& command, const QStringList& arguments) {
    if (command == QStringLiteral("clear_logs")) {
        // Fence an already queued log delivery before clearing both GUI and worker buffers.
        ++dataDemands_.generations[QStringLiteral("logs")];
        clearChannel(QStringLiteral("logs"));
        clearChannel(QStringLiteral("log_status"));
        if (settings_)
            settings_->setDemands(dataDemands_);
    }
    if (settings_) {
        settings_->command(command, arguments);
    }
}
void UiState::copyText(const QString& text) {
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) && text.size() <= 8192 &&
        QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(text);
}
void UiState::setDashboardOption(const QString& key, bool value) {
    if (!dashboard_.contains(key) || dashboard_.value(key).toBool() == value)
        return;
    auto options = dashboard_;
    options.insert(key, value);
    persist(QStringLiteral("dashboard"), options.toVariantMap());
}
} // namespace quickclash::presentation
