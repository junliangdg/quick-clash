#include "subscription_desktop_memory_probe.h"
#include "application_host.h"
#include "platform/process_memory.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QTest>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on
#endif

namespace quickclash::test {
#ifdef Q_OS_WIN
namespace {
QString option(const char* name) {
    const auto prefix = QString::fromLatin1(name) + QLatin1Char('=');
    for (const auto& argument : QCoreApplication::arguments())
        if (argument.startsWith(prefix))
            return argument.mid(prefix.size());
    return {};
}
bool waitFor(const std::function<bool()>& condition, int milliseconds = 30000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds)
        QTest::qWait(10);
    return condition();
}
bool save(const QString& output, const QJsonObject& value) {
    QSaveFile file(output);
    const auto bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
int ownedChildren() {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return -1;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int count = 0;
    if (Process32FirstW(snapshot, &entry))
        do {
            if (entry.th32ParentProcessID == GetCurrentProcessId())
                ++count;
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return count;
}
QJsonObject transportMemory() {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    QJsonObject result;
    if (Process32FirstW(snapshot, &entry))
        do {
            if (entry.th32ParentProcessID == GetCurrentProcessId() &&
                QString::fromWCharArray(entry.szExeFile) ==
                    QStringLiteral("quickclash_network.exe")) {
                result = platform::processMemory(platform::processIdentity(entry.th32ProcessID));
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return result;
}
QJsonObject processResources() {
    DWORD handles = 0;
    const bool sampled = GetProcessHandleCount(GetCurrentProcess(), &handles);
    return {
        {"handles", sampled ? static_cast<int>(handles) : -1},
        {"gdiObjects", static_cast<int>(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS))},
        {"userObjects", static_cast<int>(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS))}};
}
int sessionFiles(const QString& root) {
    QDirIterator files(root + QStringLiteral("/data/sessions"), QDir::Files,
                       QDirIterator::Subdirectories);
    int count = 0;
    while (files.hasNext()) {
        files.next();
        ++count;
    }
    return count;
}
} // namespace
#endif

int subscriptionDesktopMemoryProbe(ApplicationHost& host, const QString& output) {
#ifndef Q_OS_WIN
    Q_UNUSED(host)
    Q_UNUSED(output)
    return 77;
#else
    auto& ui = host.state();
    bool validIndex = false;
    const int index = option("--desktop-profile-index").toInt(&validIndex);
    const bool stopped = option("--desktop-memory-mode") == QStringLiteral("stopped");
    const bool unloaded = option("--desktop-memory-ui") == QStringLiteral("unloaded");
    const bool idleControl = option("--desktop-memory-action") == QStringLiteral("idle");
    const bool lifecycle = option("--desktop-memory-action") == QStringLiteral("lifecycle");
    const bool exposed = option("--desktop-memory-window") == QStringLiteral("exposed");
    bool validRepeats = false;
    int repeats = option("--desktop-memory-repeats").toInt(&validRepeats);
    if (!validRepeats)
        repeats = lifecycle ? 3 : 1;
    QJsonObject result{
        {"scenario", lifecycle ? "isolated_core_lifecycle" : "isolated_subscription_update"},
        {"qtVersion", qVersion()},
        {"stoppedUpdate", stopped},
        {"uiUnloaded", unloaded},
        {"idleControl", idleControl},
        {"passed", false}};
    const auto idle = [&] { return !ui.runtime().value("busy").toBool(); };
    const auto ready = [&] { return idle() && ui.runtime().value("ready").toBool(); };
    // The desktop entry point has already required a marked isolated data root.
    // Profiles and secrets remain inside that root; output is an allowlist of counters
    // and booleans, never runtime/profile snapshots, source bytes or identifying fields.
    std::atomic<int> phase{0};
    std::atomic<quint32> corePid{ui.runtime().value("corePid").toUInt()};
    QJsonArray controls;
    QElapsedTimer clock;
    clock.start();
    QObject observer;
    QObject::connect(&ui, &presentation::UiState::runtimeChanged, &observer, [&] {
        const auto state = ui.runtime();
        corePid = state.value("corePid").toUInt();
        controls.append(QJsonObject{{"milliseconds", clock.elapsed()},
                                    {"phase", phase.load()},
                                    {"ready", state.value("ready").toBool()},
                                    {"busy", state.value("busy").toBool()},
                                    {"status", state.value("status").toString()},
                                    {"error", state.value("error").toString()},
                                    {"corePresent", corePid.load() != 0}});
    });
    QJsonArray samples;
    QJsonArray stages;
    const auto begin = std::chrono::steady_clock::now();
    // Only this thread writes samples. The GUI reads it after joining the sampler.
    std::jthread sampler([&](std::stop_token stop) {
        int children = -1;
        int tick = 0;
        QJsonObject transport;
        while (!stop.stop_requested()) {
            if (tick++ % 5 == 0) {
                children = ownedChildren();
                transport = transportMemory();
            }
            const auto pid = corePid.load();
            samples.append(QJsonObject{
                {"phase", phase.load()},
                {"milliseconds",
                 static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - begin)
                                         .count())},
                {"client", platform::currentProcessMemory()},
                {"networkRelay", transport},
                {"resources", processResources()},
                {"ownedChildren", children},
                {"core",
                 pid ? platform::processMemory(platform::processIdentity(pid)) : QJsonObject{}}});
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    const auto finish = [&] {
        sampler.request_stop();
        sampler.join();
        result.insert("stages", stages);
        result.insert("samples", samples);
        result.insert("controls", controls);
        return save(output, result);
    };
    const auto fail = [&](const char* stage) {
        result.insert("failureStage", QString::fromLatin1(stage));
        (void)finish();
        return 200;
    };
    const auto mark = [&](const QString& name) {
        corePid = ui.runtime().value("corePid").toUInt();
        const auto number = static_cast<int>(stages.size());
        const auto root = option("--desktop-root");
        stages.append(
            QJsonObject{{"phase", number},
                        {"name", name},
                        {"ownedChildren", ownedChildren()},
                        {"resources", processResources()},
                        {"activeDemands", ui.activeDemandCount()},
                        {"activeStreams", ui.activeStreamCount()},
                        {"sessionFiles", sessionFiles(root)},
                        {"validationDirectories", QDir(root + QStringLiteral("/cache/validation"))
                                                      .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
                                                      .size()}});
        phase = number;
        (void)save(output + QStringLiteral(".stage.json"),
                   {{"phase", name},
                    {"pid", QCoreApplication::applicationPid()},
                    {"client", platform::currentProcessMemory()}});
    };
    const auto hold = [&](int milliseconds) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) {
            corePid = ui.runtime().value("corePid").toUInt();
            QTest::qWait(20);
        }
    };
    mark(QStringLiteral("initialize"));
    if (output.isEmpty() || !validIndex || index < 0 || repeats < 1 || repeats > 5 ||
        !waitFor([&] { return ui.settingsReady() && host.contentReady() && ready(); }) ||
        index >= ui.profileModel()->count())
        return fail("initialize");
    if (exposed) {
        if (!host.window())
            return fail("window_missing");
        // A runner may suppress the first native ShowWindow through STARTUPINFO.
        // Explicitly reshow before accepting this run as a rendering measurement.
        host.window()->hide();
        host.window()->showNormal();
        host.window()->raise();
        if (!QTest::qWaitForWindowExposed(host.window(), 5000))
            return fail("window_not_exposed");
    }
    const auto id = ui.profileModel()->entryAt(index).value("id").toString();
    if (!lifecycle && (!ui.navigate(QStringLiteral("subscriptions")) ||
                       !waitFor([&] { return host.contentReady(); })))
        return fail("subscriptions_page");
    if (stopped) {
        ui.runtimeCommand(QStringLiteral("stop"));
        if (!waitFor([&] { return idle() && !ui.runtime().value("ready").toBool(); }))
            return fail("stop_before_update");
    }
    result.insert("windowExposed", host.window() && host.window()->isExposed());
    result.insert(
        "graphicsApi",
        host.window() ? static_cast<int>(host.window()->rendererInterface()->graphicsApi()) : -1);
    if (unloaded) {
        host.unloadWindow();
        if (!waitFor([&] { return !host.window(); }))
            return fail("unload_before_update");
    }
    mark(QStringLiteral("before"));
    hold(3000);
    bool passed = true;
    if (lifecycle) {
        QJsonArray cycles;
        for (int cycle = 1; cycle <= repeats; ++cycle) {
            const auto previousCore =
                platform::processIdentity(ui.runtime().value("corePid").toUInt());
            mark(QStringLiteral("stop_%1").arg(cycle));
            ui.runtimeCommand(QStringLiteral("stop"));
            const bool cleaned = waitFor([&] {
                return idle() && !ui.runtime().value("ready").toBool() &&
                       ui.runtime().value("corePid").toUInt() == 0 && ownedChildren() == 0;
            });
            mark(QStringLiteral("stopped_%1").arg(cycle));
            hold(2000);
            const bool previousExited = platform::processMemory(previousCore).isEmpty();
            mark(QStringLiteral("start_%1").arg(cycle));
            ui.runtimeCommand(QStringLiteral("start"));
            const bool started = waitFor(ready);
            mark(QStringLiteral("ready_%1").arg(cycle));
            hold(4000);
            const int children = ownedChildren();
            cycles.append(
                QJsonObject{{"cleaned", cleaned},
                            {"previousExited", previousExited},
                            {"started", started},
                            {"ownedChildren", children},
                            {"errorPresent", !ui.runtime().value("error").toString().isEmpty()}});
            passed = passed && cleaned && previousExited && started && children == 1 &&
                     ui.runtime().value("error").toString().isEmpty();
            if (!passed)
                break;
        }
        result.insert("cycles", cycles);
    } else if (idleControl) {
        mark(QStringLiteral("idle_control"));
        hold(10000);
    } else {
        QJsonArray updates;
        for (int update = 1; update <= repeats; ++update) {
            const auto old = ui.profileModel()->profile(id);
            const auto previousPid = ui.runtime().value("corePid");
            mark(QStringLiteral("update_%1").arg(update));
            ui.runtimeCommand(QStringLiteral("update"), {id});
            const bool completed = waitFor(
                [&] {
                    const auto current = ui.profileModel()->profile(id);
                    return idle() &&
                           ui.runtime().value("updatingSubscriptionId").toString().isEmpty() &&
                           current.value("lastAttemptAt") != old.value("lastAttemptAt") &&
                           !current.value("lastUpdateResult").toString().isEmpty();
                },
                40000);
            const auto current = ui.profileModel()->profile(id);
            const bool saved = completed && current.value("lastUpdateSaved").toBool();
            passed = passed && saved &&
                     current.value("lastUpdateResult") == QStringLiteral("Updated") &&
                     ui.runtime().value("error").toString().isEmpty() &&
                     ui.runtime().value("ready").toBool() == !stopped;
            updates.append(QJsonObject{
                {"completed", completed},
                {"saved", saved},
                {"corePreserved", ui.runtime().value("corePid") == previousPid},
                {"contentChanged", current.value("sourceHash") != old.value("sourceHash")},
                {"errorPresent", !ui.runtime().value("error").toString().isEmpty()}});
            mark(QStringLiteral("after_%1").arg(update));
            hold(6000);
            if (!passed)
                break;
        }
        result.insert("updates", updates);
    }
    if (!unloaded &&
        (!ui.navigate(QStringLiteral("home")) || !waitFor([&] { return host.contentReady(); })))
        passed = false;
    mark(QStringLiteral("home"));
    hold(2000);
    ui.runtimeCommand(QStringLiteral("stop"));
    const bool cleaned = waitFor([&] {
        return idle() && !ui.runtime().value("ready").toBool() &&
               ui.runtime().value("corePid").toUInt() == 0;
    });
    mark(QStringLiteral("stopped"));
    hold(1000);
    result.insert("passed", passed && cleaned && host.qmlErrorCount() == 0);
    result.insert("cleaned", cleaned);
    result.insert("qmlErrors", host.qmlErrorCount());
    return finish() && result.value("passed").toBool() ? 0 : 201;
#endif
}
} // namespace quickclash::test
