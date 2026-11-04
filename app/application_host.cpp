#include "application_host.h"
#include "presentation/localization.h"
#include "ui_font.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QIcon>
#include <QQmlEngine>
#include <QQmlError>
#include <QScopedValueRollback>
#include <QScreen>
#include <QStyleHints>
#include <algorithm>
#ifdef Q_OS_WIN
#include <QSettings>
#endif
#include <utility>

namespace quickclash {
namespace {
bool darkTrayBackground() {
#ifdef Q_OS_WIN
    // Windows can use different appearances for applications and the taskbar.
    const QSettings personalization(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    const auto light = personalization.value(QStringLiteral("SystemUsesLightTheme"));
    if (light.isValid())
        return light.toInt() == 0;
#endif
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}
QString trayLabel(QString value, int limit = 48) {
    // Profile and node names are untrusted text; keep each native menu/tooltip line bounded.
    for (auto& character : value)
        if (character.isSpace() || character.category() == QChar::Other_Control)
            character = QLatin1Char(' ');
    return value.size() > limit ? value.left(limit - 1) + QChar(0x2026) : value;
}
QString modeLabel(const QString& mode) {
    return mode == QStringLiteral("rule")     ? ApplicationHost::tr("Rule")
           : mode == QStringLiteral("global") ? ApplicationHost::tr("Global")
           : mode == QStringLiteral("direct") ? ApplicationHost::tr("Direct")
                                              : ApplicationHost::tr("Unavailable");
}
} // namespace
ApplicationHost::ApplicationHost(QString settingsFile, bool noTray, QObject* parent)
    : QObject(parent), state_(std::move(settingsFile)) {
    QQmlEngine::setObjectOwnership(&state_, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(state_.proxyModel(), QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(state_.connectionModel(), QQmlEngine::CppOwnership);
    auto* hints = QGuiApplication::styleHints();
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/brand/quickclash.ico")));
    state_.setSystemDark(hints->colorScheme() == Qt::ColorScheme::Dark);
    connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
        state_.setSystemDark(scheme == Qt::ColorScheme::Dark);
        updateTrayIcon();
    });
    setupTray(noTray);
    connect(&state_, &presentation::UiState::runtimeChanged, this,
            &ApplicationHost::updateCriticalAlerts);
    connect(&state_, &presentation::UiState::profilesChanged, this,
            &ApplicationHost::updateCriticalAlerts);
    connect(&state_, &presentation::UiState::uiVisibleChanged, this,
            &ApplicationHost::updateCriticalAlerts);
    connect(&systemEvents_, &SystemEvents::typographyChanged, this,
            &ApplicationHost::updateTrayIcon, Qt::QueuedConnection);
    connect(
        &systemEvents_, &SystemEvents::typographyChanged, this,
        [] { QApplication::setFont(platformUiFont()); }, Qt::QueuedConnection);
    qApp->installEventFilter(this);
    connect(&systemEvents_, &SystemEvents::reconcileRequested, this,
            [this] { state_.runtimeCommand(QStringLiteral("reconcile")); });
    connect(&state_, &presentation::UiState::storageChanged, this, [this] {
        updateDeferredUnload();
        if (loginPending_ && (state_.settingsReady() || !state_.storageError().isEmpty())) {
            loginPending_ = false;
            (void)startAtLogin();
        }
        if (waitingForSettings_ && (state_.settingsReady() || !state_.storageError().isEmpty())) {
            waitingForSettings_ = false;
            (void)showWindow();
        }
    });
    connect(&deferredUnload_, &DeferredUiUnload::due, this, [this](quint64 generation) {
        if (generation != static_cast<quint64>(generation_) || !hiddenWindow() ||
            state_.hostState() != QStringLiteral("Hidden") || quitting_ || unloading_ || showing_)
            return;
        // Capability may disappear before the queued callback. Keep a recovery surface,
        // even if the preference was disabled or is being saved in the meantime.
        if (!recoverableFromTray()) {
            state_.setTrayAvailable(false);
            (void)showWindow();
            return;
        }
        if (state_.settingsReady() && state_.unloadWhenHidden())
            unloadWindow();
    });
    connect(&state_, &presentation::UiState::closeRequested, this, &ApplicationHost::hideWindow);
    connect(&state_, &presentation::UiState::quitRequested, this, &ApplicationHost::quit,
            Qt::QueuedConnection);
    connect(&state_, &presentation::UiState::minimizeRequested, this, [this] {
        if (window_) {
            window_->showMinimized();
        }
    });
    connect(
        &state_, &presentation::UiState::unloadRequested, this,
        [this] {
            if (recoverableFromTray()) {
                unloadWindow();
            }
        },
        Qt::QueuedConnection);
    connect(&state_, &presentation::UiState::hostChanged, this,
            &ApplicationHost::updateDeferredUnload);
}
ApplicationHost::~ApplicationHost() {
    quitting_ = true;
    deferredUnload_.cancel();
    state_.setUiVisible(false);
    engine_.reset();
}
bool ApplicationHost::eventFilter(QObject* watched, QEvent* event) {
    if (watched == qApp && event->type() == QEvent::ApplicationFontChange && window_)
        window_->setProperty("uiFont", QGuiApplication::font());
    return QObject::eventFilter(watched, event);
}
void ApplicationHost::setupTray(bool noTray) {
    if (noTray || !QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    trayMenu_ = std::make_unique<QMenu>();
    trayOpenAction_ =
        trayMenu_->addAction(tr("Open QuickClash"), this, [this] { (void)showWindow(); });
    trayStatusAction_ = trayMenu_->addAction(tr("Waiting for a valid profile"));
    trayStatusAction_->setEnabled(false);
    connect(&state_, &presentation::UiState::runtimeChanged, this,
            &ApplicationHost::updateTrayStatus);
    trayProfileAction_ = trayMenu_->addAction(QString{});
    trayProfileAction_->setEnabled(false);
    trayOutboundAction_ = trayMenu_->addAction(QString{});
    trayOutboundAction_->setEnabled(false);
    trayModeMenu_ = trayMenu_->addMenu(tr("Outbound mode"));
    auto* modeGroup = new QActionGroup(trayModeMenu_);
    const std::array<QString, 3> modes{QStringLiteral("rule"), QStringLiteral("global"),
                                       QStringLiteral("direct")};
    for (std::size_t i = 0; i < modes.size(); ++i) {
        const auto& mode = modes[i];
        auto* action = trayModeMenu_->addAction(modeLabel(mode));
        action->setObjectName(QStringLiteral("tray_mode_") + mode);
        action->setData(mode);
        action->setCheckable(true);
        modeGroup->addAction(action);
        trayModeActions_[i] = action;
        connect(action, &QAction::triggered, this, [this, mode] {
            state_.runtimeCommand(QStringLiteral("mode"), {mode});
            // A request is not confirmation. Keep the last published mode until it succeeds.
            updateTrayStatus();
        });
    }
    traySystemProxyAction_ = trayMenu_->addAction(tr("System proxy"));
    trayTunAction_ = trayMenu_->addAction(tr("Virtual network adapter (TUN)"));
    traySystemProxyAction_->setCheckable(true);
    trayTunAction_->setCheckable(true);
    connect(traySystemProxyAction_, &QAction::triggered, this, [this](bool checked) {
        state_.runtimeCommand(QStringLiteral("system_proxy"),
                              {checked ? QStringLiteral("true") : QStringLiteral("false")});
        updateTrayStatus();
    });
    connect(trayTunAction_, &QAction::triggered, this, [this](bool checked) {
        state_.runtimeCommand(QStringLiteral("tun"),
                              {checked ? QStringLiteral("true") : QStringLiteral("false")});
        updateTrayStatus();
    });
    trayMenu_->addSeparator();
    traySettingsAction_ = trayMenu_->addAction(tr("Settings"), this, [this] {
        (void)state_.navigate(QStringLiteral("settings"));
        (void)showWindow();
    });
    trayQuitAction_ = trayMenu_->addAction(tr("Quit"), this, &ApplicationHost::quit);
    tray_ = std::make_unique<QSystemTrayIcon>();
    updateTrayIcon();
    tray_->setToolTip(QStringLiteral("QuickClash"));
    tray_->setContextMenu(trayMenu_.get());
    connect(tray_.get(), &QSystemTrayIcon::messageClicked, this, [this] {
        if (!alertPage_.isEmpty())
            (void)state_.navigate(alertPage_);
        (void)showWindow();
    });
    connect(tray_.get(), &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    (void)showWindow();
                }
            });
    tray_->show();
    state_.setTrayAvailable(true);
    updateTrayStatus();
}
void ApplicationHost::setLanguageController(presentation::Localization* localization) {
    if (!localization)
        return;
    localization_ = localization;
    connect(&state_, &presentation::UiState::languageModeChanged, this,
            &ApplicationHost::applyLanguageMode, Qt::UniqueConnection);
    applyLanguageMode(state_.languageMode());
}
void ApplicationHost::applyLanguageMode(const QString& mode) {
    if (!localization_)
        return;
    (void)localization_->loadPreference(mode);
    state_.retranslateUi();
    if (engine_)
        engine_->retranslate();
    retranslateTray();
}
void ApplicationHost::retranslateTray() {
    if (!trayMenu_)
        return;
    if (trayOpenAction_)
        trayOpenAction_->setText(tr("Open QuickClash"));
    if (traySystemProxyAction_)
        traySystemProxyAction_->setText(tr("System proxy"));
    if (trayTunAction_)
        trayTunAction_->setText(tr("Virtual network adapter (TUN)"));
    if (traySettingsAction_)
        traySettingsAction_->setText(tr("Settings"));
    if (trayQuitAction_)
        trayQuitAction_->setText(tr("Quit"));
    for (auto* action : trayModeActions_)
        if (action)
            action->setText(modeLabel(action->data().toString()));
    updateTrayStatus();
}
void ApplicationHost::updateTrayStatus() {
    if (!trayStatusAction_)
        return;
    const auto runtime = state_.runtime();
    const auto settings = runtime.value(QStringLiteral("settingsState")).toMap();
    const auto preferences = runtime.value(QStringLiteral("preferences")).toMap();
    const auto mode = (settings.value(QStringLiteral("running")).toBool()
                           ? settings.value(QStringLiteral("effective")).toMap()
                           : preferences)
                          .value(QStringLiteral("mode"))
                          .toString();
    const bool enabled = !runtime.value(QStringLiteral("selectedId")).toString().isEmpty() &&
                         !runtime.value(QStringLiteral("busy")).toBool() &&
                         !runtime.value(QStringLiteral("readOnly")).toBool();
    const auto capabilities = runtime.value(QStringLiteral("capabilities")).toMap();
    traySystemProxyAction_->setEnabled(enabled &&
                                       capabilities.value(QStringLiteral("systemProxy")) !=
                                           QStringLiteral("Unsupported"));
    trayTunAction_->setEnabled(enabled && capabilities.value(QStringLiteral("tun")) !=
                                              QStringLiteral("Unsupported"));
    traySystemProxyAction_->setChecked(runtime.value(QStringLiteral("systemProxy")).toBool());
    trayTunAction_->setChecked(runtime.value(QStringLiteral("tunState")) == QStringLiteral("On"));
    traySystemProxyAction_->setText(
        preferences.value(QStringLiteral("systemProxyDesired")).toBool() &&
                !traySystemProxyAction_->isChecked()
            ? tr("System proxy (requested, inactive)")
            : tr("System proxy"));
    trayTunAction_->setText(preferences.value(QStringLiteral("tunDesired")).toBool() &&
                                    !trayTunAction_->isChecked()
                                ? tr("Virtual network adapter (TUN, requested, inactive)")
                                : tr("Virtual network adapter (TUN)"));
    const bool attention = runtime.value(QStringLiteral("readOnly")).toBool() ||
                           runtime.value(QStringLiteral("proxyConflict")).toBool() ||
                           runtime.value(QStringLiteral("tunState")) == QStringLiteral("Uncertain");
    const auto status = attention ? tr("Network needs attention")
                        : runtime.value(QStringLiteral("ready")).toBool()
                            ? tr("Network engine ready")
                        : runtime.value(QStringLiteral("selectedId")).toString().isEmpty()
                            ? tr("Waiting for a valid profile")
                        : !runtime.value(QStringLiteral("error")).toString().isEmpty()
                            ? tr("Network engine unavailable")
                            : tr("Network engine starting or recovering");
    trayStatusAction_->setText(status);
    trayModeMenu_->setTitle(tr("Mode: %1").arg(modeLabel(mode)));
    trayModeMenu_->setEnabled(enabled);
    for (auto* action : trayModeActions_)
        action->setChecked(action->data().toString() == mode);
    const auto profile = runtime.value(QStringLiteral("selectedName")).toString();
    const auto outbound = runtime.value(QStringLiteral("outbound")).toMap();
    const auto node = !runtime.value(QStringLiteral("ready")).toBool() ? tr("Unavailable")
                      : !outbound.value(QStringLiteral("confirmed")).toBool()
                          ? tr("Awaiting confirmation")
                      : outbound.value(QStringLiteral("balanced")).toBool() ? tr("Load balancing")
                      : outbound.value(QStringLiteral("node")).toString().isEmpty()
                          ? tr("Awaiting confirmation")
                          : outbound.value(QStringLiteral("node")).toString();
    const auto profileText =
        tr("Profile: %1").arg(trayLabel(profile.isEmpty() ? tr("None") : profile));
    const auto outboundText = tr("Outbound: %1").arg(trayLabel(node));
    trayProfileAction_->setText(QString(profileText).replace("&", "&&"));
    trayOutboundAction_->setText(QString(outboundText).replace("&", "&&"));
    if (tray_) {
        // Windows has a small native tooltip budget; the menu keeps the longer bounded labels.
        tray_->setToolTip(
            (QStringLiteral("QuickClash · ") + status + QLatin1Char('\n') +
             tr("Mode: %1").arg(modeLabel(mode)) + QLatin1Char('\n') +
             tr("Profile: %1").arg(trayLabel(profile.isEmpty() ? tr("None") : profile, 24)) +
             QLatin1Char('\n') + tr("Outbound: %1").arg(trayLabel(node, 24)))
                .left(127));
    }
}
void ApplicationHost::updateCriticalAlerts() {
    QJsonArray profiles;
    auto* model = state_.profileModel();
    for (int i = 0; i < model->count(); ++i)
        profiles.append(model->entryAt(i));
    const auto alerts = criticalAlerts_.update(
        QJsonObject::fromVariantMap(state_.runtime()), profiles,
        tray_ && recoverableFromTray() && QSystemTrayIcon::supportsMessages() &&
            !state_.uiVisible() && !quitting_);
    if (alerts.isEmpty())
        return;
    using Kind = presentation::CriticalAlerts::Kind;
    QStringList messages;
    bool network = false;
    for (const auto kind : alerts) {
        switch (kind) {
        case Kind::TunCleanup:
            network = true;
            messages.append(tr("TUN cleanup needs verification. Check network settings."));
            break;
        case Kind::RecoveryStopped:
            network = true;
            messages.append(tr("Automatic network recovery stopped. Check network settings."));
            break;
        case Kind::ProxyConflict:
            network = true;
            messages.append(
                tr("Another application changed the system proxy. Review before taking over."));
            break;
        case Kind::SubscriptionExpired:
            messages.append(tr("A subscription has expired."));
            break;
        case Kind::QuotaExhausted:
            messages.append(tr("A subscription has used all its traffic allowance."));
            break;
        case Kind::UpdateFailed:
            messages.append(
                tr("A subscription update failed. The previous configuration was retained."));
            break;
        case Kind::QuotaLow:
            messages.append(tr("A subscription has 10% or less traffic remaining."));
            break;
        case Kind::SubscriptionExpiring:
            messages.append(tr("A subscription expires within 7 days."));
            break;
        }
    }
    if (messages.join(QLatin1Char('\n')).size() > 240) {
        messages.clear();
        if (network)
            messages.append(
                tr("Check network settings for recovery, proxy conflicts or TUN cleanup."));
        if (std::any_of(alerts.cbegin(), alerts.cend(),
                        [](Kind kind) { return kind >= Kind::SubscriptionExpired; }))
            messages.append(
                tr("Check subscriptions for failed updates, traffic allowance and expiry."));
    }
    alertPage_ = network ? QStringLiteral("settings") : QStringLiteral("subscriptions");
    tray_->showMessage(network ? tr("Network needs attention") : tr("Subscription needs attention"),
                       messages.join(QLatin1Char('\n')), QSystemTrayIcon::Warning);
}
void ApplicationHost::updateTrayIcon() {
    if (tray_) {
        tray_->setIcon(QIcon(darkTrayBackground()
                                 ? QStringLiteral(":/icons/brand/qcat_tray_light.svg")
                                 : QStringLiteral(":/icons/brand/qcat_tray_dark.svg")));
    }
}
bool ApplicationHost::startAtLogin() {
    if (!state_.settingsReady() && state_.storageError().isEmpty()) {
        loginPending_ = true;
        return true;
    }
    const auto startup = state_.runtime().value(QStringLiteral("startup")).toMap();
    if (state_.settingsReady() && startup.value(QStringLiteral("hidden"), true).toBool() &&
        recoverableFromTray())
        return true;
    return showWindow();
}
bool ApplicationHost::showWindow() {
    loginPending_ = false;
    deferredUnload_.cancel();
    if (quitting_)
        return false;
    if (unloading_) {
        restorePending_ = true;
        return true;
    }
    if (showing_)
        return true;
    QScopedValueRollback<bool> showing(showing_, true);
    if (!state_.settingsReady() && state_.storageError().isEmpty()) {
        waitingForSettings_ = true;
        return true;
    }
    if (window_ && window_->isVisible() && window_->visibility() != QWindow::Minimized) {
        window_->raise();
        window_->requestActivate();
        return true;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    if (!engine_) {
        state_.setHostState(QStringLiteral("Loading"));
        engine_ = std::make_unique<QQmlApplicationEngine>();
        connect(engine_.get(), &QQmlEngine::warnings, this,
                [this](const QList<QQmlError>& warnings) {
                    qmlErrorCount_ += static_cast<int>(warnings.size());
                });
        engine_->setInitialProperties({{QStringLiteral("ui"), QVariant::fromValue(&state_)},
                                       {QStringLiteral("uiFont"), QGuiApplication::font()}});
        engine_->loadFromModule("QuickClash.Ui", "Main");
        if (engine_->rootObjects().size() != 1) {
            engine_.reset();
            state_.setHostState(QStringLiteral("Unloaded"));
            return false;
        }
        window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
        if (!window_) {
            engine_.reset();
            state_.setHostState(QStringLiteral("Unloaded"));
            return false;
        }
        ++generation_;
        window_->setIcon(QApplication::windowIcon());
        window_->setPersistentGraphics(false);
        window_->setPersistentSceneGraph(false);
        if (geometry_.isValid()) {
            window_->setGeometry(geometry_);
        } else if (requestedSize_.isValid()) {
            window_->resize(requestedSize_);
            requestedSize_ = {};
        } else {
            centerInitialWindow();
        }
        connect(window_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
            const bool visible = visibility != QWindow::Hidden && visibility != QWindow::Minimized;
            state_.setUiVisible(visible, visibility == QWindow::Minimized);
            if (!unloading_ && !quitting_) {
                state_.setHostState(
                    visibility == QWindow::Minimized
                        ? QStringLiteral("Minimized")
                        : (visible ? QStringLiteral("Visible") : QStringLiteral("Hidden")));
            }
        });
    }
    state_.setUiVisible(true);
    state_.setHostState(QStringLiteral("Visible"));
    if (requestedSize_.isValid()) {
        window_->resize(requestedSize_);
        requestedSize_ = {};
    }
    window_->showNormal();
    window_->raise();
    window_->requestActivate();
    openMilliseconds_ = elapsed.elapsed();
    return true;
}
void ApplicationHost::centerInitialWindow() {
    // First display centers the default 1000x640 logical window in the
    // available area of the screen under the cursor; a geometry saved by
    // hide/unload restores instead and takes precedence over this default.
    const QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = window_->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect available = screen->availableGeometry();
    const int x = available.x() + qMax(0, (available.width() - window_->width()) / 2);
    const int y = available.y() + qMax(0, (available.height() - window_->height()) / 2);
    window_->setPosition(x, y);
}
void ApplicationHost::resizeWindow(const QSize& size) {
    if (window_)
        window_->resize(size);
    else
        requestedSize_ = size;
}
bool ApplicationHost::contentReady() const {
    return window_ && window_->property("pageReady").toBool() && qmlErrorCount_ == 0;
}
void ApplicationHost::hideWindow(bool confirmed) {
    if (!window_ || quitting_) {
        return;
    }
    const bool available = recoverableFromTray();
    state_.setTrayAvailable(available);
    if (!available || (!confirmed && !state_.closeAcknowledged())) {
        emit state_.closeConfirmationRequired();
        return;
    }
    if (confirmed) {
        state_.acknowledgeClose();
    }
    geometry_ = window_->geometry();
    state_.setUiVisible(false);
    window_->hide();
    state_.setHostState(QStringLiteral("Hidden"));
    updateDeferredUnload();
}
bool ApplicationHost::recoverableFromTray() const {
    return tray_ && tray_->isVisible() && QSystemTrayIcon::isSystemTrayAvailable();
}
bool ApplicationHost::hiddenWindow() const {
    return window_ && window_->visibility() == QWindow::Hidden;
}
void ApplicationHost::updateDeferredUnload() {
    const bool hidden = !quitting_ && !showing_ && !unloading_ && engine_ &&
                        state_.hostState() == QStringLiteral("Hidden") && hiddenWindow();
    const bool trayAvailable = recoverableFromTray();
    const bool trayLost = hidden && state_.trayAvailable() && !trayAvailable;
    const bool shouldUnload =
        hidden && trayAvailable && state_.settingsReady() && state_.unloadWhenHidden();
    deferredUnload_.update(trayLost || shouldUnload, static_cast<quint64>(generation_));
}
void ApplicationHost::unloadWindow() {
    deferredUnload_.cancel();
    if (!engine_ || unloading_) {
        return;
    }
    unloading_ = true;
    state_.setHostState(QStringLiteral("Unloading"));
    state_.setUiVisible(false);
    if (window_) {
        geometry_ = window_->geometry();
        window_->hide();
        window_->releaseResources();
    }
    window_.clear();
    engine_.reset();
    unloading_ = false;
    state_.setHostState(QStringLiteral("Unloaded"));
    if (std::exchange(restorePending_, false) && !quitting_)
        QTimer::singleShot(0, this, [this] { (void)showWindow(); });
}
void ApplicationHost::quit() {
    if (quitting_) {
        return;
    }
    if (state_.hasSettingsDraft()) {
        if (showWindow())
            emit state_.settingsQuitConfirmationRequired();
        return;
    }
    quitting_ = true;
    restorePending_ = false;
    deferredUnload_.cancel();
    unloadWindow();
    QCoreApplication::quit();
}
} // namespace quickclash
