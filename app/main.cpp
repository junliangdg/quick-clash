#include "application_host.h"
#include "host_probe.h"
#include "single_instance.h"
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include <memory>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // Qt 6.8/D3D11 threaded teardown grows handles in the reference environment.
    // The basic render loop retains the native GPU backend and passed 100 rebuilds.
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", "basic");
    }
#endif
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QuickClash"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    auto uiFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef Q_OS_WIN
    // The UI alias resolves to Arial on the reference system. Use the installed
    // family directly; loading the full TTC as an application font copies it.
    if (QFontDatabase::hasFamily(QStringLiteral("Microsoft YaHei"))) {
        uiFont.setFamily(QStringLiteral("Microsoft YaHei"));
        uiFont.setStyleName(QString{});
        uiFont.setStyleHint(QFont::SansSerif);
    }
#endif
    QApplication::setFont(uiFont);
    const auto arguments = app.arguments();
    const bool smoke = arguments.contains(QStringLiteral("--smoke-test"));
    const bool prototype = arguments.contains(QStringLiteral("--prototype-test"));
    const bool hostTest = arguments.contains(QStringLiteral("--host-test"));
    const bool trayTest = arguments.contains(QStringLiteral("--tray-test"));
    const bool keyboardTest = arguments.contains(QStringLiteral("--keyboard-test"));
    const bool isolated = smoke || prototype || hostTest || trayTest || keyboardTest ||
                          arguments.contains(QStringLiteral("--isolated"));
    const auto option = [&arguments](const QString& key) {
        const auto prefix = key + QLatin1Char('=');
        for (const auto& argument : arguments) {
            if (argument.startsWith(prefix)) {
                return argument.mid(prefix.size());
            }
        }
        return QString{};
    };
    const auto dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto configDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    quickclash::SingleInstance instance(dataDirectory);
    if (!isolated) {
        switch (instance.start()) {
        case quickclash::SingleInstance::Result::Activated:
            return 0;
        case quickclash::SingleInstance::Result::Unavailable:
            std::fputs("QuickClash instance lock/activation unavailable.\n", stderr);
            return 2;
        case quickclash::SingleInstance::Result::Primary:
            break;
        }
        if (!QDir().mkpath(configDirectory)) {
            return 3;
        }
    }
    quickclash::ApplicationHost host(
        isolated ? QString{} : QDir(configDirectory).filePath(QStringLiteral("ui.ini")),
        (isolated && !trayTest) || arguments.contains(QStringLiteral("--no-tray")));
    QObject::connect(&instance, &quickclash::SingleInstance::activationRequested, &host,
                     [&host] { (void)host.showWindow(); });
    const auto replayConnections = option(QStringLiteral("--replay-connections"));
    if (!replayConnections.isEmpty() &&
        !host.state().setReplayConnections(replayConnections.toInt())) {
        std::fputs("Replay connections must be 1000 or 10000.\n", stderr);
        return 9;
    }
    host.state().setReplay(arguments.contains(QStringLiteral("--replay")) || prototype ||
                           hostTest || trayTest);
    if (!option(QStringLiteral("--theme")).isEmpty()) {
        host.state().setThemeMode(option(QStringLiteral("--theme")));
    }
    if (!option(QStringLiteral("--page")).isEmpty() &&
        !host.state().navigate(option(QStringLiteral("--page")))) {
        return 4;
    }
    if (!host.showWindow()) {
        return 1;
    }
    const auto windowSize = option(QStringLiteral("--window-size"));
    if (isolated && !windowSize.isEmpty()) {
        const auto dimensions = windowSize.split(QLatin1Char('x'));
        if (dimensions.size() != 2 || dimensions[0].toInt() < 720 || dimensions[1].toInt() < 560) {
            return 13;
        }
        host.window()->resize(dimensions[0].toInt(), dimensions[1].toInt());
    }
    if (hostTest) {
        return quickclash::runHostProbe(host);
    }
    if (trayTest) {
        return quickclash::runTrayProbe(host);
    }
#ifdef QUICKCLASH_TEST_PROBES
    if (keyboardTest) {
        return quickclash::runKeyboardProbe(host);
    }
#else
    if (keyboardTest) {
        return 14;
    }
#endif
    const auto capture = option(QStringLiteral("--capture"));
    if (isolated && !capture.isEmpty()) {
        QTimer::singleShot(1000, &host, [&host, capture] {
            if (!host.contentReady() || !host.window()->grabWindow().save(capture)) {
                QCoreApplication::exit(12);
            }
        });
    }
    if (smoke) {
        QTimer::singleShot(300, &host,
                           [&host] { QCoreApplication::exit(host.contentReady() ? 0 : 8); });
    }
    const auto quitAfter = option(QStringLiteral("--quit-after-ms")).toInt();
    if (quitAfter > 0) {
        QTimer::singleShot(quitAfter, &app, &QCoreApplication::quit);
    }
    // Deterministic resource scenarios; hidden/unloaded windows intentionally have no tray in
    // isolated runs.
    const auto scenario = option(QStringLiteral("--scenario"));
    if (scenario == QStringLiteral("hidden") || scenario == QStringLiteral("unloaded")) {
        QTimer::singleShot(1500, &host, [&host, scenario] {
            host.state().setUiVisible(false);
            if (host.window()) {
                host.window()->hide();
            }
            host.state().setHostState(QStringLiteral("Hidden"));
            if (scenario == QStringLiteral("unloaded")) {
                (void)host.state().navigate(QStringLiteral("home"));
                host.unloadWindow();
            }
        });
    }
    const auto diagnostics = option(QStringLiteral("--diagnostics"));
    if (!diagnostics.isEmpty()) {
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &host, [&host, diagnostics] {
            const auto& state = host.state();
            QSaveFile output(diagnostics);
            if (!output.open(QIODevice::WriteOnly)) {
                QCoreApplication::exit(5);
                return;
            }
            const QJsonObject snapshot{
                {QStringLiteral("host_state"), state.hostState()},
                {QStringLiteral("engine_generation"), host.engineGeneration()},
                {QStringLiteral("active_demands"), state.activeDemandCount()},
                {QStringLiteral("active_streams"), state.activeStreamCount()},
                {QStringLiteral("replay_running"), state.replayRunning()},
                {QStringLiteral("page_ready"), host.contentReady()},
                {QStringLiteral("qml_errors"), host.qmlErrorCount()},
                {QStringLiteral("open_ms"), host.lastOpenMilliseconds()},
                {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
                {QStringLiteral("font_family"), QGuiApplication::font().family()},
                {QStringLiteral("resolved_font_family"),
                 QFontInfo(QGuiApplication::font()).family()},
                {QStringLiteral("replay_connections"), state.replayConnections()},
                {QStringLiteral("tray_available"), state.trayAvailable()},
                {QStringLiteral("graphics_api"),
                 host.window() ? static_cast<int>(host.window()->rendererInterface()->graphicsApi())
                               : -1},
                {QStringLiteral("window_width"), host.window() ? host.window()->width() : 0},
                {QStringLiteral("window_height"), host.window() ? host.window()->height() : 0},
                {QStringLiteral("device_pixel_ratio"),
                 host.window() ? host.window()->devicePixelRatio() : 0.0},
                {QStringLiteral("logical_dpi"),
                 QGuiApplication::primaryScreen()
                     ? QGuiApplication::primaryScreen()->logicalDotsPerInch()
                     : 0.0},
                {QStringLiteral("page"), state.currentPage()}};
            const auto payload = QJsonDocument(snapshot).toJson();
            if (output.write(payload) != payload.size() || !output.commit()) {
                QCoreApplication::exit(5);
            }
        });
    }
    if (prototype) {
        int step = 0;
        QJsonArray lifecycleSamples;
        const int initialGeneration = host.engineGeneration();
        auto timer = std::make_unique<QTimer>();
        auto* timerPointer = timer.get();
        timer->setInterval(40);
        QObject::connect(
            timerPointer, &QTimer::timeout, &host,
            [&host, &step, &lifecycleSamples, initialGeneration, timerPointer] {
                const auto entries = host.state().navigation();
                if (!host.contentReady()) {
                    QCoreApplication::exit(8);
                    return;
                }
                if (step < entries.size() * 2) {
                    const int row = step % static_cast<int>(entries.size());
                    host.state().setThemeMode(step < entries.size() ? QStringLiteral("light")
                                                                    : QStringLiteral("dark"));
                    if (!host.state().navigate(
                            entries.at(row).toMap().value(QStringLiteral("pageId")).toString())) {
                        QCoreApplication::exit(6);
                        return;
                    }
                } else if (step < entries.size() * 2 + 100) {
                    if (step == entries.size() * 2) {
                        (void)host.state().navigate(
                            QStringLiteral("connections"),
                            {{QStringLiteral("filter"), QStringLiteral("app_2")}});
                    }
                    const QPointer<QQuickWindow> previousWindow = host.window();
                    host.unloadWindow();
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    if (!previousWindow.isNull() || host.state().activeDemandCount() != 0 ||
                        host.state().replayRunning() || !host.showWindow() ||
                        host.state().currentPage() != QStringLiteral("connections") ||
                        host.state()
                                .readState(QStringLiteral("connections"))
                                .value(QStringLiteral("filter"))
                                .toString() != QStringLiteral("app_2") ||
                        host.engineGeneration() !=
                            initialGeneration + step - static_cast<int>(entries.size()) * 2 + 1) {
                        QCoreApplication::exit(7);
                        return;
                    }
                    QJsonObject sample{
                        {QStringLiteral("cycle"), step - static_cast<int>(entries.size()) * 2 + 1},
                        {QStringLiteral("window_qobjects"),
                         static_cast<int>(host.window()->findChildren<QObject*>().size())},
                        {QStringLiteral("open_ms"), host.lastOpenMilliseconds()}};
#ifdef Q_OS_WIN
                    DWORD handles = 0;
                    if (!GetProcessHandleCount(GetCurrentProcess(), &handles)) {
                        QCoreApplication::exit(13);
                        return;
                    }
                    sample.insert(QStringLiteral("handles"), static_cast<qint64>(handles));
#endif
                    lifecycleSamples.append(sample);
                } else {
                    timerPointer->stop();
                    const auto samples =
                        QJsonDocument(lifecycleSamples).toJson(QJsonDocument::Compact);
                    std::printf("Lifecycle samples: %s\n", samples.constData());
                    QCoreApplication::quit();
                }
                ++step;
            });
        timer->start();
        return app.exec();
    }
    return app.exec();
}
