#pragma once
#include "deferred_ui_unload.h"
#include "presentation/ui_state.h"
#include "system_events.h"
#include <QElapsedTimer>
#include <QMenu>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRect>
#include <QSize>
#include <QSystemTrayIcon>
#include <QTimer>
#include <memory>

namespace quickclash::presentation {
class Localization;
}

namespace quickclash {
class ApplicationHost final : public QObject {
    Q_OBJECT
  public:
    explicit ApplicationHost(QString settingsFile, bool noTray, QObject* parent = nullptr);
    ~ApplicationHost() override;
    [[nodiscard]] presentation::UiState& state() {
        return state_;
    }
    [[nodiscard]] bool showWindow();
    [[nodiscard]] bool startAtLogin();
    void setLanguageController(presentation::Localization* localization);
    void resizeWindow(const QSize& size);
    void hideWindow(bool confirmed);
    void unloadWindow();
    void quit();
    [[nodiscard]] int engineGeneration() const {
        return generation_;
    }
    [[nodiscard]] QQuickWindow* window() const {
        return window_;
    }
    [[nodiscard]] qint64 lastOpenMilliseconds() const {
        return openMilliseconds_;
    }
    [[nodiscard]] bool contentReady() const;
    [[nodiscard]] int qmlErrorCount() const {
        return qmlErrorCount_;
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    // The native probe simulates loss of this application's tray icon.
    friend int runTrayProbe(ApplicationHost& host);
    void setupTray(bool noTray);
    void applyLanguageMode(const QString& mode);
    void retranslateTray();
    void updateTrayStatus();
    void updateTrayIcon();
    [[nodiscard]] bool recoverableFromTray() const;
    [[nodiscard]] bool hiddenWindow() const;
    void updateDeferredUnload();
    void centerInitialWindow();
    presentation::UiState state_;
    presentation::Localization* localization_ = nullptr;
    SystemEvents systemEvents_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QPointer<QQuickWindow> window_;
    std::unique_ptr<QMenu> trayMenu_;
    std::unique_ptr<QSystemTrayIcon> tray_;
    QAction* trayOpenAction_ = nullptr;
    QAction* trayStatusAction_ = nullptr;
    QAction* traySystemProxyAction_ = nullptr;
    QAction* trayTunAction_ = nullptr;
    QAction* traySettingsAction_ = nullptr;
    QAction* trayQuitAction_ = nullptr;
    DeferredUiUnload deferredUnload_;
    QRect geometry_;
    QSize requestedSize_;
    bool quitting_ = false;
    bool loginPending_ = false;
    bool waitingForSettings_ = false;
    bool showing_ = false;
    bool unloading_ = false;
    bool restorePending_ = false;
    int generation_ = 0;
    qint64 openMilliseconds_ = 0;
    int qmlErrorCount_ = 0;
};
} // namespace quickclash
