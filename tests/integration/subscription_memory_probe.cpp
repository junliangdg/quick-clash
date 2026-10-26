#include "subscription_memory_probe.h"
#include "application/desktop_runtime.h"
#include "network_fixture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& predicate, int milliseconds = 30000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return predicate();
}
int port() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
struct Peak {
    quint64 privateWorkingSet = 0, workingSet = 0, privateCommit = 0, samples = 0;
    QJsonObject json() const {
        return {{"privateWorkingSet", QString::number(privateWorkingSet)},
                {"workingSet", QString::number(workingSet)},
                {"privateCommit", QString::number(privateCommit)},
                {"samples", QString::number(samples)}};
    }
};
} // namespace
int subscriptionMemoryProbe() {
#ifndef Q_OS_WIN
    return 77;
#else
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (std::holds_alternative<domain::Error>(store.open()))
        return 1;
    application::DesktopRuntime runtime(store);
    runtime.initialize();
    return subscriptionMemoryProbe(
        {[&] { return runtime.snapshot(); },
         [&](int mixed, int controller) { runtime.setPorts(mixed, controller); },
         [&](const QString& url) {
             runtime.addSubscription(url, QStringLiteral("Public memory fixture"));
         },
         [&] { runtime.start(false); },
         [&](const QString& id) { runtime.updateProfile(id); },
         [&] { runtime.stop(); },
         {},
         {}});
#endif
}
int subscriptionMemoryProbe(const SubscriptionProbeRuntime& runtime) {
#ifndef Q_OS_WIN
    Q_UNUSED(runtime)
    return 77;
#else
    auto mixed = port(), controller = port();
    while (mixed == controller)
        controller = port();
    runtime.ports(mixed, controller);
    TlsFixture origin;
    if (!origin.initialize())
        return 2;
    QByteArray source = "proxies:\n";
    for (int i = 0; i < 58; ++i)
        source += "- {name: node_" + QByteArray::number(i) +
                  ", type: http, server: 127.0.0.1, port: 9}\n";
    source += "proxy-groups:\n- {name: primary, type: select, proxies: [node_0, DIRECT]}\n"
              "dns: {enable: true, enhanced-mode: fake-ip, nameserver: [1.1.1.1]}\nrules:\n";
    for (int i = 0; i < 12000; ++i)
        source += "- DOMAIN-SUFFIX,site_" + QByteArray::number(i) + ".invalid,primary\n";
    source += "- MATCH,primary\n";
    origin.subscription = source;
    runtime.subscribe(QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort()));
    if (!waitFor([&] {
            return runtime.snapshot().value("profiles").toArray().size() == 1 &&
                   !runtime.snapshot().value("busy").toBool();
        }))
        return 3;
    runtime.start();
    if (!waitFor([&] {
            return runtime.snapshot().value("ready").toBool() &&
                   !runtime.snapshot().value("busy").toBool();
        }))
        return 4;
    const auto id = runtime.snapshot().value("selectedId").toString();
    const auto originalPid = runtime.snapshot().value("corePid");
    const auto originalGeneration = runtime.snapshot().value("selectionGeneration");
    std::array<Peak, 5> peaks;
    std::atomic<int> phase = 0;
    QString updatedNode = QStringLiteral("node_0");
    {
        std::jthread sampler([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                struct Counters {
                    PROCESS_MEMORY_COUNTERS_EX base;
                    SIZE_T privateWorkingSet;
                    SIZE_T sharedCommit;
                } counters{};
                counters.base.cb = sizeof(counters);
                if (GetProcessMemoryInfo(GetCurrentProcess(),
                                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                         sizeof(counters))) {
                    auto& peak = peaks[static_cast<size_t>(phase.load())];
                    peak.privateWorkingSet = std::max(
                        peak.privateWorkingSet, static_cast<quint64>(counters.privateWorkingSet));
                    peak.workingSet = std::max(peak.workingSet,
                                               static_cast<quint64>(counters.base.WorkingSetSize));
                    peak.privateCommit = std::max(peak.privateCommit,
                                                  static_cast<quint64>(counters.base.PrivateUsage));
                    ++peak.samples;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        const auto idle = [](int ms) {
            QElapsedTimer elapsed;
            elapsed.start();
            waitFor([&] { return elapsed.elapsed() >= ms; }, ms + 1000);
        };
        idle(2000);
        phase = 1;
        runtime.update(id); // Unchanged downloads should preserve the live core.
        if (!waitFor([&] { return runtime.snapshot().value("busy").toBool(); }) ||
            !waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }))
            return 5;
        if (runtime.snapshot().value("corePid") != originalPid ||
            !runtime.snapshot().value("ready").toBool() ||
            !runtime.snapshot().value("error").toString().isEmpty())
            return 8;
        phase = 2;
        origin.subscription.replace("site_0.invalid", "updated.invalid");
        const bool unicode =
            QCoreApplication::arguments().contains(QStringLiteral("--probe-unicode"));
        if (unicode) {
            // Introduce fallback glyphs only on update, as real subscription names can change.
            updatedNode =
                QStringLiteral("\U0001f1ef\U0001f1f5\u65e5\u672c\u9ad8\u901f02|CTCU|0.5x");
            origin.subscription.replace("primary",
                                        QStringLiteral("\u81ea\u52a8\u9009\u62e9").toUtf8());
        }
        if (QCoreApplication::arguments().contains(QStringLiteral("--probe-missing-glyph"))) {
            // Valid YAML text with an unassigned character exercises exhausted font fallback.
            updatedNode += QChar(0x0378);
        }
        origin.subscription.replace("node_0", updatedNode.toUtf8());
        runtime.update(id); // Changed content exercises preflight, validation and apply.
        if (!waitFor([&] { return runtime.snapshot().value("busy").toBool(); }) ||
            !waitFor([&] { return !runtime.snapshot().value("busy").toBool(); }))
            return 6;
        if (!runtime.snapshot().value("ready").toBool() ||
            runtime.snapshot().value("selectionGeneration") == originalGeneration)
            return 9;
        phase = 3;
        idle(6000);
        if (runtime.returnHome) {
            phase = 4;
            runtime.returnHome();
            idle(6000);
        }
    }
    const auto outbound = runtime.snapshot().value("outbound").toObject();
    const bool nodeMatched = outbound.value("node").toString() == updatedNode;
    const bool textObserved = runtime.homeContainsNode && runtime.homeContainsNode(updatedNode);
    QJsonObject result{
        {"scenario",
         "Public loopback HTTPS; 58 nodes / 12001 rules / DNS+Geo; no system proxy or TUN"},
        {"qtVersion", qVersion()},
        {"unicodeOnUpdate",
         QCoreApplication::arguments().contains(QStringLiteral("--probe-unicode"))},
        {"missingGlyphOnUpdate",
         QCoreApplication::arguments().contains(QStringLiteral("--probe-missing-glyph"))},
        {"sourceBytes", source.size()},
        {"unchangedKeptCore", true},
        {"changedApplied", true},
        {"before", peaks[0].json()},
        {"unchangedUpdate", peaks[1].json()},
        {"changedUpdate", peaks[2].json()},
        {"after", peaks[3].json()},
        {"home", peaks[4].json()},
        {"homeOutbound", outbound},
        {"updatedNodeSelected", nodeMatched},
        {"homeTextCheckAvailable", bool(runtime.homeContainsNode)},
        {"homeOutboundTextObserved", textObserved},
        {"error", runtime.snapshot().value("error")}};
    runtime.stop();
    std::cout << QJsonDocument(result).toJson().constData();
    if (!result.value("error").toString().isEmpty())
        return 7;
    return runtime.homeContainsNode && (!nodeMatched || !textObserved) ? 21 : 0;
#endif
}
} // namespace quickclash::test
