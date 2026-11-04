#include "proxy_latency_probe.h"
#include "application/desktop_runtime.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <iostream>

namespace quickclash::test {
namespace {
bool wait(const std::function<bool()>& condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 15000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    return condition();
}
int freePort() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
} // namespace
int proxyLatencyProbe() {
    QTcpServer health;
    if (!health.listen(QHostAddress::LocalHost))
        return 1;
    int requests = 0;
    QHash<QByteArray, int> targets;
    bool hold = false;
    QList<QPointer<QTcpSocket>> held;
    QObject::connect(&health, &QTcpServer::newConnection, &health, [&] {
        while (auto* socket = health.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (request.startsWith("CONNECT ") && request.contains("\r\n\r\n")) {
                    socket->setProperty("request", QByteArray{});
                    socket->write("HTTP/1.1 200 Connection Established\r\n\r\n");
                    return;
                }
                if (request.contains("\r\n\r\n") &&
                    (request.startsWith("HEAD ") || request.startsWith("GET "))) {
                    ++requests;
                    ++targets[request.split(' ').value(1)];
                    socket->setProperty("request", QByteArray{});
                    if (hold) {
                        held.append(socket);
                    } else {
                        // Ensure a measurable nonzero result even on a fast loopback host.
                        QTimer::singleShot(15, socket, [socket] {
                            socket->write("HTTP/1.1 204 No Content\r\nContent-Length: "
                                          "0\r\nConnection: close\r\n\r\n");
                            socket->disconnectFromHost();
                        });
                    }
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!std::holds_alternative<std::monostate>(store.open()))
        return 2;
    QJsonObject publishedControl;
    qint64 clockOffsetSeconds = 0;
    application::DesktopRuntime runtime(store, nullptr, nullptr, [&clockOffsetSeconds] {
        return QDateTime::currentDateTimeUtc().addSecs(clockOffsetSeconds);
    });
    QObject::connect(&runtime, &application::DesktopRuntime::changed, &runtime,
                     [&publishedControl](const QJsonObject& state) { publishedControl = state; });
    runtime.initialize();
    int mixed = freePort(), controller = freePort();
    while (mixed == controller)
        controller = freePort();
    runtime.setPorts(mixed, controller);
    QFile file(directory.filePath("nodes.yaml"));
    const auto yaml =
        QByteArray("proxies:\n  - {name: good, type: http, server: 127.0.0.1, port: ") +
        QByteArray::number(health.serverPort()) +
        "}\n  - {name: bad, type: http, server: 127.0.0.1, port: 9}\n"
        "proxy-groups:\n  - {name: parent, type: select, proxies: [auto, good, bad]}\n"
        "  - {name: batch, type: select, proxies: [auto, twin, other]}\n"
        "  - {name: auto, type: url-test, proxies: [bad, good], url: "
        "'http://fixture.invalid/health', "
        "interval: 86400, timeout: 500, lazy: false, tolerance: 0}\n"
        "  - {name: twin, type: url-test, proxies: [bad, good], url: "
        "'http://fixture.invalid/health', interval: 86400, timeout: 500, lazy: false}\n"
        "  - {name: other, type: url-test, proxies: [bad, good], url: "
        "'http://fixture.invalid/other', interval: 86400, timeout: 500, lazy: false}\n"
        "rules: ['MATCH,parent']\n";
    if (!file.open(QIODevice::WriteOnly) || file.write(yaml) != yaml.size())
        return 3;
    file.close();
    runtime.importFile(QUrl::fromLocalFile(file.fileName()).toString(), "Latency fixture");
    if (!wait([&] { return !runtime.snapshot().value("busy").toBool(); }))
        return 4;
    const auto profiles = runtime.snapshot().value("profiles").toArray();
    if (profiles.size() != 1)
        return 5;
    runtime.selectProfile(profiles.first().toObject().value("id").toString());
    if (!wait([&] { return !runtime.snapshot().value("busy").toBool(); }))
        return 6;
    runtime.start(false);
    if (!wait([&] {
            return runtime.snapshot().value("ready").toBool() &&
                   !runtime.snapshot().value("busy").toBool() && requests > 0;
        })) {
        std::cerr << "core fixture did not receive a health request: "
                  << QJsonDocument(runtime.snapshot()).toJson().constData();
        return 7;
    }
    const auto synchronized = [&] {
        const auto snapshot = runtime.snapshot();
        return snapshot.value("delays").toObject().value("good").toString().endsWith(" ms") &&
               snapshot.value("delays").toObject().value("bad") == "Failed" &&
               snapshot.value("outbound").toObject().value("node") == "good";
    };
    runtime.featureCommand("refresh_groups", {});
    if (!wait(synchronized)) {
        std::cerr << "core health history was not shared: "
                  << QJsonDocument(runtime.snapshot()).toJson().constData();
        return 8;
    }
    const auto detail = runtime.snapshot()
                            .value("delay_details")
                            .toObject()
                            .value("good")
                            .toObject()
                            .value("latest")
                            .toObject();
    if (detail.value("source") != "Core" || detail.value("time").toString().isEmpty())
        return 9;
    const auto expectedTime =
        QDateTime::fromString(detail.value("time").toString(), Qt::ISODateWithMs)
            .toString(Qt::ISODate);
    const auto controlTimes = publishedControl.value("features").toObject().value("delayTimes");
    if (expectedTime.isEmpty() || controlTimes.toObject().value("good") != expectedTime)
        return 20;
    const auto before = requests;
    for (int i = 0; i < 10; ++i)
        runtime.featureCommand("refresh_groups", {});
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 250) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    if (requests != before || !synchronized() ||
        publishedControl.value("features").toObject().value("delayTimes") != controlTimes)
        return 10;
    const auto progress = [&] {
        return runtime.snapshot().value("features").toObject().value("delayTest").toObject();
    };
    const auto record = [&](const QString& node, const QString& target) {
        const auto records = runtime.snapshot()
                                 .value("delay_details")
                                 .toObject()
                                 .value(node)
                                 .toObject()
                                 .value("records")
                                 .toArray();
        for (const auto& value : records)
            if (value.toObject().value("targetHash") == domain::ProxyLatency::targetHash(target))
                return value.toObject();
        return QJsonObject{};
    };
    const auto healthTarget = QStringLiteral("http://fixture.invalid/health");
    const auto otherTarget = QStringLiteral("http://fixture.invalid/other");
    const auto initialHealth = targets.value("/health");
    runtime.featureCommand("delay", {"good", "auto"});
    if (record("good", healthTarget).value("state") != "Testing" ||
        publishedControl.value("features")
            .toObject()
            .value("delayTimes")
            .toObject()
            .contains("good") ||
        !wait([&] { return progress().value("state") == "Completed"; }) ||
        targets.value("/health") != initialHealth + 1 ||
        record("good", healthTarget).value("state") != "Success" ||
        publishedControl.value("features")
            .toObject()
            .value("delayTimes")
            .toObject()
            .value("good")
            .toString()
            .isEmpty())
        return 13;
    const auto afterSingle = requests;
    runtime.featureCommand("delay", {"parent"});
    if (!wait([&] { return progress().value("state") == "Completed"; }) ||
        requests != afterSingle + 1 ||
        progress().value("targetHash") != domain::ProxyLatency::targetHash(healthTarget))
        return 14;
    const auto batchHealth = targets.value("/health");
    const auto batchOther = targets.value("/other");
    hold = true;
    runtime.featureCommand("delay_batch", {"batch"});
    if (progress().value("total") != 4 ||
        record("good", healthTarget).value("state") != "Testing" ||
        record("good", otherTarget).value("state") != "Testing" || !wait([&] {
            return targets.value("/health") == batchHealth + 1 &&
                   targets.value("/other") == batchOther + 1;
        }))
        return 15;
    runtime.featureCommand("cancel_delays", {});
    hold = false;
    for (const auto& socket : held)
        if (socket) {
            socket->write(
                "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        }
    held.clear();
    QElapsedTimer late;
    late.start();
    while (late.elapsed() < 100) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    runtime.featureCommand("refresh_groups", {});
    if (record("good", healthTarget).value("state") != "Cancelled" ||
        record("good", otherTarget).value("state") != "Cancelled")
        return 16;
    const auto finalHealth = targets.value("/health");
    const auto finalOther = targets.value("/other");
    runtime.featureCommand("delay_batch", {"batch"});
    if (!wait([&] { return progress().value("state") == "Completed"; }) ||
        progress().value("total") != 4 || progress().value("completed") != 4 ||
        progress().value("failed") != 2 || targets.value("/health") != finalHealth + 1 ||
        targets.value("/other") != finalOther + 1 ||
        record("good", healthTarget).value("state") != "Success" ||
        record("good", otherTarget).value("state") != "Success")
        return 17;
    const auto previousProgress = progress();
    const auto previousRequests = requests;
    runtime.featureCommand("delay", {"good", "https://injected.invalid/"});
    if (requests != previousRequests || progress() != previousProgress)
        return 18;
    runtime.featureCommand("delay_target", {"https://changed.invalid/"});
    if (record("good", healthTarget).value("state") != "Success" ||
        record("good", otherTarget).value("state") != "Success")
        return 19;
    runtime.selectProxy("parent", "bad");
    if (!wait([&] {
            return runtime.snapshot().value("outbound").toObject().value("node") == "bad";
        }) ||
        runtime.snapshot().value("delays").toObject().value("bad") != "Failed")
        return 11;
    // The projection is reused on unchanged core polls, but must still age and
    // recover if the wall clock jumps forward and then back.
    clockOffsetSeconds = 601;
    runtime.featureCommand("refresh_groups", {});
    if (!wait([&] {
            return record("good", healthTarget).value("state") == "Stale" &&
                   !publishedControl.value("features")
                        .toObject()
                        .value("delayTimes")
                        .toObject()
                        .contains("good");
        }))
        return 21;
    clockOffsetSeconds = 0;
    runtime.featureCommand("refresh_groups", {});
    if (!wait([&] {
            return record("good", healthTarget).value("state") == "Success" &&
                   publishedControl.value("features")
                       .toObject()
                       .value("delayTimes")
                       .toObject()
                       .contains("good");
        }))
        return 22;
    runtime.stop();
    if (!runtime.snapshot().value("delay_details").toObject().isEmpty() ||
        !publishedControl.value("features").toObject().value("delayTimes").toObject().isEmpty())
        return 12;
    std::cout << "Core health/manual latency: group target, nested exit, cross-group dedup, "
                 "parallel targets, cancellation/late replies, target isolation, clock changes, "
                 "readback and stop passed";
    return 0;
}
} // namespace quickclash::test
