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
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <array>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 16000) {
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
    QString output, mode;
    for (const auto& argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--service-probe-output=")))
            output = argument.mid(23);
        if (argument.startsWith(QStringLiteral("--service-probe-mode=")))
            mode = argument.mid(21);
    }
    if (output.isEmpty())
        return 1;
    infra::ServiceClient client;
    QJsonObject evidence;
    if (mode == QStringLiteral("delay_batch")) {
        const bool passed = delayBatchProbe(client, evidence);
        return saveEvidence(output, evidence) ? (passed ? 0 : 3) : 2;
    }
    if (mode == QStringLiteral("tun_delay")) {
        const bool passed = tunDelayProbe(client, evidence);
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
