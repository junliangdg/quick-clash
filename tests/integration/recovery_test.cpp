#include "application/desktop_runtime.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <iostream>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using quickclash::application::DesktopRuntime;
using quickclash::domain::CommandResult;
using quickclash::domain::Error;
using quickclash::infra::JsonStore;
bool passed(const CommandResult& result) {
    return std::holds_alternative<std::monostate>(result);
}
bool waitFor(const std::function<bool()>& condition, int milliseconds = 15000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
bool check(bool condition, const char* message) {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}
int freePort() {
    QTcpServer socket;
    return socket.listen(QHostAddress::LocalHost) ? socket.serverPort() : 0;
}
QJsonObject originalSettings() {
    return {{QStringLiteral("flags"), 13},
            {QStringLiteral("server"), QStringLiteral("previous.example.invalid:8888")},
            {QStringLiteral("bypass"), QStringLiteral("*.example.invalid;<local>")},
            {QStringLiteral("pac"), QStringLiteral("https://pac.example.invalid/config")}};
}
class Settings final : public quickclash::platform::ProxySettings {
  public:
    QJsonObject value = originalSettings();
    std::function<void()> afterWrite;
    bool fail = false;
    quickclash::domain::Result<QJsonObject> read() override {
        return value;
    }
    CommandResult write(const QJsonObject& next) override {
        if (fail)
            return quickclash::infra::storageError("InjectedPlatformFailure");
        value = next;
        if (afterWrite)
            afterWrite();
        return std::monostate{};
    }
};
// Models an OS setting that survives the killed worker. Never stored in the repository.
class FileSettings final : public quickclash::platform::ProxySettings {
  public:
    explicit FileSettings(QString path) : path_(std::move(path)) {}
    quickclash::domain::Result<QJsonObject> read() override {
        QFile file(path_);
        if (!file.open(QIODevice::ReadOnly))
            return quickclash::infra::storageError("FixtureReadFailed");
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            return quickclash::infra::storageError("FixtureReadFailed");
        return document.object();
    }
    CommandResult write(const QJsonObject& value) override {
        QSaveFile file(path_);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(value).toJson()) < 0 ||
            !file.commit())
            return quickclash::infra::storageError("FixtureWriteFailed");
        return std::monostate{};
    }

  private:
    QString path_;
};
bool ready(const DesktopRuntime& runtime) {
    return runtime.snapshot().value("ready").toBool() && !runtime.snapshot().value("busy").toBool();
}
bool import(DesktopRuntime& runtime) {
    const auto first = freePort();
    auto second = freePort();
    while (second == first)
        second = freePort();
    runtime.setPorts(first, second);
    runtime.importFile(QUrl::fromLocalFile(QDir::current().absoluteFilePath(QStringLiteral(
                                               "tests/fixtures/mvp/inline_valid.yaml")))
                           .toString(),
                       QStringLiteral("Recovery fixture"));
    return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
           !runtime.snapshot().value("selectedId").toString().isEmpty();
}
int child(const QStringList& arguments) {
    const auto root = arguments.value(2);
    FileSettings fileSettings(root + QStringLiteral("/os.json"));
    quickclash::platform::WindowsProxySettings nativeSettings;
    auto* settings = arguments.contains(QStringLiteral("--native-system-proxy"))
                         ? static_cast<quickclash::platform::ProxySettings*>(&nativeSettings)
                         : &fileSettings;
    JsonStore store(root + QStringLiteral("/store"));
    const auto opened = store.open();
    if (const auto* failure = std::get_if<Error>(&opened)) {
        std::cerr << failure->code.toStdString() << '\n';
        return 1;
    }
    DesktopRuntime runtime(store, nullptr, settings);
    runtime.initialize();
    if (arguments.contains(QStringLiteral("--recover-only"))) {
        quickclash::infra::ConfigRepository repository(store);
        const bool recovered = passed(repository.load()) && !repository.recoveryRequired() &&
                               !runtime.snapshot().value("readOnly").toBool() &&
                               !runtime.snapshot().value("ready").toBool() &&
                               !runtime.snapshot().value("systemProxy").toBool() &&
                               !runtime.snapshot().value("selectedId").toString().isEmpty();
        std::cout << (recovered ? "Recovered\n" : "Failed\n");
        if (!recovered)
            std::cerr << QJsonDocument(runtime.snapshot().value("errors").toArray())
                             .toJson(QJsonDocument::Compact)
                             .constData()
                      << '\n';
        return recovered ? 0 : 2;
    }
    if (!import(runtime))
        return 3;
    runtime.start(true);
    if (!waitFor([&] { return ready(runtime); }) ||
        !runtime.snapshot().value("systemProxy").toBool())
        return 4;
    // Leave a real uncommitted Apply journal while the current owned session is alive.
    quickclash::infra::ConfigRepository repository(store);
    if (!passed(repository.load()) ||
        !std::holds_alternative<QUuid>(
            repository.prepare(repository.manifest(), QStringLiteral("Apply"), true)) ||
        !passed(repository.phase(QStringLiteral("CoreApplying"))))
        return 5;
    std::cout << QJsonDocument(
                     QJsonObject{
                         {QStringLiteral("corePid"), runtime.snapshot().value("corePid")},
                         {QStringLiteral("mixedPort"),
                          runtime.snapshot().value("preferences").toObject().value("mixedPort")}})
                     .toJson(QJsonDocument::Compact)
                     .constData()
              << std::endl;
    return QCoreApplication::exec();
}
bool killedWorker(bool native) {
    QTemporaryDir directory;
    FileSettings fileSettings(directory.filePath(QStringLiteral("os.json")));
    quickclash::platform::WindowsProxySettings nativeSettings;
    auto& settings =
        native ? static_cast<quickclash::platform::ProxySettings&>(nativeSettings) : fileSettings;
    if (!native && !passed(settings.write(originalSettings())))
        return false;
    const auto before = settings.read();
    if (!std::holds_alternative<QJsonObject>(before))
        return false;
    // Restore only our exact value on an early failure; never overwrite an external edit.
    struct Cleanup {
        quickclash::platform::ProxySettings& settings;
        QJsonObject before;
        QJsonObject applied;
        ~Cleanup() {
            const auto observed = settings.read();
            if (!applied.isEmpty() && std::holds_alternative<QJsonObject>(observed) &&
                std::get<QJsonObject>(observed) == applied)
                (void)settings.write(before);
        }
    } cleanup{settings, std::get<QJsonObject>(before), {}};
    QStringList arguments{QStringLiteral("--child"), directory.path()};
    if (native)
        arguments.append(QStringLiteral("--native-system-proxy"));
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), arguments);
    QByteArray output;
    const bool started = waitFor([&] {
        output += process.readAllStandardOutput();
        return output.contains('\n') || process.state() == QProcess::NotRunning;
    });
    QFile leaseFile(directory.filePath(QStringLiteral("store/data/leases/windows_lan.json")));
    if (leaseFile.open(QIODevice::ReadOnly)) {
        const auto lease = QJsonDocument::fromJson(leaseFile.readAll()).object();
        if (lease.value(QStringLiteral("before")).toObject() == cleanup.before)
            cleanup.applied = lease.value(QStringLiteral("applied")).toObject();
        leaseFile.close();
    }
    const auto state = QJsonDocument::fromJson(output.trimmed()).object();
    const auto pid = static_cast<quint32>(state.value("corePid").toInt());
    if (!check(started && pid && process.state() == QProcess::Running, "child ready"))
        return false;
#ifdef Q_OS_WIN
    const auto core = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!core)
        return false;
#endif
    process.kill();
    const bool killed = process.waitForFinished(5000);
#ifdef Q_OS_WIN
    const bool coreExited = WaitForSingleObject(core, 5000) == WAIT_OBJECT_0;
    CloseHandle(core);
#else
    const bool coreExited = false;
#endif
    QTcpServer listener;
    const bool portReleased = listener.listen(
        QHostAddress::LocalHost, static_cast<quint16>(state.value("mixedPort").toInt()));
    listener.close();
    arguments.append(QStringLiteral("--recover-only"));
    process.start(QCoreApplication::applicationFilePath(), arguments);
    const bool finished = process.waitForFinished(15000);
    const auto recoveredOutput = process.readAllStandardOutput();
    if (!check(finished && process.exitCode() == 0 && recoveredOutput.contains("Recovered"),
               "next launch recovery")) {
        std::cerr << "finished=" << finished << " exit=" << process.exitCode()
                  << " stdout=" << recoveredOutput.constData()
                  << " stderr=" << process.readAllStandardError().constData() << '\n';
        return false;
    }
    const auto after = settings.read();
    return check(killed && coreExited && portReleased &&
                     std::holds_alternative<QJsonObject>(after) &&
                     std::get<QJsonObject>(after) == cleanup.before,
                 "Job, journal and exact proxy restoration after forced client exit");
}
bool firstFailures() {
    for (int scenario = 0; scenario != 4; ++scenario) {
        QTemporaryDir directory;
        JsonStore store(directory.path());
        Settings settings;
        if (!passed(store.open()))
            return false;
        {
            DesktopRuntime runtime(store, nullptr, &settings);
            runtime.initialize();
            if (!import(runtime))
                return false;
            const auto selection = runtime.snapshot().value("selectionGeneration");
            QTcpServer occupied;
            if (scenario == 0) {
                if (!occupied.listen(QHostAddress::LocalHost))
                    return false;
                runtime.setPorts(occupied.serverPort(), freePort());
                if (!waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }))
                    return false;
            } else if (scenario == 1)
                settings.fail = true;
            else if (scenario == 2)
                settings.afterWrite = [&] { store.freeze(); };
            else {
                runtime.enableBackgroundManagement();
                // The timer has not started the core yet. Exercise the combined start.
                settings.fail = true;
            }
            runtime.start(true);
            if (!check(waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
                           runtime.snapshot().value("operation").toObject().value("state") ==
                               "Failed" &&
                           !runtime.snapshot().value("systemProxy").toBool() &&
                           settings.value == originalSettings() &&
                           (scenario == 3 ? ready(runtime)
                                          : !runtime.snapshot().value("ready").toBool()),
                       "first start failure must not confirm proxy success or leak settings"))
                return false;
            if (scenario == 1 &&
                !check(runtime.snapshot().value("selectionGeneration") == selection,
                       "first platform failure preserves selection"))
                return false;
            runtime.stop(quickclash::domain::StopReason::AppExit);
        }
        settings.fail = false;
        settings.afterWrite = {};
    }
    return true;
}
bool networkAndConflict() {
    QTemporaryDir directory;
    JsonStore store(directory.path());
    Settings settings;
    if (!passed(store.open()))
        return false;
    DesktopRuntime runtime(store, nullptr, &settings);
    runtime.initialize();
    if (!import(runtime))
        return false;
    runtime.start(true);
    if (!waitFor([&] { return ready(runtime); }))
        return false;
    const auto pid = runtime.snapshot().value("corePid");
    settings.value.insert(QStringLiteral("server"),
                          QStringLiteral("external.example.invalid:9000"));
    const auto external = settings.value;
    for (int i = 0; i < 20; ++i)
        runtime.reconcile();
    if (!check(runtime.snapshot()
                           .value("features")
                           .toObject()
                           .value("websiteTest")
                           .toObject()
                           .value("state") == "Stale" &&
                   waitFor([&] {
                       return runtime.snapshot()
                                  .value("reconciliation")
                                  .toObject()
                                  .value("completed")
                                  .toString() == "1";
                   }) &&
                   runtime.snapshot().value("reconciliation").toObject().value("requests") ==
                       "20" &&
                   runtime.snapshot().value("corePid") == pid && ready(runtime) &&
                   runtime.snapshot().value("proxyConflict").toBool() && settings.value == external,
               "network burst coalesces, invalidates results and preserves external proxy"))
        return false;
    runtime.reapplySystemProxy();
    if (!check(runtime.snapshot().value("systemProxy").toBool(), "explicit reapply"))
        return false;
    (void)runtime.setSystemProxy(false);
    if (!check(settings.value == external && ready(runtime),
               "release restores new external snapshot"))
        return false;
#ifdef Q_OS_WIN
    const auto core = OpenProcess(PROCESS_TERMINATE, FALSE,
                                  static_cast<DWORD>(runtime.snapshot().value("corePid").toInt()));
    if (!core)
        return false;
    const bool terminated = TerminateProcess(core, 42);
    CloseHandle(core);
    if (!check(terminated &&
                   waitFor([&] { return runtime.snapshot().value("recovering").toBool(); }),
               "core crash enters backoff"))
        return false;
    runtime.stop();
    QElapsedTimer timer;
    timer.start();
    (void)waitFor([&] { return timer.elapsed() >= 1700; }, 2000);
    if (!check(!runtime.snapshot().value("ready").toBool() &&
                   !runtime.snapshot().value("recovering").toBool() &&
                   runtime.snapshot().value("corePid").toInt() == 0 && settings.value == external,
               "stop cancels pending crash recovery"))
        return false;
#endif
    runtime.stop(quickclash::domain::StopReason::AppExit);
    runtime.reconcile();
    return check(runtime.snapshot().value("reconciliation").toObject().value("requests") == "20",
                 "shutdown ignores late OS events");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.contains(QStringLiteral("--child")))
        return child(arguments);
    if (!firstFailures() || !networkAndConflict() ||
        !killedWorker(arguments.contains(QStringLiteral("--native-system-proxy"))))
        return 1;
    std::cout << "First port/platform/disk failures, managed partial failure, merged network "
                 "conflict, forced worker exit, Job cleanup, journal and lease recovery passed.\n";
    return 0;
}
