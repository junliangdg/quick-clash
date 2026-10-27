#include "presentation/ui_state.h"
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <QUuid>
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
    // Start with a full zero ring so the sparkline scale is stable from the
    // first sample; real samples overwrite the oldest zeros as they arrive.
    liveUpload_.fill(0.0, kTrafficHistoryLimit);
    liveDownload_.fill(0.0, kTrafficHistoryLimit);
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
            it.key() == QStringLiteral("log_status")) {
            telemetryChanged = true;
        } else if (it.key() == QStringLiteral("profiles")) {
            emit profilesChanged();
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
    // Only a ring with real samples counts as history; the pre-filled zero
    // ring is the initial state and must not notify on its own reset.
    const bool hadHistory = !trafficSample_.isEmpty();
    // Pre-fill the ring with zero samples so the plot starts at a fixed
    // width/scale instead of re-layouting as real samples trickle in.
    liveUpload_.fill(0.0, kTrafficHistoryLimit);
    liveDownload_.fill(0.0, kTrafficHistoryLimit);
    trafficSample_.clear();
    if (hadHistory)
        emit sampleChanged();
}
void UiState::refreshTelemetry() {
    QJsonObject combined = appliedData_.value(QStringLiteral("samples")).toObject();
    for (const auto* key : {"traffic", "resources", "connection_status", "log_status"}) {
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
            it.key() != QStringLiteral("log_status"))
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
                                 {QStringLiteral("glyph"), feature.glyph},
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
                                 {QStringLiteral("glyph"), feature.glyph}});
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
    if (code == QStringLiteral("ProxyGroupAutomatic"))
        return tr("Clash Meta (mihomo) manages this group's current node automatically.");
    if (code == QStringLiteral("SubscriptionTooLarge"))
        return tr("The subscription exceeds the 16 MiB limit.");
    if (code == QStringLiteral("InvalidSubscriptionSchedule"))
        return tr("Choose a subscription update interval between 15 minutes and 7 days, or manual "
                  "updates.");
    if (code == QStringLiteral("SubscriptionRedirectUnsupported"))
        return tr(
            "The subscription redirected to another URL. Enter the final HTTPS subscription URL.");
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
void UiState::setUiVisible(bool visible) {
    if (uiVisible_ == visible) {
        return;
    }
    uiVisible_ = visible;
    if (!visible) {
        demands_.clear();
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
    const auto streams = demands_.values();
    const auto previous = dataDemands_;
    dataDemands_.setActive(replay_ || !uiVisible_ ? QStringList{} : streams);
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
    return replay_ ? QStringLiteral("%1 KiB/s").arg(replayRate(sample_, true), 0, 'f', 1)
                   : QStringLiteral("%1 KiB/s")
                         .arg(traffic.value(QStringLiteral("up")).toDouble() / 1024.0, 0, 'f', 1);
}
QString UiState::download() const {
    const auto traffic = telemetry_.value(QStringLiteral("traffic")).toMap();
    if (!replay_ && traffic.isEmpty())
        return QStringLiteral("—");
    return replay_ ? QStringLiteral("%1 MiB/s").arg(replayRate(sample_, false), 0, 'f', 1)
                   : QStringLiteral("%1 KiB/s")
                         .arg(traffic.value(QStringLiteral("down")).toDouble() / 1024.0, 0, 'f', 1);
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
    infra::UiPreferences preferences{themeMode_, unloadWhenHidden_, closeAcknowledged_};
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
                         preferences.closeAcknowledged);
    }
}
void UiState::applyPreferences(const QString& theme, bool unload, bool acknowledged) {
    themeMode_ = theme;
    unloadWhenHidden_ = unload;
    closeAcknowledged_ = acknowledged;
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
} // namespace quickclash::presentation
