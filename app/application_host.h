#pragma once
#include "presentation/ui_state.h"
#include <QElapsedTimer>
#include <QMenu>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRect>
#include <QSystemTrayIcon>
#include <QTimer>
#include <memory>

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

  private:
    void setupTray(bool noTray);
    presentation::UiState state_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QPointer<QQuickWindow> window_;
    std::unique_ptr<QMenu> trayMenu_;
    std::unique_ptr<QSystemTrayIcon> tray_;
    QTimer unloadTimer_;
    QRect geometry_;
    bool quitting_ = false;
    int generation_ = 0;
    qint64 openMilliseconds_ = 0;
    int qmlErrorCount_ = 0;
};
} // namespace quickclash
