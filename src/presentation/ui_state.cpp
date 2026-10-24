#include "presentation/ui_state.h"
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
        settings_ = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
        const auto mode =
            settings_->value(QStringLiteral("theme"), QStringLiteral("system")).toString();
        if (mode == QStringLiteral("light") || mode == QStringLiteral("dark")) {
            themeMode_ = mode;
        }
        unloadWhenHidden_ = settings_->value(QStringLiteral("unload_when_hidden"), false).toBool();
        closeAcknowledged_ = settings_->value(QStringLiteral("close_acknowledged"), false).toBool();
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
QUrl UiState::pageSource() const {
    return registry_.resolve(currentPage_)->page;
}
bool UiState::navigate(const QString& pageId, const QVariantMap& context) {
    if (!registry_.resolve(pageId, context)) {
        routeError_ = QStringLiteral("无法打开页面：路由或参数无效");
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
    themeMode_ = mode;
    persist(QStringLiteral("theme"), mode);
    emit themeChanged();
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
    unloadWhenHidden_ = enabled;
    persist(QStringLiteral("unload_when_hidden"), enabled);
    emit hostChanged();
}
void UiState::acknowledgeClose() {
    closeAcknowledged_ = true;
    persist(QStringLiteral("close_acknowledged"), true);
}
void UiState::persist(const QString& key, const QVariant& value) {
    if (settings_) {
        settings_->setValue(key, value);
    }
}
} // namespace quickclash::presentation
