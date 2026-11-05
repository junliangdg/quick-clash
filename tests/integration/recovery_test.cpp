#include "application/desktop_runtime.h"
#include "platform/windows/system_proxy.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <iostream>
#include <utility>
#include <yaml-cpp/yaml.h>
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
    const bool imported = waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
                          !runtime.snapshot().value("selectedId").toString().isEmpty();
    if (!imported)
        std::cerr << "fixture import: "
                  << QJsonDocument(runtime.snapshot().value("errors").toArray())
                         .toJson(QJsonDocument::Compact)
                         .constData()
                  << '\n';
    return imported;
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
    QTcpServer pendingWebsite;
    if (!pendingWebsite.listen(QHostAddress::LocalHost))
        return false;
    runtime.testWebsite(QStringLiteral("https://127.0.0.1:%1/").arg(pendingWebsite.serverPort()),
                        QStringLiteral("Direct"));
    if (!check(runtime.snapshot()
                       .value("features")
                       .toObject()
                       .value("websiteTest")
                       .toObject()
                       .value("state") == "Running",
               "network reconciliation starts with an actual pending website test"))
        return false;
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
    (void)runtime.setSystemProxy(true);
    if (!check(runtime.snapshot().value("systemProxy").toBool() &&
                   !runtime.snapshot().value("proxyConflict").toBool(),
               "explicit switch-on re-baselines the conflicted lease"))
        return false;
    (void)runtime.setSystemProxy(false);
    if (!check(settings.value == external && ready(runtime),
               "release restores the re-baselined external snapshot"))
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
    bool diagnosed = false;
    for (const auto& value : runtime.snapshot().value("errors").toArray()) {
        const auto entry = value.toObject();
        diagnosed |= entry.value("code") == "CoreExited" &&
                     entry.value("detail").toString().contains("code=42");
    }
    if (!check(diagnosed, "ordinary core crash records its actual exit code"))
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
bool failedStopPreservesProfile() {
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
    const auto before = runtime.snapshot();
    settings.fail = true;
    runtime.deleteProfile(before.value("selectedId").toString());
    const auto failed = runtime.snapshot();
    const bool preserved =
        failed.value("profiles") == before.value("profiles") &&
        failed.value("selectedId") == before.value("selectedId") &&
        failed.value("selectionGeneration") == before.value("selectionGeneration") &&
        failed.value("error") == "InjectedPlatformFailure";
    settings.fail = false;
    runtime.stop();
    return check(preserved && settings.value == originalSettings(),
                 "failed proxy release must preserve active profile and selection for repair");
}
QString actualMode(JsonStore& store, const QJsonObject& state) {
    const auto bytes = store.readBytes(
        QStringLiteral("data/sessions/%1/config.yaml").arg(state.value("epoch").toString()));
    if (!std::holds_alternative<QByteArray>(bytes))
        return {};
    const auto config = YAML::Load(std::get<QByteArray>(bytes).toStdString());
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:%1/configs")
                 .arg(state.value("preferences").toObject().value("controllerPort").toInt())));
    request.setRawHeader("Authorization",
                         "Bearer " + QByteArray::fromStdString(config["secret"].as<std::string>()));
    request.setTransferTimeout(3000);
    auto* reply = network.get(request);
    if (!waitFor([&] { return reply->isFinished(); }, 4000) ||
        reply->error() != QNetworkReply::NoError)
        return {};
    return QJsonDocument::fromJson(reply->readAll()).object().value("mode").toString();
}
bool modeCommitFailureRestoresCore() {
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
    const auto before = runtime.snapshot();
    for (const auto* mode : {"global", "direct", "rule"}) {
        runtime.setMode(QLatin1String(mode));
        if (!check(waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }) &&
                       ready(runtime) &&
                       runtime.snapshot().value("corePid") == before.value("corePid") &&
                       runtime.snapshot().value("epoch") == before.value("epoch") &&
                       actualMode(store, runtime.snapshot()) == QLatin1String(mode),
                   "mode PATCH confirms actual core without replacing process or epoch"))
            return false;
    }
    const auto committed = runtime.snapshot();
    HANDLE manifestLock = INVALID_HANDLE_VALUE;
    // Allow reads and staging, but deny replacement of the original manifest. This
    // exercises compensation after the real core has accepted the new mode.
    QObject::connect(&runtime, &DesktopRuntime::changed, &runtime, [&](const QJsonObject& state) {
        if (manifestLock == INVALID_HANDLE_VALUE && state.value("status") == "Reloading") {
            const auto path = store.root() + QStringLiteral("/data/runtime/current.json");
            manifestLock =
                CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }
    });
    runtime.setMode(QStringLiteral("global"));
    const bool finished = waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
    const bool locked = manifestLock != INVALID_HANDLE_VALUE;
    if (locked)
        CloseHandle(manifestLock);
    const auto after = runtime.snapshot();
    const bool restored =
        finished && locked && ready(runtime) && after.value("corePid") == before.value("corePid") &&
        after.value("epoch") == before.value("epoch") &&
        after.value("preferences").toObject().value("mode") == "rule" &&
        after.value("settingsState").toObject().value("effective").toObject().value("mode") ==
            "rule" &&
        after.value("selectionGeneration") == committed.value("selectionGeneration") &&
        after.value("systemProxy").toBool() && !after.value("error").toString().isEmpty();
    const bool actualRestored = actualMode(store, after) == QStringLiteral("rule");
    runtime.stop();
    return check(restored && actualRestored && settings.value == originalSettings(),
                 "mode commit failure restores the live mode, manifest and proxy lease");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.contains(QStringLiteral("--child")))
        return child(arguments);
    if (!check(firstFailures(), "first failure scenarios") ||
        !check(networkAndConflict(), "network and conflict scenarios") ||
        !check(failedStopPreservesProfile(), "failed stop preservation") ||
        !check(modeCommitFailureRestoresCore(), "mode commit compensation") ||
        !check(killedWorker(arguments.contains(QStringLiteral("--native-system-proxy"))),
               "killed worker recovery"))
        return 1;
    std::cout << "First port/platform/disk failures, managed partial failure, merged network "
                 "conflict, forced worker exit, Job cleanup, journal and lease recovery passed.\n";
    return 0;
}
