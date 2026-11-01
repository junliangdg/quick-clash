#include "core_update_probe.h"
#include "application/desktop_runtime.h"
#include "network_fixture.h"
#include "platform/core_artifacts.h"
#include "platform/desktop_services.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& predicate, int limit = 30000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < limit) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return predicate();
}
application::DesktopServices services(infra::JsonStore& store, const QString& core,
                                      const QUrl& source) {
    return {platform::makeCredentialStore(store),
            platform::desktopPlatformInfo(),
            platform::makeDesktopResources(),
            platform::makeSystemProxyLease(store),
            platform::makeDesktopProcess(),
            platform::makeServiceSetup(),
            core,
            source};
}
int port() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
} // namespace
int coreUpdateProbe() {
    QTemporaryDir root;
    const auto bundled = root.filePath("core/mihomo.exe");
    if (!QDir().mkpath(root.filePath("core")) || !QFile::copy(".cache/mihomo/mihomo.exe", bundled))
        return 1;
    infra::JsonStore store(root.filePath("storage"));
    if (std::holds_alternative<domain::Error>(store.open()))
        return 2;
    TlsFixture origin;
    if (!origin.initialize())
        return 3;
    QFile zip(".cache/mihomo-windows-amd64-v1.19.33.zip");
    if (!zip.open(QIODevice::ReadOnly))
        return 4;
    origin.payloads.insert("/core.zip", zip.readAll());
    zip.close();
    const QUrl source(QStringLiteral("https://127.0.0.1:%1/core.zip").arg(origin.serverPort()));
    application::DesktopRuntime runtime(store, services(store, bundled, source));
    runtime.initialize();
    const auto check = [&](bool good, const char* message) {
        if (!good)
            std::fprintf(stderr, "%s: %s\n", message,
                         QJsonDocument(runtime.snapshot()).toJson().constData());
        return good;
    };
    const auto done = [&] {
        return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
    };
    const auto state = [&] {
        return runtime.snapshot().value("coreUpdate").toObject().value("state").toString();
    };
    auto mixed = port(), controller = port();
    while (mixed == controller)
        controller = port();
    runtime.setPorts(mixed, controller);
    const auto profile = root.filePath("profile.yaml");
    {
        QFile file(profile);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write("proxies: []\nproxy-groups: []\nrules: ['MATCH,DIRECT']\n") <= 0)
            return 5;
    }
    runtime.importFile(QUrl::fromLocalFile(profile).toString(), "Core update fixture");
    if (!check(done() && !runtime.snapshot().value("selectedId").toString().isEmpty(), "import"))
        return 6;
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(), "bundled Ready"))
        return 7;
    const auto pid = runtime.snapshot().value("corePid");
    runtime.featureCommand("core_check", {});
    if (!check(state() == "Available", "catalog check"))
        return 8;
    origin.responseDelays.insert("/core.zip", 800);
    runtime.featureCommand("core_download", {});
    runtime.featureCommand("core_cancel", {});
    QElapsedTimer late;
    late.start();
    waitFor([&] { return late.elapsed() > 1000; });
    if (!check(state() == "Cancelled" && runtime.snapshot().value("corePid") == pid,
               "cancel preserves session"))
        return 9;
    origin.responseDelays.clear();
    auto validZip = origin.payloads.value("/core.zip");
    auto corrupt = validZip;
    corrupt[1000] ^= 1;
    origin.payloads.insert("/core.zip", corrupt);
    runtime.featureCommand("core_download", {});
    if (!check(done() && state() == "Failed" && runtime.snapshot().value("corePid") == pid &&
                   runtime.snapshot().value("error") == "CoreIntegrityFailed",
               "corrupt archive rejected before extraction"))
        return 10;
    origin.payloads.insert("/core.zip", validZip);
    corrupt.clear();
    validZip.clear();
    runtime.featureCommand("core_download", {});
    if (!check(done() && state() == "Verified" && runtime.snapshot().value("corePid") == pid &&
                   origin.lastCookie.isEmpty(),
               "verified download does not switch core"))
        return 11;
    const auto id = platform::coreArtifact().value("id").toString();
    const auto managed = platform::managedCorePath(bundled, id);
    if (!check(platform::verifyCoreArtifact(managed, id) &&
                   platform::managedCorePath(bundled, "../escape").isEmpty(),
               "catalog identity and executable digest"))
        return 12;
    runtime.featureCommand("core_apply", {"confirmed"});
    if (!check(done() && state() == "Applied" && runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("corePid") != pid &&
                   runtime.snapshot().value("preferences").toObject().value("coreArtifactId") == id,
               "apply managed core"))
        return 13;
    const auto generation = runtime.snapshot().value("selectionGeneration");
    QTcpServer blocker;
    bool blocked = false;
    auto connection = QObject::connect(
        &runtime, &application::DesktopRuntime::changed, &runtime, [&](const QJsonObject& value) {
            if (value.value("status") == "Starting" && !blocked)
                blocked = blocker.listen(QHostAddress::LocalHost, mixed);
            if (blocked && !value.value("error").toString().isEmpty())
                blocker.close();
        });
    runtime.featureCommand("core_bundled", {"confirmed"});
    if (!check(done() && blocked && state() == "Failed" &&
                   runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("selectionGeneration") == generation &&
                   runtime.snapshot().value("preferences").toObject().value("coreArtifactId") == id,
               "startup failure restores managed core and config"))
        return 14;
    QObject::disconnect(connection);
    runtime.featureCommand("core_rollback", {"confirmed"});
    if (!check(done() && state() == "Applied" &&
                   runtime.snapshot().value("preferences").toObject().value("coreArtifactId") ==
                       "bundled",
               "rollback to bundled"))
        return 15;
    runtime.featureCommand("core_rollback", {"confirmed"});
    if (!check(done() && state() == "Applied" &&
                   runtime.snapshot().value("preferences").toObject().value("coreArtifactId") == id,
               "rollback retains previous managed core"))
        return 16;
    runtime.stop();
    application::DesktopRuntime reopened(store, services(store, bundled, source));
    reopened.initialize();
    if (!check(reopened.snapshot().value("preferences").toObject().value("coreArtifactId") == id,
               "selection survives reopen"))
        return 18;
    reopened.start(false);
    if (!check(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }) &&
                   reopened.snapshot().value("ready").toBool() &&
                   reopened.snapshot().value("coreVersion") == "v1.19.33",
               "managed core Ready after reopen"))
        return 19;
    reopened.stop();
    if (!check(QDir(QFileInfo(managed).absolutePath())
                   .entryList({"stage_*"}, QDir::Dirs | QDir::NoDotAndDotDot)
                   .isEmpty(),
               "staging released"))
        return 20;
    std::puts("Core official download, integrity, cancel, apply, compensation, rollback and reopen "
              "passed");
    return 0;
}
} // namespace quickclash::test
