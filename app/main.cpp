#include "application_host.h"
#include "presentation/localization.h"
#include "single_instance.h"
#include "ui_font.h"
#ifdef QUICKCLASH_TEST_PROBES
#include "desktop_flow_probe.h"
#include "desktop_probe.h"
#include "desktop_proxy_probe.h"
#include "host_probe.h"
#include "language_ui_probe.h"
#include "runtime_ui_probe.h"
#include "service_probe.h"
#include "subscription_memory_probe.h"
#include "subscription_ui_probe.h"
#endif
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickStyle>
#include <QQuickWindow>
#ifdef QUICKCLASH_TEST_PROBES
#include <QQuickItem>
#endif
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string_view>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "platform/windows/service_installer.h"
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    {
        // The elevated helper runs this same executable before any UI, single-instance check or
        // user-profile access. Only the fixed setup actions are accepted.
        QStringList arguments;
        arguments.reserve(argc);
        for (int index = 0; index < argc; ++index)
            arguments.append(QString::fromLocal8Bit(argv[index]));
        const auto invocation = quickclash::platform::parseServiceSetupCommandLine(arguments);
        if (invocation.present) {
            if (!invocation.valid) {
                std::fprintf(stderr, "%s\n", invocation.problem.toUtf8().constData());
                return 2;
            }
            return quickclash::platform::runServiceSetup(invocation.request);
        }
    }
#endif
#ifdef QUICKCLASH_TEST_PROBES
    if (std::any_of(argv + 1, argv + argc, [](const char* argument) {
            return std::string_view(argument).starts_with("--desktop-flow-client=") ||
                   std::string_view(argument).starts_with("--desktop-flow-native=");
        })) {
        QCoreApplication probe(argc, argv);
        return quickclash::test::desktopFlowClient();
    }
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument.starts_with("--desktop-flow-server=")) {
            QCoreApplication probe(argc, argv);
            return quickclash::test::desktopFlowServer(
                QString::fromUtf8(argument.substr(22).data()));
        }
    }
    if (std::any_of(argv + 1, argv + argc, [](const char* argument) {
            return std::string_view(argument) == "--desktop-proxy-external";
        })) {
        QCoreApplication probe(argc, argv);
        return quickclash::test::desktopExternalProxyMutation();
    }
    if (std::any_of(argv + 1, argv + argc, [](const char* argument) {
            return std::string_view(argument) == "--service-test";
        })) {
        QCoreApplication probe(argc, argv);
        return quickclash::test::serviceProbe();
    }
#endif
    const bool softwareRendering = std::any_of(argv + 1, argv + argc, [](const char* argument) {
        return std::string_view(argument) == "--software-rendering";
    });
#ifdef Q_OS_WIN
    // Qt 6.8/D3D11 threaded teardown grows handles in the reference environment.
    // The basic render loop retains the native GPU backend and passed 100 rebuilds.
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", "basic");
    }
    // Qt 6.8's D3D11 frame-latency wait can stall the basic loop's GUI
    // thread for one second during resize. Keep flip presentation and vsync,
    // but disable the waitable object on this default path. Respect overrides.
    const auto rhiBackend = qgetenv("QSG_RHI_BACKEND");
    if (qgetenv("QSG_RENDER_LOOP") == "basic" && (rhiBackend.isEmpty() || rhiBackend == "d3d11") &&
        !softwareRendering && !qEnvironmentVariableIsSet("QT_QUICK_BACKEND") &&
        !qEnvironmentVariableIsSet("QT_D3D_MAX_FRAME_LATENCY") &&
        !qEnvironmentVariableIsSet("QT_D3D_NO_FLIP")) {
        qputenv("QT_D3D_MAX_FRAME_LATENCY", "0");
    }
#endif
    QApplication app(argc, argv);
    if (softwareRendering) {
        // Qt Quick owns the cross-platform raster scene graph; fonts keep normal Qt fallback.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }
    quickclash::presentation::Localization localization;
    if (!localization.load()) {
        std::fputs("Translation catalog unavailable; using English.\n", stderr);
    }
    QCoreApplication::setApplicationName(QStringLiteral("QuickClash"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication::setFont(quickclash::platformUiFont());
    const auto arguments = app.arguments();
    bool desktopTest = false;
#ifdef QUICKCLASH_TEST_PROBES
    desktopTest = arguments.contains(QStringLiteral("--desktop-test"));
    const bool smoke = arguments.contains(QStringLiteral("--smoke-test"));
    const bool prototype = arguments.contains(QStringLiteral("--prototype-test"));
    const bool hostTest = arguments.contains(QStringLiteral("--host-test"));
    const bool trayTest = arguments.contains(QStringLiteral("--tray-test"));
    const bool keyboardTest = arguments.contains(QStringLiteral("--keyboard-test"));
    const bool accessibilityTest = arguments.contains(QStringLiteral("--accessibility-test"));
    const bool responsiveTest = arguments.contains(QStringLiteral("--responsive-test"));
    const bool visualTest = arguments.contains(QStringLiteral("--visual-test"));
    const bool runtimeListTest = arguments.contains(QStringLiteral("--runtime-list-test"));
    const bool pageMemoryTest = arguments.contains(QStringLiteral("--page-memory-test"));
    const bool subscriptionUiTest = arguments.contains(QStringLiteral("--subscription-ui-test"));
    const bool languageTest = arguments.contains(QStringLiteral("--language-test"));
    const bool isolated = desktopTest || smoke || prototype || hostTest || trayTest ||
                          keyboardTest || accessibilityTest || responsiveTest || visualTest ||
                          runtimeListTest || subscriptionUiTest || languageTest || pageMemoryTest ||
                          arguments.contains(QStringLiteral("--isolated")) ||
                          arguments.contains(QStringLiteral("--subscription-memory-test"));
#else
    const bool isolated = arguments.contains(QStringLiteral("--isolated"));
#endif
    const auto option = [&arguments](const QString& key) {
        const auto prefix = key + QLatin1Char('=');
        for (const auto& argument : arguments) {
            if (argument.startsWith(prefix)) {
                return argument.mid(prefix.size());
            }
        }
        return QString{};
    };
    std::unique_ptr<QTemporaryDir> probeDirectory;
    QString probeSettings;
#ifdef QUICKCLASH_TEST_PROBES
    const auto probeLanguage = option(QStringLiteral("--probe-language"));
    if (isolated && !probeLanguage.isEmpty() && !localization.load({probeLanguage})) {
        return 16;
    }
    const auto runtimeProbe = option(QStringLiteral("--runtime-probe"));
    if (desktopTest) {
        probeSettings =
            quickclash::test::desktopProbeSettings(option(QStringLiteral("--desktop-root")));
        if (probeSettings.isEmpty())
            return 15;
    } else if (isolated && (!runtimeProbe.isEmpty() || languageTest ||
                            arguments.contains(QStringLiteral("--subscription-memory-test")))) {
        probeDirectory = std::make_unique<QTemporaryDir>();
        if (!probeDirectory->isValid())
            return 15;
        probeSettings = probeDirectory->filePath(QStringLiteral("ui.ini"));
    }
#endif
    const auto dataDirectory =
        desktopTest ? QFileInfo(probeSettings).absolutePath()
                    : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto configDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    quickclash::SingleInstance instance(dataDirectory);
    if (!isolated || desktopTest) {
        switch (instance.start(!arguments.contains(QStringLiteral("--autostart")))) {
        case quickclash::SingleInstance::Result::Activated:
            return 0;
        case quickclash::SingleInstance::Result::Unavailable:
            std::fputs("QuickClash instance lock/activation unavailable.\n", stderr);
            return 2;
        case quickclash::SingleInstance::Result::Primary:
            break;
        }
        if (!desktopTest && !QDir().mkpath(configDirectory)) {
            return 3;
        }
    }
    quickclash::ApplicationHost host(
        isolated ? probeSettings : QDir(configDirectory).filePath(QStringLiteral("ui.ini")),
#ifdef QUICKCLASH_TEST_PROBES
        (isolated && !trayTest && !desktopTest) || arguments.contains(QStringLiteral("--no-tray")));
#else
        isolated || arguments.contains(QStringLiteral("--no-tray")));
#endif
#ifdef QUICKCLASH_TEST_PROBES
    // Probe language is an explicit test override; saved preferences must not replace it.
    if (probeLanguage.isEmpty())
        host.setLanguageController(&localization);
#else
    host.setLanguageController(&localization);
#endif
    QObject::connect(&instance, &quickclash::SingleInstance::activationRequested, &host,
                     [&host] { (void)host.showWindow(); });
#ifdef QUICKCLASH_TEST_PROBES
    if (!probeSettings.isEmpty() && !runtimeProbe.isEmpty()) {
        auto stage = std::make_shared<int>(0);
        QObject::connect(
            &host.state(), &quickclash::presentation::UiState::runtimeChanged, &host,
            [&host, stage, runtimeProbe, option] {
                const auto state = host.state().runtime();
                if (state.value(QStringLiteral("busy")).toBool())
                    return;
                if (*stage == 0) {
                    *stage = 1;
                    host.state().runtimeCommand(
                        QStringLiteral("ports"),
                        {option(QStringLiteral("--probe-mixed-port")),
                         option(QStringLiteral("--probe-controller-port"))});
                    host.state().runtimeCommand(QStringLiteral("import"),
                                                {QUrl::fromLocalFile(runtimeProbe).toString(),
                                                 QStringLiteral("Runtime sample")});
                } else if (*stage == 1 && state.value(QStringLiteral("ready")).toBool()) {
                    *stage = 2;
                }
            });
        const auto stateFile = option(QStringLiteral("--runtime-state"));
        if (!stateFile.isEmpty()) {
            auto* timer = new QTimer(&host);
            QObject::connect(timer, &QTimer::timeout, &host, [&host, stateFile] {
                const auto state = host.state().runtime();
                QSaveFile file(stateFile);
                const QJsonObject value{
                    {QStringLiteral("ready"), state.value(QStringLiteral("ready")).toBool()},
                    {QStringLiteral("busy"), state.value(QStringLiteral("busy")).toBool()},
                    {QStringLiteral("corePid"), state.value(QStringLiteral("corePid")).toInt()},
                    {QStringLiteral("error"), state.value(QStringLiteral("error")).toString()}};
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QJsonDocument(value).toJson());
                    file.commit();
                }
            });
            timer->start(500);
        }
    }
    const auto replayConnections = option(QStringLiteral("--replay-connections"));
    if (!replayConnections.isEmpty() &&
        !host.state().setReplayConnections(replayConnections.toInt())) {
        std::fputs("Replay connections must be 1000 or 10000.\n", stderr);
        return 9;
    }
    host.state().setReplay(arguments.contains(QStringLiteral("--replay")) || prototype ||
                           hostTest || trayTest);
#endif
    if (!option(QStringLiteral("--theme")).isEmpty()) {
        host.state().setThemeMode(option(QStringLiteral("--theme")));
    }
    if (!option(QStringLiteral("--page")).isEmpty() &&
        !host.state().navigate(option(QStringLiteral("--page")))) {
        return 4;
    }
    if (!(arguments.contains(QStringLiteral("--autostart")) ? host.startAtLogin()
                                                            : host.showWindow())) {
        return 1;
    }
#ifdef QUICKCLASH_TEST_PROBES
    if (arguments.contains(QStringLiteral("--subscription-memory-test"))) {
        QTimer::singleShot(1000, &host, [&host] {
            auto& ui = host.state();
            if (!ui.settingsReady()) {
                QCoreApplication::exit(20);
                return;
            }
            ui.navigate(QStringLiteral("subscriptions"));
            const quickclash::test::SubscriptionProbeRuntime client{
                [&] { return ui.diagnosticSnapshot(); },
                [&](int mixed, int controller) {
                    ui.runtimeCommand(QStringLiteral("ports"),
                                      {QString::number(mixed), QString::number(controller)});
                },
                [&](const QString& url) {
                    ui.runtimeCommand(QStringLiteral("subscribe"),
                                      {url, QStringLiteral("Public memory fixture")});
                },
                [&] { ui.runtimeCommand(QStringLiteral("start")); },
                [&](const QString& id) {
                    if (QCoreApplication::arguments().contains(
                            QStringLiteral("--probe-management"))) {
                        auto* page = host.window()->findChild<QObject*>(
                            QStringLiteral("subscriptions_page"));
                        if (page)
                            QMetaObject::invokeMethod(page, "manage", Q_ARG(QVariant, id));
                    }
                    ui.runtimeCommand(QStringLiteral("update"), {id});
                },
                [&] { ui.runtimeCommand(QStringLiteral("stop")); },
                [&] {
                    ui.navigate(QStringLiteral("home"));
                    // Match inspecting the resource card near the bottom of Home.
                    QTimer::singleShot(500, &host, [&host] {
                        for (auto* item : host.window()->findChildren<QQuickItem*>()) {
                            if (item->inherits("QQuickFlickable") && item->isVisible()) {
                                const auto bottom =
                                    item->property("contentHeight").toReal() - item->height();
                                item->setProperty("contentY", std::max(0.0, bottom));
                            }
                        }
                    });
                },
                [&](const QString& node) {
                    const auto containsNode = [&](auto&& self, QQuickItem* item) -> bool {
                        if (!item || !item->isVisible())
                            return false;
                        if (item->property("text").toString().contains(node))
                            return true;
                        for (auto* child : item->childItems())
                            if (self(self, child))
                                return true;
                        return false;
                    };
                    return containsNode(containsNode, host.window()->contentItem());
                }};
            QCoreApplication::exit(quickclash::test::subscriptionMemoryProbe(client));
        });
    }
#endif
    const auto windowSize = option(QStringLiteral("--window-size"));
    if (isolated && !windowSize.isEmpty()) {
        const auto dimensions = windowSize.split(QLatin1Char('x'));
        if (dimensions.size() != 2 || dimensions[0].toInt() < 360 || dimensions[1].toInt() < 480) {
            return 13;
        }
        host.resizeWindow(QSize(dimensions[0].toInt(), dimensions[1].toInt()));
    }
#ifdef QUICKCLASH_TEST_PROBES
    if (desktopTest) {
        return quickclash::test::runDesktopProbe(host);
    }
    if (hostTest) {
        return quickclash::runHostProbe(host);
    }
    if (trayTest) {
        return quickclash::runTrayProbe(host);
    }
    if (subscriptionUiTest) {
        return quickclash::runSubscriptionUiProbe(host);
    }
    if (languageTest) {
        return quickclash::runLanguageUiProbe(host);
    }
    if (runtimeListTest) {
        return quickclash::runRuntimeListProbe(host);
    }
    if (pageMemoryTest) {
        return quickclash::runPageMemoryProbe(host);
    }
    if (responsiveTest) {
        return quickclash::runResponsiveProbe(host);
    }
    if (visualTest) {
        return quickclash::runVisualProbe(host);
    }
    if (keyboardTest) {
        return quickclash::runKeyboardProbe(host);
    }
    if (accessibilityTest) {
        return quickclash::runAccessibilityProbe(host);
    }
#endif
    const auto capture = option(QStringLiteral("--capture"));
    if (isolated && !capture.isEmpty()) {
        auto* captureTimer = new QTimer(&host);
        captureTimer->setInterval(200);
        const bool waitForRuntime = !option(QStringLiteral("--runtime-probe")).isEmpty();
        QObject::connect(
            captureTimer, &QTimer::timeout, &host,
            [&host, capture, captureTimer, waitForRuntime, attempts = 0]() mutable {
                const auto state = host.state().runtime();
                if (!host.contentReady() ||
                    (waitForRuntime && (!state.value(QStringLiteral("ready")).toBool() ||
                                        state.value(QStringLiteral("busy")).toBool()))) {
                    if (++attempts < 50)
                        return;
                    QCoreApplication::exit(12);
                    return;
                }
                captureTimer->stop();
                if (!host.window()->grabWindow().save(capture))
                    QCoreApplication::exit(12);
                captureTimer->deleteLater();
            });
        captureTimer->start();
    }
#ifdef QUICKCLASH_TEST_PROBES
    if (smoke) {
        QTimer::singleShot(300, &host,
                           [&host] { QCoreApplication::exit(host.contentReady() ? 0 : 8); });
    }
#endif
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
#ifdef QUICKCLASH_TEST_PROBES
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
#endif
    return app.exec();
}
