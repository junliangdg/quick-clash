#include "geo_update_probe.h"
#include "application/desktop_runtime.h"
#include "network_fixture.h"
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
bool waitFor(const std::function<bool()>& predicate, int limit = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < limit) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return predicate();
}
int port() {
    QTcpServer listener;
    return listener.listen(QHostAddress::LocalHost) ? listener.serverPort() : 0;
}
} // namespace
int geoUpdateProbe() {
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (std::holds_alternative<domain::Error>(store.open()))
        return 1;
    TlsFixture origin;
    if (!origin.initialize())
        return 2;
    for (const auto* name : {"geoip.dat", "geosite.dat"}) {
        QFile file(QStringLiteral(".cache/geo/") + QLatin1String(name));
        if (!file.open(QIODevice::ReadOnly))
            return 3;
        origin.payloads.insert(QByteArray("/") + name, file.readAll());
    }
    application::DesktopRuntime runtime(
        store, nullptr, nullptr, {},
        QUrl(QStringLiteral("https://127.0.0.1:%1/").arg(origin.serverPort())));
    runtime.initialize();
    const auto check = [&](bool success, const char* message) {
        if (!success)
            std::fprintf(stderr, "%s: %s\n", message,
                         QJsonDocument(runtime.snapshot()).toJson().constData());
        return success;
    };
    const auto done = [&] {
        return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
    };
    const auto result = [&] {
        return runtime.snapshot().value("features").toObject().value("geoResult").toString();
    };
    const auto snapshot = [&] {
        return runtime.snapshot().value("preferences").toObject().value("geoSnapshot").toObject();
    };
    auto mixed = port(), controller = port();
    while (mixed == controller)
        controller = port();
    runtime.setPorts(mixed, controller);
    const auto path = directory.filePath("geo_profile.yaml");
    {
        QFile profile(path);
        if (!profile.open(QIODevice::WriteOnly) ||
            profile.write("proxies: []\nproxy-groups: []\nrules: ['GEOIP,CN,DIRECT', "
                          "'GEOSITE,cn,DIRECT', 'MATCH,DIRECT']\n") <= 0)
            return 4;
    }
    runtime.importFile(QUrl::fromLocalFile(path).toString(), "Geo fixture");
    if (!check(done() && !runtime.snapshot().value("selectedId").toString().isEmpty(),
               "import uses bundled offline databases"))
        return 5;
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(), "bundled core ready"))
        return 6;
    auto pid = runtime.snapshot().value("corePid");
    const auto beforeFirst = runtime.snapshot().value("selectionGeneration");
    runtime.featureCommand("update_geo", {});
    if (!check(done() && result() == "Applied" && snapshot().size() == 2 &&
                   runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("corePid") != pid &&
                   runtime.snapshot().value("selectionGeneration") != beforeFirst &&
                   origin.lastCookie.isEmpty(),
               "both real databases validate and switch in one transaction"))
        return 7;
    const auto goodSnapshot = snapshot();
    if (!check(runtime.snapshot().value("preferences").toObject().contains("previousGeoSnapshot") &&
                   runtime.snapshot()
                       .value("preferences")
                       .toObject()
                       .value("previousGeoSnapshot")
                       .toObject()
                       .isEmpty(),
               "first managed update retains bundled history"))
        return 16;
    pid = runtime.snapshot().value("corePid");
    const auto goodGeneration = runtime.snapshot().value("selectionGeneration");
    runtime.featureCommand("update_geo", {});
    if (!check(done() && result() == "Unchanged" && snapshot() == goodSnapshot &&
                   runtime.snapshot().value("corePid") == pid &&
                   runtime.snapshot().value("selectionGeneration") == goodGeneration,
               "same resources verify and reuse without restart"))
        return 8;
    origin.responseStatuses.insert("/geosite.dat", 503);
    runtime.featureCommand("update_geo", {});
    if (!check(done() && result() == "Failed" && snapshot() == goodSnapshot &&
                   runtime.snapshot().value("corePid") == pid,
               "second resource failure keeps both previous references"))
        return 9;
    origin.responseStatuses.remove("/geosite.dat");
    const auto validSite = origin.payloads.value("/geosite.dat");
    origin.payloads.insert("/geosite.dat", "invalid database");
    runtime.featureCommand("update_geo", {});
    if (!check(done() && result() == "Failed" && snapshot() == goodSnapshot &&
                   runtime.snapshot().value("corePid") == pid,
               "official database validation failure keeps live core"))
        return 10;
    origin.payloads.insert("/geosite.dat", validSite);
    origin.responseDelays.insert("/geosite.dat", 1000);
    const auto beforeCancel = origin.requests.size();
    runtime.featureCommand("update_geo", {});
    if (!waitFor([&] { return origin.requests.size() >= beforeCancel + 2; }))
        return 11;
    runtime.featureCommand("cancel_geo", {});
    QElapsedTimer cancelled;
    cancelled.start();
    waitFor([&] { return cancelled.elapsed() >= 1100; });
    if (!check(done() && result() == "Cancelled" && snapshot() == goodSnapshot &&
                   runtime.snapshot().value("corePid") == pid &&
                   QDir(directory.filePath("cache/geo"))
                       .entryList(QDir::Files | QDir::Hidden)
                       .isEmpty(),
               "cancel second download rejects late response and releases staging"))
        return 12;
    origin.responseDelays.clear();
    bool cancelledValidation = false;
    const auto cancelValidation = QObject::connect(
        &runtime, &application::DesktopRuntime::changed, &runtime, [&](const QJsonObject&) {
            if (!cancelledValidation && result() == "Validating") {
                cancelledValidation = true;
                QTimer::singleShot(0, &runtime, [&] { runtime.featureCommand("cancel_geo", {}); });
            }
        });
    runtime.featureCommand("bundled_geo", {"confirmed"});
    if (!check(done() && cancelledValidation && result() == "Cancelled" &&
                   snapshot() == goodSnapshot && runtime.snapshot().value("corePid") == pid,
               "cancel validation preserves running core and preferences"))
        return 17;
    QObject::disconnect(cancelValidation);
    QTcpServer candidatePortBlocker;
    bool candidateBlocked = false;
    const auto applyFailure = QObject::connect(
        &runtime, &application::DesktopRuntime::changed, &runtime, [&](const QJsonObject& state) {
            if (state.value("status") == "Starting" && !candidateBlocked)
                candidateBlocked = candidatePortBlocker.listen(QHostAddress::LocalHost, mixed);
            if (candidateBlocked && !state.value("error").toString().isEmpty())
                candidatePortBlocker.close();
        });
    runtime.featureCommand("bundled_geo", {"confirmed"});
    if (!check(done() && candidateBlocked && result() == "Failed" && snapshot() == goodSnapshot &&
                   runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("selectionGeneration") == goodGeneration,
               "candidate startup failure compensates to previous resources and session"))
        return 18;
    QObject::disconnect(applyFailure);
    runtime.featureCommand("bundled_geo", {"confirmed"});
    if (!check(done() && result() == "Applied" && snapshot().isEmpty() &&
                   runtime.snapshot()
                           .value("preferences")
                           .toObject()
                           .value("previousGeoSnapshot")
                           .toObject() == goodSnapshot &&
                   runtime.snapshot().value("ready").toBool(),
               "explicit offline fallback validates bundled pair and retains history"))
        return 13;
    runtime.featureCommand("rollback_geo", {"confirmed"});
    if (!check(done() && result() == "Applied" && snapshot() == goodSnapshot &&
                   runtime.snapshot().value("ready").toBool(),
               "rollback restores retained pair"))
        return 14;
    runtime.stop();
    application::DesktopRuntime reopened(store);
    reopened.initialize();
    if (!check(
            reopened.snapshot().value("preferences").toObject().value("geoSnapshot").toObject() ==
                goodSnapshot,
            "resource references survive reopen"))
        return 15;
    reopened.featureCommand("bundled_geo", {"confirmed"});
    if (!check(waitFor([&] { return !reopened.snapshot().value("busy").toBool(); }),
               "bundled before schedule"))
        return 19;
    reopened.stop();
    const auto settings = store.write(
        "config/geo_update.json",
        {{"route", "Direct"}, {"intervalHours", 24}, {"lastAttempt", "2000-01-01T00:00:00Z"}});
    if (std::holds_alternative<domain::Error>(settings))
        return 20;
    application::DesktopRuntime scheduled(
        store, nullptr, nullptr, {},
        QUrl(QStringLiteral("https://127.0.0.1:%1/").arg(origin.serverPort())));
    scheduled.initialize();
    scheduled.enableBackgroundManagement();
    if (!check(waitFor(
                   [&] {
                       return scheduled.snapshot()
                           .value("geoUpdate")
                           .toObject()
                           .value("pending")
                           .toBool();
                   },
                   30000) &&
                   scheduled.snapshot()
                       .value("preferences")
                       .toObject()
                       .value("geoSnapshot")
                       .toObject()
                       .isEmpty() &&
                   scheduled.snapshot().value("ready").toBool(),
               "scheduled update validates without activating")) {
        std::fprintf(stderr, "scheduled state: %s\n",
                     QJsonDocument(scheduled.snapshot()).toJson().constData());
        return 21;
    }
    const auto scheduledPid = scheduled.snapshot().value("corePid");
    scheduled.featureCommand("apply_pending_geo", {"confirmed"});
    if (!check(waitFor([&] { return !scheduled.snapshot().value("busy").toBool(); }) &&
                   scheduled.snapshot().value("ready").toBool() &&
                   scheduled.snapshot().value("corePid") != scheduledPid &&
                   !scheduled.snapshot().value("geoUpdate").toObject().value("pending").toBool(),
               "explicit pending apply restarts and clears candidate"))
        return 22;
    scheduled.stop();
    std::puts("Geo pair transaction, repeat, failure, cancellation, bundled fallback and rollback "
              "passed");
    return 0;
}
} // namespace quickclash::test
