#include "application_host.h"
#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QQmlEngine>
#include <QQmlError>
#include <QStyleHints>
#include <utility>

namespace quickclash {
ApplicationHost::ApplicationHost(QString settingsFile, bool noTray, QObject* parent)
    : QObject(parent), state_(std::move(settingsFile)) {
    QQmlEngine::setObjectOwnership(&state_, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(state_.proxyModel(), QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(state_.connectionModel(), QQmlEngine::CppOwnership);
    auto* hints = QGuiApplication::styleHints();
    state_.setSystemDark(hints->colorScheme() == Qt::ColorScheme::Dark);
    connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
        state_.setSystemDark(scheme == Qt::ColorScheme::Dark);
    });
    setupTray(noTray);
    connect(&systemEvents_, &SystemEvents::reconcileRequested, this,
            [this] { state_.runtimeCommand(QStringLiteral("reconcile")); });
    connect(&state_, &presentation::UiState::storageChanged, this, [this] {
        if (waitingForSettings_ && (state_.settingsReady() || !state_.storageError().isEmpty())) {
            waitingForSettings_ = false;
            (void)showWindow();
        }
    });
    unloadTimer_.setSingleShot(true);
    unloadTimer_.setInterval(5000);
    connect(&unloadTimer_, &QTimer::timeout, this, &ApplicationHost::unloadWindow);
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
            if (state_.trayAvailable()) {
                unloadWindow();
            }
        },
        Qt::QueuedConnection);
    connect(&state_, &presentation::UiState::hostChanged, this, [this] {
        if (!state_.unloadWhenHidden() || state_.hostState() != QStringLiteral("Hidden")) {
            unloadTimer_.stop();
        } else if (!unloadTimer_.isActive()) {
            unloadTimer_.start();
        }
    });
}
ApplicationHost::~ApplicationHost() {
    state_.setUiVisible(false);
    engine_.reset();
}
void ApplicationHost::setupTray(bool noTray) {
    if (noTray || !QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#1877f2")));
    painter.drawRoundedRect(1, 1, 30, 30, 10, 10);
    painter.setPen(Qt::white);
    auto font = painter.font();
    font.setPixelSize(21);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("Q"));
    painter.end();
    trayMenu_ = std::make_unique<QMenu>();
    trayMenu_->addAction(tr("Open QuickClash"), this, [this] { (void)showWindow(); });
    auto* status = trayMenu_->addAction(tr("Core stopped"));
    status->setEnabled(false);
    connect(&state_, &presentation::UiState::runtimeChanged, status, [this, status] {
        status->setText(state_.runtime().value(QStringLiteral("ready")).toBool()
                            ? tr("Mihomo ready")
                            : tr("Core stopped"));
    });
    trayMenu_->addAction(tr("Start proxy"), this,
                         [this] { state_.runtimeCommand(QStringLiteral("start_proxy")); });
    trayMenu_->addAction(tr("Stop and restore proxy"), this,
                         [this] { state_.runtimeCommand(QStringLiteral("stop")); });
    trayMenu_->addSeparator();
    trayMenu_->addAction(tr("Settings"), this, [this] {
        (void)state_.navigate(QStringLiteral("settings"));
        (void)showWindow();
    });
    trayMenu_->addAction(tr("Quit"), this, &ApplicationHost::quit);
    tray_ = std::make_unique<QSystemTrayIcon>(QIcon(pixmap));
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
bool ApplicationHost::showWindow() {
    if (!state_.settingsReady() && state_.storageError().isEmpty()) {
        waitingForSettings_ = true;
        return true;
    }
    if (quitting_) {
        return false;
    }
    unloadTimer_.stop();
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
        engine_->setInitialProperties(
            {{QStringLiteral("ui"), QVariant::fromValue(&state_)},
             {QStringLiteral("uiFontFamily"), QGuiApplication::font().family()}});
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
        window_->setPersistentGraphics(false);
        window_->setPersistentSceneGraph(false);
        if (geometry_.isValid()) {
            window_->setGeometry(geometry_);
        }
        connect(window_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
            const bool visible = visibility != QWindow::Hidden && visibility != QWindow::Minimized;
            state_.setUiVisible(visible);
            if (state_.hostState() != QStringLiteral("Unloading")) {
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
    const bool available = tray_ && QSystemTrayIcon::isSystemTrayAvailable();
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
    if (state_.unloadWhenHidden()) {
        unloadTimer_.start();
    }
}
void ApplicationHost::unloadWindow() {
    unloadTimer_.stop();
    if (!engine_) {
        return;
    }
    state_.setHostState(QStringLiteral("Unloading"));
    state_.setUiVisible(false);
    if (window_) {
        geometry_ = window_->geometry();
        window_->hide();
        window_->releaseResources();
    }
    window_.clear();
    engine_.reset();
    state_.setHostState(QStringLiteral("Unloaded"));
}
void ApplicationHost::quit() {
    if (quitting_) {
        return;
    }
    quitting_ = true;
    unloadWindow();
    QCoreApplication::quit();
}
} // namespace quickclash
