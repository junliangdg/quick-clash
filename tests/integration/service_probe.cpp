#include "service_probe.h"
#include "application/desktop_runtime.h"
#include "infra/core/service_client.h"
#include "infra/core/service_protocol.h"
#include "platform/process_memory.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QNetworkProxy>
#include <QSaveFile>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <array>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& condition, int milliseconds = 16000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
QJsonObject command(infra::ServiceClient& client, const QString& action,
                    const QJsonObject& payload = {}) {
    bool done = false;
    QJsonObject value;
    client.command(action, payload, [&](domain::Result<QJsonObject> result) {
        if (const auto* object = std::get_if<QJsonObject>(&result))
            value = *object;
        else
            value = {{QStringLiteral("error"), std::get<domain::Error>(result).code}};
        done = true;
    });
    if (!waitFor([&] { return done; })) {
        client.close();
        return {{QStringLiteral("error"), QStringLiteral("ProbeTimeout")}};
    }
    return value;
}
QJsonObject exchange(QLocalSocket& socket, const QJsonObject& message) {
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    QByteArray bytes;
    if (!waitFor([&] {
            bytes += socket.readAll();
            return bytes.contains('\n') || socket.state() == QLocalSocket::UnconnectedState;
        }))
        return {};
    return QJsonDocument::fromJson(bytes.left(bytes.indexOf('\n'))).object();
}
QJsonObject startPayload() {
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost))
        return {};
    const auto port = reservation.serverPort();
    reservation.close();
    // A valid first start must retain the normal configuration budget after hello.
    const QByteArray yaml = QByteArray("#") + QByteArray(20 * 1024, 'x') +
                            "\nproxies: []\nproxy-groups:\n  - name: choose\n"
                            "    type: select\n    proxies: [DIRECT, REJECT]\n"
                            "rules: ['MATCH,DIRECT']\n";
    return {{QStringLiteral("yaml"), QString::fromLatin1(yaml.toBase64())},
            {QStringLiteral("mixedPort"), port},
            {QStringLiteral("mode"), QStringLiteral("rule")},
            {QStringLiteral("dnsMode"), QStringLiteral("profile")},
            {QStringLiteral("tun"), false}};
}
bool saveEvidence(const QString& output, const QJsonObject& evidence) {
    QSaveFile file(output);
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(evidence).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool tunIdentityProbe(infra::ServiceClient& client, QJsonObject& evidence) {
#ifndef Q_OS_WIN
    Q_UNUSED(client)
    evidence.insert(QStringLiteral("error"), QStringLiteral("WindowsOnly"));
    return false;
#else
    const auto initial = command(client, QStringLiteral("status"));
    evidence.insert(QStringLiteral("initial"), initial);
    if (!initial.value(QStringLiteral("clean")).toBool())
        return false;
    auto payload = startPayload();
    payload.insert(QStringLiteral("tun"), true);
    const auto started = command(client, QStringLiteral("start"), payload);
    evidence.insert(QStringLiteral("started"), started);
    const auto status = command(client, QStringLiteral("status"));
    evidence.insert(QStringLiteral("status"), status);
    const auto identity = status.value(QStringLiteral("tunIdentity")).toObject();
    const auto actualImage =
        QFileInfo(identity.value(QStringLiteral("clientImage")).toString()).canonicalFilePath();
    const auto expectedImage =
        QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const bool imageMatches =
        !actualImage.isEmpty() && !expectedImage.isEmpty() &&
        QString::compare(actualImage, expectedImage, Qt::CaseInsensitive) == 0;
    const bool identityMatches =
        imageMatches &&
        identity.value(QStringLiteral("epoch")) == started.value(QStringLiteral("epoch")) &&
        identity.value(QStringLiteral("corePid")) == started.value(QStringLiteral("pid")) &&
        !identity.value(QStringLiteral("guid")).toString().isEmpty() &&
        !identity.value(QStringLiteral("luid")).toString().isEmpty();
    evidence.insert(QStringLiteral("clientImageMatches"), imageMatches);
    evidence.insert(QStringLiteral("identityMatches"), identityMatches);
    const auto stopped = command(client, QStringLiteral("stop"));
    const auto clean = command(client, QStringLiteral("status"));
    evidence.insert(QStringLiteral("stopped"), stopped);
    evidence.insert(QStringLiteral("clean"), clean);
    const bool passed = started.value(QStringLiteral("pid")).toInt() > 0 &&
                        status.value(QStringLiteral("ready")).toBool() &&
                        status.value(QStringLiteral("tunState")) == QStringLiteral("On") &&
                        identityMatches && stopped.value(QStringLiteral("stopped")).toBool() &&
                        clean.value(QStringLiteral("clean")).toBool();
    evidence.insert(QStringLiteral("passed"), passed);
    return passed;
#endif
}
bool connectionProbe(QJsonObject& evidence) {
    std::array<QLocalSocket, 4> peers;
    QElapsedTimer timer;
    timer.start();
    for (auto& peer : peers) {
        peer.connectToServer(QStringLiteral("QuickClash.Service.v1"));
        if (!waitFor([&] { return peer.state() != QLocalSocket::ConnectingState; }) ||
            peer.state() != QLocalSocket::ConnectedState)
            return false;
    }
    // Cover a silent peer, a fragmented request, hello-only, and a pre-owner large frame.
    peers[1].write("{\"protocolVersion\":");
    const auto hello = exchange(peers[2], {{QStringLiteral("protocolVersion"), 1},
                                           {QStringLiteral("requestId"), QStringLiteral("1")},
                                           {QStringLiteral("sessionId"), QString{}},
                                           {QStringLiteral("engineEpoch"), QString{}},
                                           {QStringLiteral("action"), QStringLiteral("hello")},
                                           {QStringLiteral("payload"), QJsonObject{}}});
    peers[3].write(QByteArray(infra::kServiceHandshakeMaxBytes + 1, 'x'));
    const bool oversizedClosed =
        waitFor([&] { return peers[3].state() == QLocalSocket::UnconnectedState; });
    const auto oversizedMs = timer.elapsed();
    const bool reclaimed = waitFor([&] {
        for (const auto& peer : peers)
            if (peer.state() != QLocalSocket::UnconnectedState)
                return false;
        return true;
    });
    const auto reclaimedMs = timer.elapsed();
    infra::ServiceClient owner;
    const auto initial = command(owner, QStringLiteral("status"));
    QElapsedTimer lease;
    lease.start();
    waitFor([&] { return lease.elapsed() >= infra::kServiceHandshakeTimeoutMs + 500; });
    const auto later = command(owner, QStringLiteral("status"));
    const bool passed = !hello.value(QStringLiteral("result")).toObject().isEmpty() &&
                        oversizedClosed && oversizedMs < infra::kServiceHandshakeTimeoutMs &&
                        reclaimed && reclaimedMs < infra::kServiceHandshakeTimeoutMs + 2000 &&
                        initial.value(QStringLiteral("clean")).toBool() &&
                        later.value(QStringLiteral("clean")).toBool() && owner.connected();
    evidence = {{QStringLiteral("helloAccepted"), hello.contains(QStringLiteral("result"))},
                {QStringLiteral("oversizedClosedMs"), oversizedMs},
                {QStringLiteral("allReclaimedMs"), reclaimedMs},
                {QStringLiteral("ownerSurvivedDeadline"), owner.connected()},
                {QStringLiteral("initial"), initial},
                {QStringLiteral("later"), later},
                {QStringLiteral("passed"), passed}};
    return passed;
}
bool protocolProbe(QJsonObject& evidence) {
    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("QuickClash.Service.v1"));
    if (!waitFor([&] { return socket.state() != QLocalSocket::ConnectingState; }) ||
        socket.state() != QLocalSocket::ConnectedState)
        return false;
    const auto request = [](int id, const QString& action, const QString& session = QString{}) {
        return QJsonObject{{QStringLiteral("protocolVersion"), 1},
                           {QStringLiteral("requestId"), QString::number(id)},
                           {QStringLiteral("sessionId"), session},
                           {QStringLiteral("engineEpoch"), QString{}},
                           {QStringLiteral("action"), action},
                           {QStringLiteral("payload"), QJsonObject{}}};
    };
    auto incompatible = request(1, QStringLiteral("status"));
    incompatible.insert(QStringLiteral("protocolVersion"), 99);
    const auto rejected = exchange(socket, incompatible);
    const auto noHandshake = exchange(socket, request(2, QStringLiteral("status")));
    const auto hello = exchange(socket, request(3, QStringLiteral("hello")));
    const auto session = hello.value(QStringLiteral("result"))
                             .toObject()
                             .value(QStringLiteral("sessionId"))
                             .toString();
    auto start = request(4, QStringLiteral("start"), session);
    start.insert(QStringLiteral("payload"), startPayload());
    const auto first = exchange(socket, start);
    const auto replay = exchange(socket, start); // A lost start reply must not launch another core.
    auto changed = start;
    changed.insert(QStringLiteral("action"), QStringLiteral("stop"));
    socket.write(QJsonDocument(changed).toJson(QJsonDocument::Compact) + '\n');
    const bool disconnected =
        waitFor([&] { return socket.state() == QLocalSocket::UnconnectedState; });
    const bool passed =
        rejected.value(QStringLiteral("error")) == QStringLiteral("ServiceProtocolMismatch") &&
        noHandshake.value(QStringLiteral("error")) == QStringLiteral("ServiceHandshakeRequired") &&
        !session.isEmpty() && first == replay &&
        first.value(QStringLiteral("result")).toObject().value(QStringLiteral("pid")).toInt() > 0 &&
        disconnected;
    evidence.insert(QStringLiteral("protocol"),
                    QJsonObject{{QStringLiteral("incompatible"), rejected},
                                {QStringLiteral("beforeHandshake"), noHandshake},
                                {QStringLiteral("firstStart"), first},
                                {QStringLiteral("replayedExactly"), first == replay},
                                {QStringLiteral("changedIdDisconnected"), disconnected},
                                {QStringLiteral("passed"), passed}});
    return passed;
}
bool controllerDenied() {
#ifdef Q_OS_WIN
    WIN32_FIND_DATAW entry{};
    const auto search = FindFirstFileW(LR"(\\.\pipe\*)", &entry);
    if (search == INVALID_HANDLE_VALUE)
        return false;
    bool found = false, denied = true;
    do {
        if (!QString::fromWCharArray(entry.cFileName)
                 .startsWith(QStringLiteral("QuickClash.Core.")))
            continue;
        found = true;
        const auto path = QStringLiteral(R"(\\.\pipe\)") + QString::fromWCharArray(entry.cFileName);
        const auto pipe =
            CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_EXISTING, 0, nullptr);
        const auto error = GetLastError();
        denied &= pipe == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED;
        if (pipe != INVALID_HANDLE_VALUE)
            CloseHandle(pipe);
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    return found && denied;
#else
    return false;
#endif
}
bool delayBatchProbe(infra::ServiceClient& client, QJsonObject& evidence) {
    const auto initial = command(client, QStringLiteral("status"));
    evidence.insert(QStringLiteral("initial"), initial);
    if (!initial.value(QStringLiteral("clean")).toBool())
        return false;
    QTcpServer held;
    if (!held.listen(QHostAddress::LocalHost))
        return false;
    QObject::connect(&held, &QTcpServer::newConnection, &held, [&held] {
        while (auto* socket = held.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket] { (void)socket->readAll(); });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    auto payload = startPayload();
    QByteArray yaml = "proxies:\n";
    for (int index = 0; index < 96; ++index)
        yaml += "  - {name: fast" + QByteArray::number(index) +
                ", type: http, server: 127.0.0.1, port: 1}\n";
    for (int index = 0; index < 32; ++index)
        yaml += "  - {name: slow" + QByteArray::number(index) +
                ", type: http, server: 127.0.0.1, port: " + QByteArray::number(held.serverPort()) +
                "}\n";
    QTcpServer health;
    if (!health.listen(QHostAddress::LocalHost))
        return false;
    int healthRequests = 0;
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
                if (!request.contains("\r\n\r\n"))
                    return;
                ++healthRequests;
                QTimer::singleShot(15, socket, [socket] {
                    socket->write("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n"
                                  "Connection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    yaml += "  - {name: good, type: http, server: 127.0.0.1, port: " +
            QByteArray::number(health.serverPort()) + "}\n";
    yaml += "proxy-groups:\n  - {name: choose, type: select, proxies: [DIRECT, fast0]}\n"
            "rules: ['MATCH,DIRECT']\n";
    payload.insert(QStringLiteral("yaml"), QString::fromLatin1(yaml.toBase64()));
    const auto started = command(client, QStringLiteral("start"), payload);
    evidence.insert(QStringLiteral("started"), started);
    if (started.value(QStringLiteral("pid")).toInt() <= 0)
        return false;
    const auto groups = command(client, QStringLiteral("groups"));
    bool lost = false;
    const auto lostConnection =
        QObject::connect(&client, &infra::ServiceClient::lost, &client, [&] { lost = true; });
    bool passed = groups.value(QStringLiteral("proxies")).toObject().contains("slow31");
    const auto httpDelay = command(client, QStringLiteral("delay"),
                                   {{"name", "good"}, {"url", "http://fixture.invalid/health"}});
    const auto rejectedDelay =
        command(client, QStringLiteral("delay"),
                {{"name", "good"}, {"url", "http://user@fixture.invalid/health"}});
    passed &= httpDelay.value("delay").toInt() > 0 && healthRequests == 1 &&
              rejectedDelay.value("error") == QStringLiteral("ServiceDelayRejected");
    evidence.insert(QStringLiteral("httpDelay"), httpDelay);
    evidence.insert(QStringLiteral("rejectedDelay"), rejectedDelay);
    evidence.insert(QStringLiteral("httpRequests"), healthRequests);
    QList<qint64> trafficTimes;
    QElapsedTimer trafficClock;
    trafficClock.start();
    for (int sample = 0; sample < 4; ++sample) {
        const auto value = command(client, QStringLiteral("traffic"));
        passed &= value.value("up").isDouble() && value.value("down").isDouble();
        trafficTimes.append(trafficClock.elapsed());
    }
    QJsonArray observedTraffic;
    for (const auto time : std::as_const(trafficTimes))
        observedTraffic.append(time);
    for (qsizetype sample = 1; sample < trafficTimes.size(); ++sample)
        passed &= trafficTimes[sample] - trafficTimes[sample - 1] >= 750 &&
                  trafficTimes[sample] - trafficTimes[sample - 1] <= 1350;
    (void)command(client, QStringLiteral("traffic"), {{"enabled", false}});
    evidence.insert(QStringLiteral("trafficSamples"), observedTraffic);
    int cancelledCallbacks = 0;
    for (int index = 0; index < 32; ++index)
        client.command(QStringLiteral("delay"),
                       {{"name", QStringLiteral("slow%1").arg(index)},
                        {"url", QStringLiteral("https://fixture.invalid/cancel")}},
                       [&](domain::Result<QJsonObject>) { ++cancelledCallbacks; });
    client.cancelQueuedDelays();
    const auto batch = [&](const QString& prefix, int count) {
        int submitted = 0, completed = 0, active = 0, polls = 0;
        bool healthy = true, pollPending = false;
        QJsonObject errors;
        QElapsedTimer elapsed;
        elapsed.start();
        std::function<void()> pump;
        pump = [&] {
            while (!lost && active < 32 && submitted < count) {
                const auto name = prefix + QString::number(submitted++);
                ++active;
                client.command(
                    QStringLiteral("delay"),
                    {{QStringLiteral("name"), name},
                     {QStringLiteral("url"), QStringLiteral("https://fixture.invalid/health")}},
                    [&](domain::Result<QJsonObject> result) {
                        --active;
                        ++completed;
                        if (const auto* error = std::get_if<domain::Error>(&result)) {
                            errors.insert(error->code, errors.value(error->code).toInt() + 1);
                            healthy &= error->code == QStringLiteral("CoreRequestFailed") ||
                                       error->code == QStringLiteral("CoreRequestTimeout");
                        }
                    });
            }
        };
        QTimer poll;
        poll.setInterval(250);
        QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
            if (pollPending || lost)
                return;
            pollPending = true;
            client.command(QStringLiteral("status"), {}, [&](domain::Result<QJsonObject> state) {
                pollPending = false;
                ++polls;
                const auto* value = std::get_if<QJsonObject>(&state);
                healthy &= value && value->value(QStringLiteral("ready")).toBool() &&
                           value->value(QStringLiteral("pid")) == started.value("pid") &&
                           value->value(QStringLiteral("epoch")) == started.value("epoch");
            });
        });
        poll.start();
        const bool finished = waitFor([&] {
            pump();
            return lost || (completed == count && !pollPending);
        });
        poll.stop();
        const bool ok = finished && !lost && healthy && completed == count && polls > 0;
        evidence.insert(prefix, QJsonObject{{"submitted", submitted},
                                            {"completed", completed},
                                            {"polls", polls},
                                            {"elapsedMs", elapsed.elapsed()},
                                            {"errors", errors},
                                            {"passed", ok}});
        // Every callback above borrows this batch's stack. Close on failure before returning.
        if (!finished || active || pollPending)
            client.close();
        return ok;
    };
    passed &= batch(QStringLiteral("fast"), 96);
    if (passed)
        passed &= batch(QStringLiteral("slow"), 32);
    QObject::disconnect(lostConnection);
    const auto after = command(client, QStringLiteral("status"));
    passed &= !lost && after.value(QStringLiteral("ready")).toBool() &&
              after.value(QStringLiteral("pid")) == started.value("pid") &&
              after.value(QStringLiteral("epoch")) == started.value("epoch");
    const auto stopped = command(client, QStringLiteral("stop"));
    const auto clean = command(client, QStringLiteral("status"));
    evidence.insert(QStringLiteral("after"), after);
    evidence.insert(QStringLiteral("stopped"), stopped);
    evidence.insert(QStringLiteral("clean"), clean);
    evidence.insert(QStringLiteral("lost"), lost);
    passed &= stopped.value(QStringLiteral("stopped")).toBool() &&
              clean.value(QStringLiteral("clean")).toBool() && cancelledCallbacks <= 1;
    evidence.insert(QStringLiteral("cancelledCallbacks"), cancelledCallbacks);
    evidence.insert(QStringLiteral("passed"), passed);
    return passed;
}
bool tunDelayProbe(infra::ServiceClient& client, QJsonObject& evidence) {
    const auto initial = command(client, QStringLiteral("status"));
    client.close();
    evidence.insert(QStringLiteral("initial"), initial);
    if (!initial.value(QStringLiteral("clean")).toBool())
        return false;
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!directory.isValid() || !std::holds_alternative<std::monostate>(store.open()))
        return false;
    auto runtime = std::make_unique<application::DesktopRuntime>(store);
    runtime->initialize();
    QTcpServer mixed, controller;
    if (!mixed.listen(QHostAddress::LocalHost) || !controller.listen(QHostAddress::LocalHost))
        return false;
    runtime->setPorts(mixed.serverPort(), controller.serverPort());
    mixed.close();
    controller.close();
    QByteArray yaml = "proxies:\n";
    for (int index = 0; index < 96; ++index)
        yaml += "  - {name: node" + QByteArray::number(index) +
                ", type: http, server: 127.0.0.1, port: 1}\n";
    yaml += "proxy-groups:\n  - name: batch\n    type: select\n    proxies: [";
    for (int index = 0; index < 96; ++index)
        yaml += (index ? ", node" : "node") + QByteArray::number(index);
    yaml += "]\nrules: ['MATCH,DIRECT']\n";
    const auto saved = store.writeBytes(QStringLiteral("data/nodes.yaml"), yaml);
    if (!std::holds_alternative<std::monostate>(saved))
        return false;
    runtime->importFile(QUrl::fromLocalFile(directory.filePath("data/nodes.yaml")).toString(),
                        QStringLiteral("TUN delay fixture"));
    bool passed = waitFor([&] { return !runtime->snapshot().value("busy").toBool(); });
    const auto profiles = runtime->snapshot().value("profiles").toArray();
    if (!passed || profiles.size() != 1)
        return false;
    runtime->selectProfile(profiles.first().toObject().value("id").toString());
    passed &= waitFor([&] { return !runtime->snapshot().value("busy").toBool(); });
    runtime->setTun(true);
    passed &= waitFor([&] {
        const auto state = runtime->snapshot();
        return state.value("ready").toBool() && !state.value("busy").toBool() &&
               state.value("tunState") == QStringLiteral("On");
    });
    const auto started = runtime->snapshot();
    evidence.insert(QStringLiteral("started"), QJsonObject{{"pid", started.value("corePid")},
                                                           {"epoch", started.value("epoch")},
                                                           {"tunState", started.value("tunState")},
                                                           {"error", started.value("error")}});
    const auto progress = [&] {
        return runtime->snapshot().value("features").toObject().value("delayTest").toObject();
    };
    bool healthy = true;
    const auto observation =
        QObject::connect(runtime.get(), &application::DesktopRuntime::changed, runtime.get(),
                         [&](const QJsonObject& state) {
                             healthy &= state.value("ready").toBool() &&
                                        state.value("corePid") == started.value("corePid") &&
                                        state.value("epoch") == started.value("epoch");
                         });
    if (passed) {
        runtime->setDemands({QStringLiteral("proxies"), QStringLiteral("traffic")});
        runtime->featureCommand(QStringLiteral("delay_batch"), {QStringLiteral("batch")});
        passed &= waitFor([&] { return progress().value("state") == QStringLiteral("Completed"); });
        passed &= progress().value("total").toInt() == 96 &&
                  progress().value("completed").toInt() == 96 && healthy;
        evidence.insert(QStringLiteral("batch"), progress());
        runtime->featureCommand(QStringLiteral("delay_batch"), {QStringLiteral("batch")});
        runtime->featureCommand(QStringLiteral("cancel_delays"), {});
        passed &= progress().value("state") == QStringLiteral("Cancelled");
        evidence.insert(QStringLiteral("cancelled"), progress());
        runtime->featureCommand(QStringLiteral("delay"),
                                {QStringLiteral("node0"), QStringLiteral("batch")});
        passed &= waitFor([&] { return progress().value("state") == QStringLiteral("Completed"); });
        passed &= progress().value("total").toInt() == 1 &&
                  progress().value("completed").toInt() == 1 && healthy;
        evidence.insert(QStringLiteral("single"), progress());
    }
    QObject::disconnect(observation);
    runtime->stop();
    runtime.reset();
    QJsonObject clean;
    passed &= waitFor([&] {
        clean = command(client, QStringLiteral("status"));
        const bool stopped = clean.value(QStringLiteral("clean")).toBool();
        if (!stopped)
            client.close();
        return stopped;
    });
    evidence.insert(QStringLiteral("clean"), clean);
    evidence.insert(QStringLiteral("passed"), passed);
    return passed;
}
bool tunForwardProbe(infra::ServiceClient& client, QJsonObject& evidence, bool probeBusy = false) {
    const auto initial = command(client, QStringLiteral("status"));
    client.close();
    evidence.insert(QStringLiteral("initial"), initial);
    if (!initial.value(QStringLiteral("clean")).toBool())
        return false;

    QTcpServer proxy;
    if (!proxy.listen(QHostAddress::LocalHost))
        return false;
    int healthRequests = 0, forwardedRequests = 0;
    QJsonArray requestLines;
    QObject::connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
        while (auto* socket = proxy.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("replied").toBool())
                    return;
                const auto firstLine = request.left(request.indexOf("\r\n"));
                if (requestLines.size() < 16)
                    requestLines.append(QString::fromLatin1(firstLine));
                if (firstLine.startsWith("CONNECT ")) {
                    const auto target = firstLine.split(' ').value(1);
                    if (target != "203.0.113.7:38491" && target != "fixture.invalid:80") {
                        socket->abort();
                        return;
                    }
                    socket->setProperty("tunnelTarget", target);
                    socket->setProperty("request", QByteArray{});
                    socket->write("HTTP/1.1 200 Connection Established\r\n\r\n");
                    return;
                }
                const bool health = firstLine.startsWith("HEAD /health ") ||
                                    firstLine.startsWith("GET /health ") ||
                                    firstLine.startsWith("GET http://fixture.invalid/health ");
                const bool forwarded =
                    firstLine.startsWith("GET /quickclash-tun ") &&
                    socket->property("tunnelTarget").toByteArray() == "203.0.113.7:38491";
                if (!health && !forwarded) {
                    socket->abort();
                    return;
                }
                socket->setProperty("replied", true);
                if (health)
                    ++healthRequests;
                else
                    ++forwardedRequests;
                // A nonzero delay makes the loopback health check a valid latency result.
                QTimer::singleShot(15, socket, [socket, health] {
                    socket->write(health ? "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n"
                                           "Connection: close\r\n\r\n"
                                         : "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n"
                                           "Connection: close\r\n\r\ntun-forwarded");
                    socket->disconnectFromHost();
                });
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!directory.isValid() || !std::holds_alternative<std::monostate>(store.open()))
        return false;
    auto runtime = std::make_unique<application::DesktopRuntime>(store);
    runtime->initialize();
    QTcpServer mixed, controller;
    // Keep the controller outside the Windows ephemeral range until the core binds it.
    for (quint16 port = 30000; port < 40000 && !mixed.isListening(); ++port)
        (void)mixed.listen(QHostAddress::LocalHost, port);
    for (quint16 port = 40000; port < 49000 && !controller.isListening(); ++port)
        (void)controller.listen(QHostAddress::LocalHost, port);
    if (!mixed.isListening() || !controller.isListening())
        return false;
    const auto mixedPort = mixed.serverPort();
    runtime->setPorts(mixedPort, controller.serverPort());
    mixed.close();
    controller.close();
    const QByteArray yaml =
        QByteArray("proxies:\n  - {name: local-success, type: http, server: 127.0.0.1, port: ") +
        QByteArray::number(proxy.serverPort()) +
        "}\nproxy-groups:\n  - {name: fixture, type: url-test, proxies: [local-success], "
        "url: 'http://fixture.invalid/health', interval: 86400, lazy: true}\n"
        "rules: ['MATCH,fixture']\n";
    if (!std::holds_alternative<std::monostate>(
            store.writeBytes(QStringLiteral("data/tun_forward.yaml"), yaml)))
        return false;
    runtime->importFile(QUrl::fromLocalFile(directory.filePath("data/tun_forward.yaml")).toString(),
                        QStringLiteral("TUN forwarding fixture"));
    bool passed = waitFor([&] { return !runtime->snapshot().value("busy").toBool(); });
    const auto profiles = runtime->snapshot().value("profiles").toArray();
    if (!passed || profiles.size() != 1) {
        evidence.insert(QStringLiteral("importState"), runtime->snapshot());
        return false;
    }
    runtime->selectProfile(profiles.first().toObject().value("id").toString());
    passed &= waitFor([&] { return !runtime->snapshot().value("busy").toBool(); });
    runtime->start(false);
    const bool ordinaryReady = waitFor([&] {
        const auto state = runtime->snapshot();
        return state.value("ready").toBool() && !state.value("busy").toBool() &&
               state.value("coreExecutionMode") == QStringLiteral("Local");
    });
    const auto ordinary = runtime->snapshot();
    evidence.insert(QStringLiteral("ordinaryBeforeTun"),
                    QJsonObject{{"ready", ordinaryReady},
                                {"pid", ordinary.value("corePid")},
                                {"executionMode", ordinary.value("coreExecutionMode")},
                                {"tunState", ordinary.value("tunState")}});
    passed &= ordinaryReady;
    const auto measure = [&](bool throughTun, QJsonObject& measurement) {
        const auto started = runtime->snapshot();
        const auto healthBefore = healthRequests;
        const auto forwardedBefore = forwardedRequests;
        int samples = 0;
        qint64 previous = 0, maximumGap = 0;
        double peakUp = 0, peakDown = 0;
        QJsonObject observedTraffic;
        const auto observation = QObject::connect(
            runtime.get(), &application::DesktopRuntime::dataPublished, runtime.get(),
            [&](const application::RuntimeUpdate& update) {
                const auto traffic =
                    update.values.value("traffic").toObject().value("traffic").toObject();
                const auto time = traffic.value("time").toString().toLongLong();
                if (time <= 0 || time == previous)
                    return;
                if (previous > 0)
                    maximumGap = std::max(maximumGap, time - previous);
                previous = time;
                observedTraffic = traffic;
                peakUp = std::max(peakUp, traffic.value("up").toDouble());
                peakDown = std::max(peakDown, traffic.value("down").toDouble());
                ++samples;
            });
        runtime->setDemands({QStringLiteral("proxies"), QStringLiteral("traffic")});
        runtime->featureCommand(QStringLiteral("delay"),
                                {QStringLiteral("local-success"), QStringLiteral("fixture")});
        const auto progress = [&] {
            return runtime->snapshot().value("features").toObject().value("delayTest").toObject();
        };
        const bool delayCompleted = waitFor([&] {
            return progress().value("state") == QStringLiteral("Completed") &&
                   healthRequests > healthBefore;
        });
        const auto delay = progress();
        const bool delayPassed = delayCompleted && delay.value("completed").toInt() == 1 &&
                                 delay.value("failed").toInt() == 0;
        measurement.insert(QStringLiteral("delay"), delay);
        measurement.insert(QStringLiteral("delayPassed"), delayPassed);
        measurement.insert(QStringLiteral("healthRequests"), healthRequests - healthBefore);
        measurement.insert(
            QStringLiteral("delayDetails"),
            runtime->snapshot().value("delay_details").toObject().value("local-success"));

        // TEST-NET-3 has no listening server; only a TUN-routed CONNECT through the
        // loopback proxy can produce this response. Ordinary mode uses the same
        // target and upstream via the explicit mixed listener; neither uses WinINet.
        QTcpSocket socket;
        socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        QByteArray response, handshake;
        bool connectedTunnel = throughTun;
        const auto sendRequest = [&socket] {
            socket.write("GET /quickclash-tun HTTP/1.1\r\nHost: 203.0.113.7:38491\r\n"
                         "Connection: close\r\n\r\n");
        };
        QObject::connect(&socket, &QTcpSocket::connected, &socket, [&] {
            if (throughTun)
                sendRequest();
            else
                socket.write("CONNECT 203.0.113.7:38491 HTTP/1.1\r\n"
                             "Host: 203.0.113.7:38491\r\n\r\n");
        });
        QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&] {
            if (connectedTunnel) {
                response += socket.readAll();
                return;
            }
            handshake += socket.readAll();
            const auto end = handshake.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            if (!handshake.startsWith("HTTP/1.1 200")) {
                socket.abort();
                return;
            }
            connectedTunnel = true;
            response += handshake.mid(end + 4);
            sendRequest();
        });
        if (throughTun)
            socket.connectToHost(QStringLiteral("203.0.113.7"), 38491);
        else
            socket.connectToHost(QHostAddress::LocalHost, mixedPort);
        const bool finished = waitFor([&] {
            return response.contains("\r\n\r\ntun-forwarded") ||
                   socket.state() == QAbstractSocket::UnconnectedState;
        });
        const bool transferred = finished && response.startsWith("HTTP/1.1 200 OK\r\n") &&
                                 response.endsWith("\r\n\r\ntun-forwarded") &&
                                 forwardedRequests > forwardedBefore;
        measurement.insert(QStringLiteral("networkError"), socket.errorString());
        measurement.insert(QStringLiteral("forwarded"), transferred);
        measurement.insert(QStringLiteral("forwardedRequests"),
                           forwardedRequests - forwardedBefore);
        socket.abort();
        const bool traffic = waitFor([&] { return samples >= 3; });
        QObject::disconnect(observation);
        measurement.insert(QStringLiteral("trafficSamples"), samples);
        measurement.insert(QStringLiteral("trafficMaximumGapMs"), maximumGap);
        measurement.insert(QStringLiteral("trafficPeakUp"), peakUp);
        measurement.insert(QStringLiteral("trafficPeakDown"), peakDown);
        measurement.insert(QStringLiteral("lastTraffic"), observedTraffic);
        const auto after = runtime->snapshot();
        const bool identityStable = after.value("ready").toBool() &&
                                    after.value("corePid") == started.value("corePid") &&
                                    after.value("epoch") == started.value("epoch") &&
                                    after.value("tunState") == started.value("tunState");
        measurement.insert(QStringLiteral("identityStable"), identityStable);
        return delayPassed && transferred && traffic && maximumGap < 2000 && peakUp > 0 &&
               peakDown > 0 && identityStable;
    };
    QJsonObject ordinaryMeasurement, tunMeasurement, restoredMeasurement;
    const auto reverseWhileBusy = [&](bool desired, const QString& key) {
        const auto pending = runtime->snapshot();
        if (!pending.value("busy").toBool()) {
            evidence.insert(key, QJsonObject{{"busyObserved", false}});
            return false;
        }
        runtime->setTun(desired);
        const auto rejected = runtime->snapshot();
        const bool unchanged = rejected.value("busy").toBool() &&
                               rejected.value("selectedId") == pending.value("selectedId") &&
                               rejected.value("tunPending") == pending.value("tunPending") &&
                               rejected.value("tunDisabling") == pending.value("tunDisabling") &&
                               rejected.value("preferences").toObject().value("tunDesired") ==
                                   pending.value("preferences").toObject().value("tunDesired");
        const bool denied = unchanged && rejected.value("error") == "OperationUnavailable";
        evidence.insert(
            key,
            QJsonObject{
                {"busyObserved", true},
                {"operationUnavailable", denied},
                {"pending", pending.value("tunPending")},
                {"disabling", pending.value("tunDisabling")},
                {"desiredBefore", pending.value("preferences").toObject().value("tunDesired")},
                {"desiredAfter", rejected.value("preferences").toObject().value("tunDesired")}});
        return denied;
    };
    if (passed)
        passed &= measure(false, ordinaryMeasurement);
    evidence.insert(QStringLiteral("ordinaryMeasurement"), ordinaryMeasurement);
    if (passed) {
        runtime->setTun(true);
        if (probeBusy)
            passed &= reverseWhileBusy(false, QStringLiteral("enableBusy"));
        passed &= waitFor([&] {
            const auto state = runtime->snapshot();
            return state.value("ready").toBool() && !state.value("busy").toBool() &&
                   state.value("tunState") == QStringLiteral("On");
        });
    }
    const auto started = runtime->snapshot();
    evidence.insert(QStringLiteral("started"),
                    QJsonObject{{"pid", started.value("corePid")},
                                {"epoch", started.value("epoch")},
                                {"tunState", started.value("tunState")},
                                {"executionMode", started.value("coreExecutionMode")},
                                {"error", started.value("error")}});
    if (passed) {
        passed &= started.value("corePid").toInt() > 0 &&
                  started.value("coreExecutionMode") == QStringLiteral("Service") &&
                  !started.value("systemProxy").toBool() &&
                  started.value("preferences").toObject().value("tunDesired").toBool() &&
                  started.value("corePid") != ordinary.value("corePid");
        if (passed)
            passed &= measure(true, tunMeasurement);
        evidence.insert(QStringLiteral("tunMeasurement"), tunMeasurement);
        const auto ordinaryTarget =
            ordinaryMeasurement.value("delay").toObject().value("targetHash");
        const bool sameTarget =
            !ordinaryTarget.toString().isEmpty() &&
            ordinaryTarget == tunMeasurement.value("delay").toObject().value("targetHash");
        evidence.insert(QStringLiteral("sameDelayTarget"), sameTarget);
        passed &= sameTarget;
        runtime->setTun(false);
        if (probeBusy)
            passed &= reverseWhileBusy(true, QStringLiteral("disableBusy"));
        const bool ordinaryRestored = waitFor([&] {
            const auto state = runtime->snapshot();
            return state.value("ready").toBool() && !state.value("busy").toBool() &&
                   state.value("tunState") == QStringLiteral("Off") &&
                   state.value("coreExecutionMode") == QStringLiteral("Local") &&
                   state.value("corePid").toInt() > 0 &&
                   state.value("corePid") != started.value("corePid");
        });
        const auto restored = runtime->snapshot();
        evidence.insert(QStringLiteral("ordinaryAfterTun"),
                        QJsonObject{{"ready", ordinaryRestored},
                                    {"pid", restored.value("corePid")},
                                    {"executionMode", restored.value("coreExecutionMode")},
                                    {"tunState", restored.value("tunState")}});
        passed &= ordinaryRestored &&
                  !restored.value("preferences").toObject().value("tunDesired").toBool();
        if (passed)
            passed &= measure(false, restoredMeasurement);
    }
    evidence.insert(QStringLiteral("restoredMeasurement"), restoredMeasurement);
    evidence.insert(QStringLiteral("requestLines"), requestLines);
    runtime->stop();
    runtime.reset();
    QJsonObject clean;
    const bool cleaned = waitFor([&] {
        clean = command(client, QStringLiteral("status"));
        const bool stopped = clean.value(QStringLiteral("clean")).toBool();
        if (!stopped)
            client.close();
        return stopped;
    });
    evidence.insert(QStringLiteral("clean"), clean);
    passed &= cleaned;
    evidence.insert(QStringLiteral("passed"), passed);
    return passed;
}
bool realSubscriptionProbe(infra::ServiceClient& client, QJsonObject& evidence,
                           const QString& source, bool enableTun) {
    const auto initial = command(client, QStringLiteral("status"));
    const bool initiallyClean = initial.value(QStringLiteral("clean")).toBool() &&
                                !initial.value(QStringLiteral("coreRunning")).toBool();
    evidence.insert(QStringLiteral("serviceInitiallyClean"), initiallyClean);
    client.close();
    const QFileInfo input(source);
    if (!initiallyClean || !input.isAbsolute() || !input.isFile() || input.isSymLink()) {
        evidence.insert(QStringLiteral("failure"), QStringLiteral("Preflight"));
        return false;
    }
    // Generated sessions can contain real credentials. Keep temporary copies in
    // the audit directory and remove them on both success and failure.
    QTemporaryDir directory(input.dir().filePath(QStringLiteral("subscription_store_XXXXXX")));
    infra::JsonStore store(directory.path());
    if (!directory.isValid() || !std::holds_alternative<std::monostate>(store.open())) {
        evidence.insert(QStringLiteral("failure"), QStringLiteral("IsolatedStore"));
        return false;
    }
    auto runtime = std::make_unique<application::DesktopRuntime>(store);
    runtime->initialize();
    QString failure;
    const auto require = [&](bool condition, const char* stage) {
        if (!condition && failure.isEmpty())
            failure = QString::fromLatin1(stage);
        return condition;
    };
    QTcpServer mixed, controller;
    for (quint16 port = 30000; port < 40000 && !mixed.isListening(); ++port)
        (void)mixed.listen(QHostAddress::LocalHost, port);
    for (quint16 port = 40000; port < 49000 && !controller.isListening(); ++port)
        (void)controller.listen(QHostAddress::LocalHost, port);
    bool passed = require(mixed.isListening() && controller.isListening(), "FreePorts");
    const auto mixedPort = mixed.serverPort();
    if (passed) {
        runtime->setPorts(mixedPort, controller.serverPort());
        mixed.close();
        controller.close();
        runtime->importFile(QUrl::fromLocalFile(input.absoluteFilePath()).toString(),
                            QStringLiteral("Cached subscription"));
        const bool imported = waitFor(
            [&] {
                const auto state = runtime->snapshot();
                return !state.value("busy").toBool() &&
                       state.value("profiles").toArray().size() == 1;
            },
            45000);
        evidence.insert(QStringLiteral("cachedImport"), imported);
        passed = require(imported, "Import");
    }
    if (passed) {
        const auto profiles = runtime->snapshot().value("profiles").toArray();
        runtime->selectProfile(profiles.first().toObject().value("id").toString());
        const bool selected = waitFor(
            [&] {
                const auto state = runtime->snapshot();
                return !state.value("busy").toBool() &&
                       !state.value("selectedId").toString().isEmpty();
            },
            30000);
        evidence.insert(QStringLiteral("profileSelected"), selected);
        passed = require(selected, "SelectProfile");
    }
    if (passed) {
        runtime->start(false);
        const bool ready = waitFor(
            [&] {
                const auto state = runtime->snapshot();
                return state.value("ready").toBool() && !state.value("busy").toBool() &&
                       state.value("coreExecutionMode") == QStringLiteral("Local");
            },
            45000);
        evidence.insert(QStringLiteral("ordinaryReady"), ready);
        passed = require(ready, "OrdinaryStart");
    }
    QStringList candidates;
    QString winningNode;
    if (passed) {
        runtime->setDemands({QStringLiteral("proxies"), QStringLiteral("traffic")});
        const bool nodesReady = waitFor(
            [&] {
                return !runtime->snapshot()
                            .value("features")
                            .toObject()
                            .value("nodes")
                            .toArray()
                            .isEmpty();
            },
            20000);
        passed = require(nodesReady, "NodeList");
    }
    if (passed) {
        const auto state = runtime->snapshot();
        const auto nodes = state.value("features").toObject().value("nodes").toArray();
        QHash<QString, QString> types;
        for (const auto& node : nodes) {
            const auto object = node.toObject();
            types.insert(object.value("name").toString(), object.value("type").toString());
        }
        QSet<QString> selectable;
        for (const auto& value : state.value("groups").toArray()) {
            const auto group = value.toObject();
            if (group.value("name") != QStringLiteral("GLOBAL") ||
                !group.value("canSelect").toBool())
                continue;
            for (const auto& member : group.value("members").toArray())
                if (types.contains(member.toString()))
                    selectable.insert(member.toString());
        }
        // Mix protocols and locations instead of exhausting the probe budget on
        // the first group of similarly unavailable nodes.
        QHash<QString, QStringList> byType;
        QStringList typeOrder;
        for (const auto& node : nodes) {
            const auto object = node.toObject();
            const auto name = object.value("name").toString();
            const auto type = object.value("type").toString();
            if (!selectable.contains(name))
                continue;
            if (!byType.contains(type))
                typeOrder.append(type);
            byType[type].append(name);
        }
        for (int index = 0; index < 24 && candidates.size() < 24; ++index) {
            bool added = false;
            for (const auto& type : typeOrder) {
                const auto& entries = byType[type];
                if (index < entries.size() && candidates.size() < 24) {
                    candidates.append(entries[index]);
                    added = true;
                }
            }
            if (!added)
                break;
        }
        evidence.insert(QStringLiteral("nodeCount"), static_cast<int>(nodes.size()));
        evidence.insert(QStringLiteral("selectableNodeCount"), static_cast<int>(selectable.size()));
        evidence.insert(QStringLiteral("candidateBudget"), static_cast<int>(candidates.size()));
        passed = require(!candidates.isEmpty(), "SelectableNodes");
    }
    const auto freshDelay = [&](const QString& name, QJsonObject& result) {
        const auto progress = [&] {
            return runtime->snapshot().value("features").toObject().value("delayTest").toObject();
        };
        // Running is published synchronously before completion. An old Completed
        // result must never satisfy a new request that was rejected or ignored.
        bool runningObserved = false;
        const auto observer = QObject::connect(
            runtime.get(), &application::DesktopRuntime::changed, runtime.get(),
            [&](const QJsonObject& state) {
                const auto current =
                    state.value("features").toObject().value("delayTest").toObject();
                runningObserved |= current.value("state") == QStringLiteral("Running") &&
                                   current.value("scope") == QStringLiteral("node") &&
                                   current.value("total").toInt() == 1;
            });
        runtime->featureCommand(QStringLiteral("delay"), {name, QString{}});
        const bool completed =
            runningObserved &&
            waitFor(
                [&] {
                    const auto current = progress();
                    return current.value("state") == QStringLiteral("Completed") &&
                           current.value("scope") == QStringLiteral("node") &&
                           current.value("total").toInt() == 1 &&
                           current.value("completed").toInt() == 1;
                },
                9000);
        QObject::disconnect(observer);
        result = progress();
        return completed && result.value("failed").toInt() == 0 &&
               !result.value("targetHash").toString().isEmpty();
    };
    QJsonObject selectionEvidence;
    int attempted = 0;
    if (passed) {
        for (const auto& candidate : candidates) {
            ++attempted;
            QJsonObject progress;
            if (freshDelay(candidate, progress)) {
                winningNode = candidate;
                break;
            }
        }
        selectionEvidence.insert(QStringLiteral("attemptedNodes"), attempted);
        selectionEvidence.insert(QStringLiteral("successfulNodeFound"), !winningNode.isEmpty());
        passed = require(!winningNode.isEmpty(), "RealNodeDelay");
    }
    if (passed) {
        runtime->setMode(QStringLiteral("global"));
        const bool globalReady = waitFor(
            [&] {
                const auto state = runtime->snapshot();
                return state.value("ready").toBool() && !state.value("busy").toBool() &&
                       state.value("preferences").toObject().value("mode") ==
                           QStringLiteral("global");
            },
            30000);
        if (globalReady)
            runtime->selectProxy(QStringLiteral("GLOBAL"), winningNode);
        const bool selected =
            globalReady &&
            waitFor(
                [&] {
                    const auto state = runtime->snapshot();
                    return state.value("ready").toBool() && !state.value("busy").toBool() &&
                           state.value("outbound").toObject().value("node") == winningNode;
                },
                20000);
        selectionEvidence.insert(QStringLiteral("globalSelectionConfirmed"), selected);
        passed = require(selected, "SelectNode");
    }
    evidence.insert(QStringLiteral("selection"), selectionEvidence);
    const auto measure = [&](bool throughTun, QJsonObject& measurement) {
        const auto before = runtime->snapshot();
        const auto pid = before.value("corePid");
        const auto epoch = before.value("epoch");
        int samples = 0;
        qint64 previous = 0, maximumGap = 0;
        double peakUp = 0, peakDown = 0;
        const auto observer = QObject::connect(
            runtime.get(), &application::DesktopRuntime::dataPublished, runtime.get(),
            [&](const application::RuntimeUpdate& update) {
                const auto traffic =
                    update.values.value("traffic").toObject().value("traffic").toObject();
                const auto time = traffic.value("time").toString().toLongLong();
                if (time <= 0 || time == previous)
                    return;
                if (previous > 0)
                    maximumGap = std::max(maximumGap, time - previous);
                previous = time;
                ++samples;
                peakUp = std::max(peakUp, traffic.value("up").toDouble());
                peakDown = std::max(peakDown, traffic.value("down").toDouble());
            });
        QJsonObject delay;
        int delayAttempts = 0;
        bool delayPassed = false;
        while (!delayPassed && delayAttempts < 3) {
            ++delayAttempts;
            delayPassed = freshDelay(winningNode, delay);
        }
        measurement.insert(QStringLiteral("delayAttempts"), delayAttempts);
        measurement.insert(QStringLiteral("delayPassed"), delayPassed);
        measurement.insert(QStringLiteral("delayTargetHash"), delay.value("targetHash"));

        // Hold one verified TLS socket open until connection polling identifies
        // the exact source port. A successful request or aggregate background
        // traffic alone does not prove this particular connection used TUN.
        runtime->setDemands(
            {QStringLiteral("proxies"), QStringLiteral("traffic"), QStringLiteral("connections")});
        QSslSocket socket;
        socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket.setReadBufferSize(65536);
        socket.setProxy(throughTun ? QNetworkProxy(QNetworkProxy::NoProxy)
                                   : QNetworkProxy(QNetworkProxy::HttpProxy,
                                                   QStringLiteral("127.0.0.1"), mixedPort));
        socket.connectToHostEncrypted(QStringLiteral("speed.cloudflare.com"), 443);
        const bool tlsVerified = waitFor(
                                     [&] {
                                         return socket.isEncrypted() ||
                                                socket.state() == QAbstractSocket::UnconnectedState;
                                     },
                                     16000) &&
                                 socket.isEncrypted();
        bool chainObserved = false;
        bool hostObserved = false;
        bool selectedChainObserved = false;
        const int sourcePort = socket.localPort();
        if (tlsVerified) {
            socket.write("GET /__down?bytes=1048576 HTTP/1.1\r\n"
                         "Host: speed.cloudflare.com\r\nConnection: keep-alive\r\n\r\n");
        }
        const bool responseReceived =
            tlsVerified &&
            waitFor(
                [&] {
                    return socket.bytesAvailable() > 0 ||
                           socket.state() == QAbstractSocket::UnconnectedState;
                },
                16000) &&
            socket.bytesAvailable() > 0;
        const bool inspected =
            responseReceived &&
            waitFor(
                [&] {
                    for (const auto& value : runtime->snapshot().value("connections").toArray()) {
                        const auto connection = value.toObject();
                        const auto metadata = connection.value("metadata").toObject();
                        const auto chains = connection.value("chains").toArray();
                        if (metadata.value("sourcePort").toVariant().toInt() != sourcePort)
                            continue;
                        hostObserved =
                            metadata.value("host") == QStringLiteral("speed.cloudflare.com");
                        selectedChainObserved = chains.contains(winningNode) &&
                                                !chains.contains(QStringLiteral("DIRECT"));
                        chainObserved = hostObserved && selectedChainObserved;
                        if (chainObserved)
                            break;
                    }
                    return chainObserved || socket.state() == QAbstractSocket::UnconnectedState;
                },
                6000);
        const auto response = socket.readAll();
        const bool httpOk =
            response.startsWith("HTTP/1.1 200 ") || response.startsWith("HTTP/1.0 200 ");
        const bool forwarded =
            tlsVerified && responseReceived && httpOk && inspected && chainObserved;
        const bool traffic =
            waitFor([&] { return samples >= 3 && peakUp > 0 && peakDown > 0; }, 5000);
        measurement.insert(QStringLiteral("forwarded"), forwarded);
        measurement.insert(QStringLiteral("tlsVerified"), tlsVerified);
        measurement.insert(QStringLiteral("responseReceived"), responseReceived);
        measurement.insert(QStringLiteral("httpOk"), httpOk);
        measurement.insert(QStringLiteral("targetConnectionObserved"), hostObserved);
        measurement.insert(QStringLiteral("selectedChainObserved"), selectedChainObserved);
        measurement.insert(QStringLiteral("selectedNodeChainObserved"), chainObserved);
        measurement.insert(QStringLiteral("responseBytes"), static_cast<int>(response.size()));
        measurement.insert(QStringLiteral("networkErrorCode"), static_cast<int>(socket.error()));
        socket.abort();
        QObject::disconnect(observer);
        const auto after = runtime->snapshot();
        const bool stable = after.value("ready").toBool() && after.value("corePid") == pid &&
                            after.value("epoch") == epoch &&
                            after.value("outbound").toObject().value("node") == winningNode;
        measurement.insert(QStringLiteral("trafficSamples"), samples);
        measurement.insert(QStringLiteral("trafficMaximumGapMs"), maximumGap);
        measurement.insert(QStringLiteral("trafficPeakUp"), peakUp);
        measurement.insert(QStringLiteral("trafficPeakDown"), peakDown);
        measurement.insert(QStringLiteral("identityAndSelectionStable"), stable);
        return delayPassed && forwarded && traffic && maximumGap < 2000 && peakUp > 0 &&
               peakDown > 0 && stable;
    };
    QJsonObject ordinary, tun, restored, resources;
    bool resourcesPassed = true;
    if (passed)
        passed = require(measure(false, ordinary), "OrdinaryMeasurement");
    evidence.insert(QStringLiteral("ordinary"), ordinary);
    if (passed && enableTun) {
        runtime->setTun(true);
        const bool ready = waitFor(
            [&] {
                const auto state = runtime->snapshot();
                return state.value("ready").toBool() && !state.value("busy").toBool() &&
                       state.value("tunState") == QStringLiteral("On") &&
                       state.value("coreExecutionMode") == QStringLiteral("Service") &&
                       state.value("outbound").toObject().value("node") == winningNode;
            },
            45000);
        evidence.insert(QStringLiteral("tunReady"), ready);
        passed = require(ready, "TunStartAndRestoreSelection");
        if (passed)
            passed = require(measure(true, tun), "TunMeasurement");
        if (ready) {
            QJsonArray samples;
            QString lastSample;
            bool consistent = true;
            const auto observer = QObject::connect(
                runtime.get(), &application::DesktopRuntime::dataPublished, runtime.get(),
                [&](const application::RuntimeUpdate& update) {
                    const auto observation = update.values.value("resources").toObject();
                    const auto sample = observation.value("memorySample").toString();
                    if (!observation.value("memoryComplete").toBool() || sample.isEmpty() ||
                        sample == lastSample)
                        return;
                    const auto memory = observation.value("memory").toObject();
                    const auto host = memory.value("host").toObject();
                    const auto service = memory.value("service").toObject();
                    const auto clientMemory = memory.value("client").toObject();
                    const auto core = memory.value("core").toObject();
                    bool coherent = core.value("pid").toString().toULongLong() ==
                                    runtime->snapshot().value("corePid").toVariant().toULongLong();
                    QJsonObject hostMetrics, serviceMetrics, clientMetrics, coreMetrics;
                    for (const auto* field : {"privateWorkingSet", "workingSet", "privateCommit"}) {
                        bool hostOk = false, serviceOk = false, clientOk = false, coreOk = false;
                        const auto key = QLatin1String(field);
                        const auto hostValue = host.value(key).toString().toULongLong(&hostOk);
                        const auto serviceValue =
                            service.value(key).toString().toULongLong(&serviceOk);
                        const auto clientValue =
                            clientMemory.value(key).toString().toULongLong(&clientOk);
                        const auto coreValue = core.value(key).toString().toULongLong(&coreOk);
                        coherent &= hostOk && serviceOk && clientOk && coreOk && hostValue > 0 &&
                                    serviceValue > 0 && coreValue > 0 &&
                                    clientValue == hostValue + serviceValue;
                        hostMetrics.insert(key, host.value(key));
                        serviceMetrics.insert(key, service.value(key));
                        clientMetrics.insert(key, clientMemory.value(key));
                        coreMetrics.insert(key, core.value(key));
                    }
                    lastSample = sample;
                    consistent &= coherent;
                    samples.append(QJsonObject{{"host", hostMetrics},
                                               {"service", serviceMetrics},
                                               {"client", clientMetrics},
                                               {"core", coreMetrics},
                                               {"coherent", coherent}});
                });
            runtime->setDemands({QStringLiteral("proxies"), QStringLiteral("traffic"),
                                 QStringLiteral("connections"), QStringLiteral("resources")});
            const bool complete = waitFor([&] { return samples.size() >= 3; }, 20000);
            QObject::disconnect(observer);
            resourcesPassed = complete && consistent;
            resources.insert(QStringLiteral("complete"), complete);
            resources.insert(QStringLiteral("consistent"), consistent);
            resources.insert(QStringLiteral("samples"), samples);
        }
        if (passed) {
            runtime->setTun(false);
            const bool readyAgain = waitFor(
                [&] {
                    const auto state = runtime->snapshot();
                    return state.value("ready").toBool() && !state.value("busy").toBool() &&
                           state.value("tunState") == QStringLiteral("Off") &&
                           state.value("coreExecutionMode") == QStringLiteral("Local") &&
                           state.value("outbound").toObject().value("node") == winningNode;
                },
                45000);
            evidence.insert(QStringLiteral("ordinaryRestored"), readyAgain);
            passed = require(readyAgain, "OrdinaryRestore");
            if (passed)
                passed = require(measure(false, restored), "RestoredMeasurement");
        }
    }
    evidence.insert(QStringLiteral("tun"), tun);
    evidence.insert(QStringLiteral("restored"), restored);
    evidence.insert(QStringLiteral("resources"), resources);
    if (enableTun && passed) {
        const auto target = ordinary.value("delayTargetHash");
        const bool sameTarget = !target.toString().isEmpty() &&
                                target == tun.value("delayTargetHash") &&
                                target == restored.value("delayTargetHash");
        evidence.insert(QStringLiteral("sameDelayTarget"), sameTarget);
        passed = require(sameTarget, "DelayTargetChanged");
    }
    if (enableTun && passed)
        passed = require(resourcesPassed, "ResourceObservation");
    runtime->stop();
    runtime.reset();
    QJsonObject clean;
    const bool cleaned = waitFor(
        [&] {
            clean = command(client, QStringLiteral("status"));
            if (!clean.value("clean").toBool())
                client.close();
            return clean.value("clean").toBool() && !clean.value("coreRunning").toBool();
        },
        20000);
    evidence.insert(QStringLiteral("serviceClean"), cleaned);
    passed = require(cleaned, "ServiceCleanup") && passed;
    if (!failure.isEmpty())
        evidence.insert(QStringLiteral("failure"), failure);
    evidence.insert(QStringLiteral("passed"), passed);
    return passed;
}
bool tunRecoveryProbe(infra::ServiceClient& client, QJsonObject& evidence, const QString& output,
                      bool rejectWhileRecovering = false) {
#ifndef Q_OS_WIN
    Q_UNUSED(client)
    Q_UNUSED(output)
    Q_UNUSED(rejectWhileRecovering)
    evidence.insert(QStringLiteral("error"), QStringLiteral("WindowsOnly"));
    return false;
#else
    const auto initial = command(client, QStringLiteral("status"));
    client.close();
    evidence.insert(QStringLiteral("initial"), initial);
    if (!initial.value(QStringLiteral("clean")).toBool())
        return false;

    QTcpServer target;
    if (!target.listen(QHostAddress::LocalHost))
        return false;
    int targetConnections = 0, targetRequests = 0;
    QJsonArray targetRequestLines;
    QObject::connect(&target, &QTcpServer::newConnection, &target, [&] {
        while (auto* socket = target.nextPendingConnection()) {
            ++targetConnections;
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("replied").toBool())
                    return;
                socket->setProperty("replied", true);
                const auto firstLine = request.left(request.indexOf("\r\n"));
                if (targetRequestLines.size() < 8)
                    targetRequestLines.append(QString::fromLatin1(firstLine));
                const auto absolute = QByteArray("GET http://127.0.0.1:") +
                                      QByteArray::number(target.serverPort()) + "/recovery ";
                if (firstLine.startsWith("GET /recovery ") || firstLine.startsWith(absolute)) {
                    ++targetRequests;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n"
                                  "Connection: close\r\n\r\nrecovered");
                }
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!directory.isValid() || !std::holds_alternative<std::monostate>(store.open()))
        return false;
    auto runtime = std::make_unique<application::DesktopRuntime>(store);
    runtime->initialize();
    QTcpServer mixed, controller;
    // Avoid choosing an ephemeral port that a different connection can acquire between
    // closing the reservations and starting the ordinary controller after TUN exits.
    for (quint16 port = 30000; port < 40000 && !mixed.isListening(); ++port)
        (void)mixed.listen(QHostAddress::LocalHost, port);
    for (quint16 port = 40000; port < 49000 && !controller.isListening(); ++port)
        (void)controller.listen(QHostAddress::LocalHost, port);
    if (!mixed.isListening() || !controller.isListening())
        return false;
    const auto mixedPort = mixed.serverPort();
    evidence.insert(QStringLiteral("mixedPort"), mixedPort);
    evidence.insert(QStringLiteral("controllerPort"), controller.serverPort());
    runtime->setPorts(mixedPort, controller.serverPort());
    mixed.close();
    controller.close();
    const auto forwardThroughMixed = [&](const QString& prefix) {
        const auto requestsBefore = targetRequests;
        QTcpSocket socket;
        socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        QByteArray response;
        QObject::connect(&socket, &QTcpSocket::connected, &socket, [&] {
            const auto authority =
                QByteArray("127.0.0.1:") + QByteArray::number(target.serverPort());
            socket.write("GET http://" + authority + "/recovery HTTP/1.1\r\nHost: " + authority +
                         "\r\nConnection: close\r\n\r\n");
        });
        QObject::connect(&socket, &QTcpSocket::readyRead, &socket,
                         [&] { response += socket.readAll(); });
        socket.connectToHost(QHostAddress::LocalHost, mixedPort);
        const bool completed = waitFor([&] {
            return response.contains("\r\n\r\nrecovered") ||
                   socket.state() == QAbstractSocket::UnconnectedState;
        });
        const bool forwarded = completed && response.startsWith("HTTP/1.1 200 OK\r\n") &&
                               response.endsWith("\r\n\r\nrecovered") &&
                               targetRequests == requestsBefore + 1;
        evidence.insert(prefix + QStringLiteral("Forwarded"), forwarded);
        evidence.insert(prefix + QStringLiteral("Response"),
                        QString::fromUtf8(response.left(1024)));
        evidence.insert(prefix + QStringLiteral("SocketError"), socket.errorString());
        evidence.insert(prefix + QStringLiteral("Completed"), completed);
        socket.abort();
        return forwarded;
    };
    const QByteArray yaml = "proxies: []\nproxy-groups: []\nrules: ['MATCH,DIRECT']\n";
    if (!std::holds_alternative<std::monostate>(
            store.writeBytes(QStringLiteral("data/tun_recovery.yaml"), yaml)))
        return false;
    runtime->importFile(
        QUrl::fromLocalFile(directory.filePath("data/tun_recovery.yaml")).toString(),
        QStringLiteral("TUN recovery fixture"));
    bool passed = waitFor([&] { return !runtime->snapshot().value("busy").toBool(); });
    const auto profiles = runtime->snapshot().value("profiles").toArray();
    if (!passed || profiles.size() != 1) {
        evidence.insert(QStringLiteral("importState"), runtime->snapshot());
        return false;
    }
    runtime->selectProfile(profiles.first().toObject().value("id").toString());
    passed &= waitFor([&] { return !runtime->snapshot().value("busy").toBool(); });
    runtime->start(false);
    passed &= waitFor([&] {
        const auto state = runtime->snapshot();
        return state.value("ready").toBool() && !state.value("busy").toBool() &&
               state.value("coreExecutionMode") == QStringLiteral("Local");
    });
    if (passed)
        passed &= forwardThroughMixed(QStringLiteral("ordinaryBeforeTun"));
    runtime->enableBackgroundManagement();
    if (passed) {
        runtime->setTun(true);
        passed &= waitFor([&] {
            const auto state = runtime->snapshot();
            return state.value("ready").toBool() && !state.value("busy").toBool() &&
                   state.value("coreExecutionMode") == QStringLiteral("Service") &&
                   state.value("tunState") == QStringLiteral("On");
        });
    }

    // A separate elevated driver checks that each PID is the registered service's child
    // before terminating it. The ordinary client cannot terminate a SYSTEM-owned process.
    int waitingAttempt = 0, busyRejected = 0;
    const auto observation = QObject::connect(
        runtime.get(), &application::DesktopRuntime::changed, runtime.get(),
        [&](const QJsonObject& state) {
            if (!rejectWhileRecovering || waitingAttempt < 1 || waitingAttempt > 2 ||
                busyRejected >= waitingAttempt || !state.value("busy").toBool())
                return;
            const auto failedAttempt = waitingAttempt;
            QTimer::singleShot(0, runtime.get(), [&, failedAttempt] {
                const auto before = runtime->snapshot();
                if (waitingAttempt != failedAttempt || busyRejected >= failedAttempt ||
                    !before.value("busy").toBool())
                    return;
                // A rejected command is not an explicit retry and must not reset
                // the short-failure budget for the recovery already in flight.
                runtime->setTun(false);
                const auto after = runtime->snapshot();
                if (after.value("busy").toBool() &&
                    after.value("error") == QStringLiteral("OperationUnavailable"))
                    ++busyRejected;
            });
        });
    QJsonArray attempts;
    for (int attempt = 1; passed && attempt <= 3; ++attempt) {
        const auto before = runtime->snapshot();
        const auto pid = before.value("corePid").toInt();
        const auto epoch = before.value("epoch").toString();
        const auto base = output + QStringLiteral(".kill_%1").arg(attempt);
        const QJsonObject request{{"attempt", attempt}, {"pid", pid}, {"epoch", epoch}};
        if (pid <= 0 || epoch.isEmpty() || before.value("tunState") != QStringLiteral("On") ||
            !saveEvidence(base + QStringLiteral(".request.json"), request)) {
            passed = false;
            break;
        }
        const auto responsePath = base + QStringLiteral(".response.json");
        waitingAttempt = attempt;
        const bool answered = waitFor([&] { return QFileInfo::exists(responsePath); }, 30000);
        QFile responseFile(responsePath);
        const auto response = answered && responseFile.open(QIODevice::ReadOnly)
                                  ? QJsonDocument::fromJson(responseFile.readAll()).object()
                                  : QJsonObject{};
        const bool injected =
            response.value("attempt").toInt() == attempt && response.value("pid").toInt() == pid &&
            response.value("epoch").toString() == epoch && response.value("verified").toBool() &&
            response.value("terminated").toBool();
        if (!injected) {
            attempts.append(QJsonObject{{"request", request}, {"response", response}});
            passed = false;
            break;
        }
        const bool recovered = waitFor(
            [&] {
                const auto state = runtime->snapshot();
                if (!state.value("ready").toBool() || state.value("busy").toBool() ||
                    state.value("corePid").toInt() <= 0 || state.value("corePid").toInt() == pid)
                    return false;
                return attempt < 3
                           ? state.value("coreExecutionMode") == QStringLiteral("Service") &&
                                 state.value("tunState") == QStringLiteral("On")
                           : state.value("coreExecutionMode") == QStringLiteral("Local") &&
                                 state.value("tunState") == QStringLiteral("Blocked");
            },
            30000);
        const auto after = runtime->snapshot();
        waitingAttempt = 0;
        attempts.append(QJsonObject{{"request", request},
                                    {"response", response},
                                    {"recovered", recovered},
                                    {"pid", after.value("corePid")},
                                    {"epoch", after.value("epoch")},
                                    {"executionMode", after.value("coreExecutionMode")},
                                    {"tunState", after.value("tunState")},
                                    {"error", after.value("error")}});
        passed &= recovered;
    }
    QObject::disconnect(observation);
    evidence.insert(QStringLiteral("attempts"), attempts);
    if (rejectWhileRecovering) {
        evidence.insert(QStringLiteral("busyRejectedDuringRecovery"), busyRejected);
        passed &= busyRejected == 2;
    }
    const auto fallback = runtime->snapshot();
    bool exhausted = false;
    for (const auto& entry : fallback.value("errors").toArray())
        exhausted |= entry.toObject().value("code") == QStringLiteral("TunRecoveryExhausted");
    evidence.insert(QStringLiteral("exhausted"), exhausted);
    passed &= exhausted && fallback.value("preferences").toObject().value("tunDesired").toBool() &&
              !fallback.value("systemProxy").toBool();

    if (passed) {
        // Verify actual HTTP forwarding through the local mixed listener after fallback.
        passed &= forwardThroughMixed(QStringLiteral("fallback"));
    }
    evidence.insert(QStringLiteral("targetConnections"), targetConnections);
    evidence.insert(QStringLiteral("targetRequests"), targetRequests);
    evidence.insert(QStringLiteral("targetRequestLines"), targetRequestLines);
    if (passed) {
        runtime->setTun(true);
        const bool retried = waitFor([&] {
            const auto state = runtime->snapshot();
            return state.value("ready").toBool() && !state.value("busy").toBool() &&
                   state.value("coreExecutionMode") == QStringLiteral("Service") &&
                   state.value("tunState") == QStringLiteral("On");
        });
        evidence.insert(QStringLiteral("explicitRetry"), retried);
        passed &= retried;
    }
    if (!passed)
        evidence.insert(QStringLiteral("failureState"), runtime->snapshot());
    runtime->stop();
    runtime.reset();
    QJsonObject clean;
    const bool cleaned = waitFor([&] {
        clean = command(client, QStringLiteral("status"));
        const bool stopped = clean.value(QStringLiteral("clean")).toBool();
        if (!stopped)
            client.close();
        return stopped;
    });
    evidence.insert(QStringLiteral("clean"), clean);
    passed &= cleaned;
    evidence.insert(QStringLiteral("passed"), passed);
    return passed;
#endif
}
bool rateLimitProbe(QJsonObject& evidence) {
    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("QuickClash.Service.v1"));
    if (!waitFor([&] { return socket.state() != QLocalSocket::ConnectingState; }) ||
        socket.state() != QLocalSocket::ConnectedState)
        return false;
    QString session, epoch;
    const auto request = [&](int id, const QString& action,
                             const QJsonObject& payload = QJsonObject{}) {
        return QJsonObject{{"protocolVersion", 1}, {"requestId", QString::number(id)},
                           {"sessionId", session}, {"engineEpoch", epoch},
                           {"action", action},     {"payload", payload}};
    };
    const auto hello = exchange(socket, request(1, QStringLiteral("hello")));
    session = hello.value("result").toObject().value("sessionId").toString();
    const auto initial = exchange(socket, request(2, QStringLiteral("status")));
    if (session.isEmpty() || !initial.value("result").toObject().value("clean").toBool())
        return false;
    const auto started = exchange(socket, request(3, QStringLiteral("start"), startPayload()));
    const auto core = started.value("result").toObject();
    evidence.insert(QStringLiteral("started"), core);
    if (core.value("pid").toInt() <= 0)
        return false;
    epoch = core.value("epoch").toString();
    QByteArray burst;
    for (int id = 4; id < 68; ++id)
        burst +=
            QJsonDocument(request(id, QStringLiteral("status"))).toJson(QJsonDocument::Compact) +
            '\n';
    socket.write(burst);
    QByteArray replies;
    const bool finished = waitFor([&] {
        replies += socket.readAll();
        return replies.count('\n') == 64 || socket.state() == QLocalSocket::UnconnectedState;
    });
    int rejected = 0, accepted = 0;
    bool healthy = true;
    for (const auto& line : replies.split('\n')) {
        if (line.isEmpty())
            continue;
        const auto response = QJsonDocument::fromJson(line).object();
        healthy &= infra::ServiceProtocol::validResponse(response);
        if (response.value("error") == QStringLiteral("ServiceBusy")) {
            ++rejected;
        } else {
            const auto state = response.value("result").toObject();
            ++accepted;
            healthy &= state.value("ready").toBool() && state.value("pid") == core.value("pid") &&
                       state.value("epoch") == core.value("epoch");
        }
    }
    QElapsedTimer recovery;
    recovery.start();
    waitFor([&] { return recovery.elapsed() >= infra::kServiceRateWindowMs + 100; });
    const auto after = exchange(socket, request(68, QStringLiteral("status")));
    const auto state = after.value("result").toObject();
    const auto stopped = exchange(socket, request(69, QStringLiteral("stop")));
    const auto clean = exchange(socket, request(70, QStringLiteral("status")));
    const bool passed = finished && healthy && accepted > 0 && rejected > 0 &&
                        accepted + rejected == 64 && state.value("ready").toBool() &&
                        state.value("pid") == core.value("pid") &&
                        state.value("epoch") == core.value("epoch") &&
                        stopped.value("result").toObject().value("stopped").toBool() &&
                        clean.value("result").toObject().value("clean").toBool() &&
                        socket.state() == QLocalSocket::ConnectedState;
    evidence.insert(QStringLiteral("accepted"), accepted);
    evidence.insert(QStringLiteral("rateLimited"), rejected);
    evidence.insert(QStringLiteral("after"), state);
    evidence.insert(QStringLiteral("clean"), clean.value("result"));
    evidence.insert(QStringLiteral("passed"), passed);
    return passed;
}
} // namespace
int serviceProbe() {
    QString output, mode, source;
    for (const auto& argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--service-probe-output=")))
            output = argument.mid(23);
        if (argument.startsWith(QStringLiteral("--service-probe-mode=")))
            mode = argument.mid(21);
        if (argument.startsWith(QStringLiteral("--subscription-cache=")))
            source = argument.mid(21);
    }
    if (output.isEmpty())
        return 1;
    infra::ServiceClient client;
    QJsonObject evidence;
    if (mode == QStringLiteral("subscription_ordinary") ||
        mode == QStringLiteral("subscription_tun")) {
        const bool passed = realSubscriptionProbe(client, evidence, source,
                                                  mode == QStringLiteral("subscription_tun"));
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("delay_batch")) {
        const bool passed = delayBatchProbe(client, evidence);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("tun_delay")) {
        const bool passed = tunDelayProbe(client, evidence);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("tun_forward")) {
        const bool passed = tunForwardProbe(client, evidence);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("tun_busy")) {
        const bool passed = tunForwardProbe(client, evidence, true);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("tun_identity")) {
        const bool passed = tunIdentityProbe(client, evidence);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("tun_recovery")) {
        const bool passed = tunRecoveryProbe(client, evidence, output);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("tun_recovery_busy")) {
        const bool passed = tunRecoveryProbe(client, evidence, output, true);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("rate_limit")) {
        const bool passed = rateLimitProbe(evidence);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("connections")) {
        const bool passed = connectionProbe(evidence);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("timeout")) {
        const auto initial = command(client, QStringLiteral("status"));
        const auto started = command(client, QStringLiteral("start"), startPayload());
        evidence = {{QStringLiteral("initial"), initial},
                    {QStringLiteral("started"), started},
                    {QStringLiteral("waitingForPause"), true}};
        if (!saveEvidence(output, evidence) || started.value(QStringLiteral("pid")).toInt() <= 0)
            return 3;
        bool lost = false;
        QObject::connect(&client, &infra::ServiceClient::lost, &client, [&] { lost = true; });
        if (!waitFor([&] { return QFileInfo::exists(output + QStringLiteral(".paused")); }))
            return 3;
        const auto timedOut = command(client, QStringLiteral("status"));
        const auto recovered = command(client, QStringLiteral("status"));
        const auto stopped = command(client, QStringLiteral("stop"));
        const auto clean = command(client, QStringLiteral("status"));
        const bool passed =
            timedOut.value(QStringLiteral("error")) == QStringLiteral("ServiceTimeout") && !lost &&
            client.connected() && recovered.value(QStringLiteral("ready")).toBool() &&
            recovered.value(QStringLiteral("pid")) == started.value(QStringLiteral("pid")) &&
            stopped.value(QStringLiteral("stopped")).toBool() &&
            clean.value(QStringLiteral("clean")).toBool();
        evidence.insert(QStringLiteral("timedOut"), timedOut);
        evidence.insert(QStringLiteral("recovered"), recovered);
        evidence.insert(QStringLiteral("clean"), clean);
        evidence.insert(QStringLiteral("lost"), lost);
        evidence.insert(QStringLiteral("passed"), passed);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("status")) {
        evidence = command(client, QStringLiteral("status"));
        const bool clean = evidence.value(QStringLiteral("clean")).toBool();
        evidence.insert(QStringLiteral("passed"), clean);
        return saveEvidence(output, evidence) ? (clean ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("geo")) {
        QTemporaryDir resources;
        QJsonObject snapshot;
        // Minimal valid protobuf databases, deliberately different from frozen bundled data.
        const QHash<QString, QByteArray> assets{
            {QStringLiteral("geoip.dat"),
             QByteArray::fromHex("0a100a02434e12080a04c000020010181800")},
            {QStringLiteral("geosite.dat"),
             QByteArray::fromHex("0a160a02636e12100802120c6578616d706c652e74657374")}};
        for (auto it = assets.begin(); it != assets.end(); ++it) {
            const auto hash = QString::fromLatin1(
                QCryptographicHash::hash(it.value(), QCryptographicHash::Sha256).toHex());
            const auto ref = QStringLiteral("data/geo/%1/%2").arg(hash, it.key());
            const auto path = QDir(resources.path()).filePath(ref);
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                return 2;
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size())
                return 2;
            snapshot.insert(it.key(), QJsonObject{{"sha256", hash}, {"sourceRef", ref}});
        }
        auto payload = startPayload();
        const QByteArray yaml = "proxies: []\nproxy-groups: []\nrules: ['GEOIP,CN,DIRECT', "
                                "'GEOSITE,cn,DIRECT', 'MATCH,DIRECT']\n";
        payload.insert(QStringLiteral("yaml"), QString::fromLatin1(yaml.toBase64()));
        payload.insert(QStringLiteral("geo"),
                       QJsonObject{{"snapshot", snapshot}, {"storageRoot", resources.path()}});
        const auto started = command(client, QStringLiteral("start"), payload);
        const auto state = command(client, QStringLiteral("status"));
        const auto stopped = command(client, QStringLiteral("stop"));
        // Same approved reference but changed bytes must fail before the next core starts.
        const auto ref = snapshot.value(QStringLiteral("geoip.dat"))
                             .toObject()
                             .value(QStringLiteral("sourceRef"))
                             .toString();
        QFile corrupt(QDir(resources.path()).filePath(ref));
        if (!corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 2;
        corrupt.write("corrupt");
        corrupt.close();
        const auto rejected = command(client, QStringLiteral("start"), payload);
        command(client, QStringLiteral("stop"));
        const bool passed =
            started.value(QStringLiteral("pid")).toInt() > 0 &&
            state.value(QStringLiteral("ready")).toBool() &&
            state.value(QStringLiteral("geoResources"))
                    .toObject()
                    .value(QStringLiteral("resources"))
                    .toArray()
                    .size() == 2 &&
            stopped.value(QStringLiteral("stopped")).toBool() &&
            rejected.value(QStringLiteral("error")) == QStringLiteral("GeoResourceIntegrityFailed");
        evidence = {{"started", started},
                    {"status", state},
                    {"snapshot", snapshot},
                    {"corruptRejected", rejected},
                    {"passed", passed}};
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("hold")) {
        evidence.insert(QStringLiteral("initial"), command(client, QStringLiteral("status")));
        const auto started = command(client, QStringLiteral("start"), startPayload());
        evidence.insert(QStringLiteral("started"), started);
        evidence.insert(QStringLiteral("holding"), true);
        if (!saveEvidence(output, evidence))
            return 2;
        if (started.value(QStringLiteral("pid")).toInt() <= 0)
            return 3;
        bool lost = false;
        QJsonObject lastState;
        int polls = 0;
        QObject::connect(&client, &infra::ServiceClient::lost, &client, [&] { lost = true; });
        // Match EngineSession's active polling; an idle Windows pipe need not report
        // a dead server until the next I/O completes.
        QTimer poll;
        poll.setInterval(1000);
        QObject::connect(&poll, &QTimer::timeout, &client, [&] {
            client.command(QStringLiteral("status"), {}, [&](domain::Result<QJsonObject> state) {
                ++polls;
                if (const auto* value = std::get_if<QJsonObject>(&state)) {
                    lastState = *value;
                    lost |= !value->value(QStringLiteral("ready")).toBool();
                } else {
                    lastState = {{QStringLiteral("error"), std::get<domain::Error>(state).code}};
                    lost = true;
                }
            });
        });
        poll.start();
        const bool disconnected = waitFor([&] { return lost; });
        poll.stop();
        client.close();
        bool clean = false;
        for (int attempt = 0; disconnected && attempt < 50 && !clean; ++attempt) {
            const auto state = command(client, QStringLiteral("status"));
            clean = state.value(QStringLiteral("clean")).toBool();
            client.close();
            if (!clean)
                QThread::msleep(100);
        }
        evidence.insert(QStringLiteral("holding"), false);
        evidence.insert(QStringLiteral("lastStatus"), lastState);
        evidence.insert(QStringLiteral("polls"), polls);
        evidence.insert(QStringLiteral("lost"), disconnected);
        evidence.insert(QStringLiteral("restartClean"), clean);
        evidence.insert(QStringLiteral("passed"), disconnected && clean);
        return saveEvidence(output, evidence) ? (disconnected && clean ? 0 : 3) : 2;
    }
    bool passed = protocolProbe(evidence);
    const auto initial = command(client, QStringLiteral("status"));
    evidence.insert(QStringLiteral("initial"), initial);
    passed &= initial.value(QStringLiteral("clean")).toBool();
    if (initial.value(QStringLiteral("clean")).toBool()) {
        const auto unknown = command(client, QStringLiteral("execute"),
                                     {{QStringLiteral("path"), QStringLiteral("evil.exe")}});
        evidence.insert(QStringLiteral("unknownRejected"),
                        unknown.contains(QStringLiteral("error")));
        passed &= unknown.contains(QStringLiteral("error"));
        auto dangerous = startPayload();
        const QByteArray unsafeYaml = "proxies: []\nproxy-groups: []\nrules: ['MATCH,DIRECT']\n"
                                      "external-controller-pipe: arbitrary\n";
        dangerous.insert(QStringLiteral("yaml"), QString::fromLatin1(unsafeYaml.toBase64()));
        const auto rejected = command(client, QStringLiteral("start"), dangerous);
        evidence.insert(QStringLiteral("dangerousConfigRejected"), rejected);
        passed &= rejected.contains(QStringLiteral("error"));
        QTcpServer reservation;
        passed &= reservation.listen(QHostAddress::LocalHost);
        const auto port = reservation.serverPort();
        reservation.close();
        const QByteArray yaml = "proxies:\n  - name: fixture\n    type: http\n"
                                "    server: 192.0.2.1\n    port: 443\n"
                                "proxy-groups:\n  - name: choose\n    type: select\n"
                                "    proxies: [DIRECT, fixture]\nrules:\n  - MATCH,DIRECT\n";
        const auto started =
            command(client, QStringLiteral("start"),
                    {{QStringLiteral("yaml"), QString::fromLatin1(yaml.toBase64())},
                     {QStringLiteral("mixedPort"), port},
                     {QStringLiteral("mode"), QStringLiteral("rule")},
                     {QStringLiteral("dnsMode"), QStringLiteral("profile")},
                     {QStringLiteral("tun"), false}});
        evidence.insert(QStringLiteral("started"), started);
        passed &= started.value(QStringLiteral("pid")).toInt() > 0 &&
                  started.value(QStringLiteral("tun")) == false;
        if (started.value(QStringLiteral("pid")).toInt() > 0) {
            const auto groups = command(client, QStringLiteral("groups"));
            const auto selected = command(client, QStringLiteral("select"),
                                          {{QStringLiteral("group"), QStringLiteral("choose")},
                                           {QStringLiteral("member"), QStringLiteral("fixture")}});
            evidence.insert(QStringLiteral("groupsRead"),
                            groups.contains(QStringLiteral("proxies")));
            evidence.insert(QStringLiteral("selection"), selected);
            passed &= groups.contains(QStringLiteral("proxies")) &&
                      !selected.contains(QStringLiteral("error"));
            const bool denied = controllerDenied();
            QFile config(qEnvironmentVariable("ProgramData") +
                         QStringLiteral("/QuickClash/service/session/config.yaml"));
            const bool configDenied = !config.open(QIODevice::ReadOnly);
            evidence.insert(QStringLiteral("controllerDenied"), denied);
            evidence.insert(QStringLiteral("serviceConfigDenied"), configDenied);
            passed &= denied && configDenied;
            QElapsedTimer timer;
            timer.start();
            QJsonArray samples;
            for (int index = 0; index < 3; ++index) {
                if (index)
                    waitFor([&] { return timer.elapsed() >= index * 5000; });
                const auto resources = command(client, QStringLiteral("resources"));
                passed &= resources.value(QStringLiteral("complete")).toBool();
                samples.append(
                    QJsonObject{{QStringLiteral("elapsedMs"), timer.elapsed()},
                                {QStringLiteral("client"), platform::currentProcessMemory()},
                                {QStringLiteral("privileged"), resources}});
            }
            evidence.insert(QStringLiteral("memorySamples"), samples);
            // A second authenticated connection cannot claim an active session.
            infra::ServiceClient competitor;
            const auto competing = command(competitor, QStringLiteral("status"));
            passed &=
                competing.value(QStringLiteral("error")) == QStringLiteral("ServiceSessionBusy");
            evidence.insert(QStringLiteral("competitorRejected"), competing);
            competitor.close();
        }
        client.close(); // Simulates client death: the service must stop its owned core.
        bool clean = false;
        for (int attempt = 0; attempt < 20 && !clean; ++attempt) {
            infra::ServiceClient reopened;
            const auto state = command(reopened, QStringLiteral("status"));
            clean = state.value(QStringLiteral("clean")).toBool();
            reopened.close();
            if (!clean)
                QThread::msleep(100);
        }
        evidence.insert(QStringLiteral("disconnectClean"), clean);
        passed &= clean;
    }
    evidence.insert(QStringLiteral("passed"), passed);
    if (!saveEvidence(output, evidence))
        return 2;
    return passed ? 0 : 3;
}
} // namespace quickclash::test
