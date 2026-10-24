#include "presentation/ui_state.h"
#include <QFileInfo>
#include <QSet>
#include <cmath>

namespace quickclash::presentation {
namespace {
double replayRate(int sample, bool upload) {
    const double phase = static_cast<double>(sample);
    return upload ? 48.0 + 8.0 * std::sin(phase * 0.37) + 4.0 * std::sin(phase * 0.13)
                  : 2.1 + 0.35 * std::sin(phase * 0.29) + 0.15 * std::sin(phase * 0.11);
}
} // namespace
UiState::UiState(QString settingsFile, QObject* parent) : QObject(parent) {
    if (!settingsFile.isEmpty()) {
        settings_ =
            std::make_unique<infra::SettingsService>(QFileInfo(settingsFile).absolutePath());
        connect(settings_.get(), &infra::SettingsService::loaded, this, &UiState::applyPreferences);
        connect(settings_.get(), &infra::SettingsService::saved, this, &UiState::applyPreferences);
        connect(settings_.get(), &infra::SettingsService::runtimeChanged, this,
                [this](const QJsonObject& state) {
                    runtime_ = state.toVariantMap();
                    emit runtimeChanged();
                });
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
    for (const auto& feature : registry_.project({"home", "proxies", "settings"})) {
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
        return tr("The server did not return a Clash/Mihomo YAML profile. Use a standard "
                  "subscription URL; Base64 and share links are not supported yet.");
    if (code == QStringLiteral("UnsupportedConfigFeature"))
        return tr(
            "This profile uses unsupported features. Inline HTTP, SOCKS5, Shadowsocks, "
            "Hysteria2, Trojan, VLESS, VMess and select/url-test/fallback groups are supported. "
            "Bundled Geo rules and basic fake-ip DNS are supported. Providers and advanced DNS "
            "options require later development.");
    if (code == QStringLiteral("GeoResourceIntegrityFailed"))
        return tr("The bundled Geo data is missing or damaged. Restore the complete application "
                  "package.");
    if (code == QStringLiteral("ProxyGroupAutomatic"))
        return tr("Mihomo manages this group's current node automatically.");
    if (code == QStringLiteral("SubscriptionTooLarge"))
        return tr("The subscription exceeds the 16 MiB limit.");
    if (code == QStringLiteral("SubscriptionRedirectUnsupported"))
        return tr(
            "The subscription redirected to another URL. Enter the final HTTPS subscription URL.");
    if (code == QStringLiteral("SubscriptionDownloadFailed"))
        return tr("Unable to download the subscription. Check the network, HTTPS certificate and "
                  "subscription validity.");
    if (code == QStringLiteral("HttpsSubscriptionRequired"))
        return tr(
            "Enter a valid HTTPS subscription URL without embedded login details or a fragment.");
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
void UiState::setDemand(const QString& instanceKey, const QString& stream, bool active) {
    const QStringList streams{QStringLiteral("traffic"), QStringLiteral("proxies"),
                              QStringLiteral("connections")};
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
    return replay_ ? QStringLiteral("%1 KiB/s").arg(replayRate(sample_, true), 0, 'f', 1)
                   : QStringLiteral("—");
}
QString UiState::download() const {
    return replay_ ? QStringLiteral("%1 MiB/s").arg(replayRate(sample_, false), 0, 'f', 1)
                   : QStringLiteral("—");
}
QVariantList UiState::uploadHistory() const {
    return history(true);
}
QVariantList UiState::downloadHistory() const {
    return history(false);
}
QVariantList UiState::history(bool upload) const {
    QVariantList result;
    if (replay_) {
        result.reserve(120);
        for (int offset = 119; offset >= 0; --offset) {
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
    if (settings_) {
        settings_->command(command, arguments);
    }
}
} // namespace quickclash::presentation
