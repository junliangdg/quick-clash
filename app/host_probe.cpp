#include "host_probe.h"
#include "application_host.h"
#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <cstdio>

namespace quickclash {
int runTrayProbe(ApplicationHost& host) {
    if (!host.state().trayAvailable()) {
        std::fputs("Tray probe requires an available native desktop tray.\n", stderr);
        return 77;
    }
    int step = 0;
    int confirmations = 0;
    const auto confirmationConnection =
        QObject::connect(&host.state(), &presentation::UiState::closeConfirmationRequired, &host,
                         [&confirmations] { ++confirmations; });
    QTimer timer;
    timer.setInterval(200);
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
        const auto require = [](bool condition, const char* message) {
            if (!condition) {
                std::fprintf(stderr, "Tray probe failed: %s\n", message);
                QCoreApplication::exit(11);
            }
            return condition;
        };
        switch (step++) {
        case 0:
            if (!require(host.contentReady(), "initial content"))
                return;
            host.state().requestClose();
            if (!require(confirmations == 1 && host.window()->isVisible(), "first close confirms"))
                return;
            host.state().requestClose(true);
            if (!require(host.state().closeAcknowledged() && !host.window()->isVisible() &&
                             host.state().activeDemandCount() == 0 && !host.state().replayRunning(),
                         "confirmed close hides and releases consumers"))
                return;
            if (!require(host.showWindow(), "restore from tray host"))
                return;
            break;
        case 1:
            if (!require(host.state().activeDemandCount() > 0, "restored page demand"))
                return;
            host.state().requestClose();
            if (!require(confirmations == 1 && !host.window()->isVisible(), "acknowledged close"))
                return;
            host.state().setUnloadWhenHidden(true);
            timer.setInterval(5500);
            break;
        case 2:
            if (!require(!host.window() && host.state().hostState() == QStringLiteral("Unloaded"),
                         "delayed hidden unload"))
                return;
            if (!require(host.showWindow() && host.engineGeneration() == 2,
                         "tray restore rebuilds once"))
                return;
            timer.setInterval(200);
            break;
        case 3:
            if (!require(host.contentReady() && host.state().activeDemandCount() > 0,
                         "rebuilt content and consumers"))
                return;
            timer.stop();
            std::puts("Native tray host probe passed");
            QCoreApplication::quit();
            break;
        }
    });
    timer.start();
    const int result = QCoreApplication::exec();
    QObject::disconnect(confirmationConnection);
    return result;
}
int runHostProbe(ApplicationHost& host) {
    int step = 0;
    int confirmations = 0;
    QPointer<QQuickWindow> previousWindow;
    const auto confirmationConnection =
        QObject::connect(&host.state(), &presentation::UiState::closeConfirmationRequired, &host,
                         [&confirmations] { ++confirmations; });
    QTimer timer;
    timer.setInterval(100);
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
        const auto require = [](bool condition, const char* message) {
            if (!condition) {
                std::fprintf(stderr, "Host probe failed: %s\n", message);
                QCoreApplication::exit(10);
            }
            return condition;
        };
        switch (step++) {
        case 0:
            if (!require(host.contentReady() && !host.state().trayAvailable(),
                         "initial no-tray page"))
                return;
            host.state().requestClose();
            if (!require(confirmations == 1 && host.window()->isVisible(), "close remains visible"))
                return;
            host.state().requestClose(true);
            if (!require(confirmations == 2 && host.window()->isVisible() &&
                             !host.state().closeAcknowledged(),
                         "no tray cannot acknowledge hide"))
                return;
            host.window()->resize(720, 560);
            break;
        case 1:
            if (!require(host.window()->property("compact").toBool(), "compact desktop layout"))
                return;
            host.state().requestMinimize();
            break;
        case 2:
            if (!require(host.state().hostState() == QStringLiteral("Minimized") &&
                             host.state().activeDemandCount() == 0 && !host.state().replayRunning(),
                         "minimize releases demand"))
                return;
            if (!require(host.showWindow(), "restore minimized window"))
                return;
            break;
        case 3:
            if (!require(host.state().uiVisible() && host.state().activeDemandCount() > 0 &&
                             host.state().replayRunning(),
                         "show reacquires page demand"))
                return;
            if (!require(host.state().navigate(QStringLiteral("connections")), "connections route"))
                return;
            host.state().setFilter(QStringLiteral("connections"), QStringLiteral("app_2"));
            previousWindow = host.window();
            host.unloadWindow();
            if (!require(previousWindow.isNull() && !host.window() &&
                             host.state().activeDemandCount() == 0 && !host.state().replayRunning(),
                         "engine and window destroyed synchronously"))
                return;
            if (!require(host.showWindow(), "rebuild window"))
                return;
            break;
        case 4:
            if (!require(host.contentReady() && host.window()->width() == 720 &&
                             host.state().currentPage() == QStringLiteral("connections") &&
                             host.state()
                                     .readState(QStringLiteral("connections"))
                                     .value(QStringLiteral("filter"))
                                     .toString() == QStringLiteral("app_2") &&
                             host.state().activeDemandCount() == 1,
                         "restore geometry route filter and demand"))
                return;
            // Hidden scenario must stop both the timer and page consumers.
            host.window()->hide();
            break;
        case 5:
            if (!require(host.state().hostState() == QStringLiteral("Hidden") &&
                             host.state().activeDemandCount() == 0 && !host.state().replayRunning(),
                         "hidden lifecycle"))
                return;
            host.unloadWindow();
            std::puts("Host lifecycle probe passed");
            timer.stop();
            QCoreApplication::quit();
            break;
        }
    });
    timer.start();
    const int result = QCoreApplication::exec();
    QObject::disconnect(confirmationConnection);
    return result;
}
} // namespace quickclash
