#include "service_probe.h"
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
#include <QTemporaryDir>
#include <QThread>
#include <array>
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
