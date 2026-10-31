#include "application_host.h"
#include "ui_font.h"
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QIcon>
#include <QQmlEngine>
#include <QQmlError>
#include <QScopedValueRollback>
#include <QScreen>
#include <QStyleHints>
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
        if (waitingForSettings_ && (state_.settingsReady() || !state_.storageError().isEmpty())) {
            waitingForSettings_ = false;
            (void)showWindow();
        }
    });
    connect(&deferredUnload_, &DeferredUiUnload::due, this, [this](quint64 generation) {
        if (generation != static_cast<quint64>(generation_) || !window_ || !backgroundWindow() ||
            !state_.unloadWhenHidden() || !state_.settingsReady() || quitting_ || unloading_ ||
            showing_)
            return;
        // Capability may disappear during the delay. Never destroy the only recovery surface.
        if (!recoverableFromTray()) {
            state_.setTrayAvailable(false);
            if (window_->visibility() == QWindow::Hidden)
                (void)showWindow();
            return;
        }
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
    trayMenu_->addAction(tr("Open QuickClash"), this, [this] { (void)showWindow(); });
    auto* status = trayMenu_->addAction(tr("Waiting for a valid profile"));
    status->setEnabled(false);
    connect(&state_, &presentation::UiState::runtimeChanged, status, [this, status] {
        const auto runtime = state_.runtime();
        status->setText(runtime.value(QStringLiteral("ready")).toBool() ? tr("Network engine ready")
                        : runtime.value(QStringLiteral("selectedId")).toString().isEmpty()
                            ? tr("Waiting for a valid profile")
                            : tr("Network engine starting or recovering"));
    });
    auto* systemProxy = trayMenu_->addAction(tr("System proxy"));
    auto* tun = trayMenu_->addAction(tr("Virtual network adapter (TUN)"));
    systemProxy->setCheckable(true);
    tun->setCheckable(true);
    const auto syncNetwork = [this, systemProxy, tun] {
        const auto runtime = state_.runtime();
        const auto preferences = runtime.value(QStringLiteral("preferences")).toMap();
        const bool enabled = !runtime.value(QStringLiteral("selectedId")).toString().isEmpty() &&
                             !runtime.value(QStringLiteral("busy")).toBool() &&
                             !runtime.value(QStringLiteral("readOnly")).toBool();
        const auto capabilities = runtime.value(QStringLiteral("capabilities")).toMap();
        systemProxy->setEnabled(enabled && capabilities.value(QStringLiteral("systemProxy")) !=
                                               QStringLiteral("Unsupported"));
        tun->setEnabled(enabled &&
                        capabilities.value(QStringLiteral("tun")) != QStringLiteral("Unsupported"));
        systemProxy->setChecked(runtime.value(QStringLiteral("systemProxy")).toBool() ||
                                preferences.value(QStringLiteral("systemProxyDesired")).toBool());
        tun->setChecked(runtime.value(QStringLiteral("tunState")).toString() ==
                            QStringLiteral("On") ||
                        preferences.value(QStringLiteral("tunDesired")).toBool());
    };
    connect(&state_, &presentation::UiState::runtimeChanged, systemProxy, syncNetwork);
    connect(systemProxy, &QAction::triggered, this, [this](bool checked) {
        state_.runtimeCommand(QStringLiteral("system_proxy"),
                              {checked ? QStringLiteral("true") : QStringLiteral("false")});
    });
    connect(tun, &QAction::triggered, this, [this](bool checked) {
        state_.runtimeCommand(QStringLiteral("tun"),
                              {checked ? QStringLiteral("true") : QStringLiteral("false")});
    });
    syncNetwork();
    trayMenu_->addSeparator();
    trayMenu_->addAction(tr("Settings"), this, [this] {
        (void)state_.navigate(QStringLiteral("settings"));
        (void)showWindow();
    });
    trayMenu_->addAction(tr("Quit"), this, &ApplicationHost::quit);
    tray_ = std::make_unique<QSystemTrayIcon>();
    updateTrayIcon();
    tray_->setToolTip(QStringLiteral("QuickClash"));
    tray_->setContextMenu(trayMenu_.get());
    connect(tray_.get(), &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    (void)showWindow();
                }
            });
    tray_->show();
    state_.setTrayAvailable(true);
}
void ApplicationHost::updateTrayIcon() {
    if (tray_) {
        tray_->setIcon(QIcon(darkTrayBackground()
                                 ? QStringLiteral(":/icons/brand/qcat_tray_light.svg")
                                 : QStringLiteral(":/icons/brand/qcat_tray_dark.svg")));
    }
}
bool ApplicationHost::showWindow() {
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
bool ApplicationHost::backgroundWindow() const {
    return window_ && (window_->visibility() == QWindow::Hidden ||
                       window_->visibility() == QWindow::Minimized);
}
void ApplicationHost::updateDeferredUnload() {
    const bool hostBackground = state_.hostState() == QStringLiteral("Hidden") ||
                                state_.hostState() == QStringLiteral("Minimized");
    deferredUnload_.update(!quitting_ && !showing_ && !unloading_ && engine_ &&
                               state_.settingsReady() && state_.unloadWhenHidden() &&
                               hostBackground && backgroundWindow() && recoverableFromTray(),
                           static_cast<quint64>(generation_));
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
    quitting_ = true;
    restorePending_ = false;
    deferredUnload_.cancel();
    unloadWindow();
    QCoreApplication::quit();
}
} // namespace quickclash
