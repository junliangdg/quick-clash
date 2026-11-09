#include "core_compatibility_probe.h"
#include "application/desktop_runtime.h"
#include "listener_fixture.h"
#include "network_fixture.h"
#include "platform/core_artifacts.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cstdio>

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& done) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return done();
}
int port() {
    return quickclash::test::availableListenerPort();
}
} // namespace
int coreCompatibilityProbe() {
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (std::holds_alternative<domain::Error>(store.open()))
        return 1;
    application::DesktopRuntime runtime(store);
    runtime.initialize();
    auto mixed = port(), controller = port();
    while (mixed == controller)
        controller = port();
    runtime.setPorts(mixed, controller);
    TlsFixture origin, redirectOrigin;
    QFile fixture(QStringLiteral("tests/fixtures/mvp/core_compatible.yaml"));
    if (!origin.initialize() || !redirectOrigin.initialize() || !fixture.open(QIODevice::ReadOnly))
        return 2;
    origin.subscription = fixture.readAll();
    const auto source =
        QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort());
    const auto done = [&] {
        return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
    };
    const auto profile = [&] { return runtime.snapshot().value("profiles").toArray(); };
    const auto check = [&](bool valid, const char* message) {
        if (!valid)
            std::fprintf(stderr, "%s: %s\n", message,
                         runtime.snapshot().value("error").toString().toUtf8().constData());
        return valid;
    };
    runtime.addSubscription(source, QStringLiteral("Core compatibility"));
    if (!check(done() && profile().size() == 1 &&
                   runtime.snapshot().value("error").toString().isEmpty(),
               "core-supported protocols/options/groups/rules/DNS import"))
        return 3;
    const auto saved = profile().first().toObject();
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(),
               "core-supported configuration starts"))
        return 4;
    runtime.setDemands({QStringLiteral("proxies")});
    runtime.refresh();
    if (!check(waitFor([&] {
                   for (const auto& value : runtime.snapshot().value("groups").toArray())
                       if (value.toObject().value("name") == "Main")
                           return value.toObject().value("type") == "Selector";
                   return false;
               }),
               "core group remains visible after live readback"))
        return 5;
    const auto projected = [&] {
        QJsonObject nodes;
        for (const auto& value :
             runtime.snapshot().value("features").toObject().value("nodes").toArray()) {
            const auto node = value.toObject();
            nodes.insert(node.value("name").toString(), node);
        }
        return nodes;
    };
    if (!check(waitFor([&] {
                   const auto nodes = projected();
                   for (const auto* name :
                        {"anytls_fixture", "vless_fixture", "ssh_fixture", "ssr_fixture",
                         "snell_fixture", "hysteria_fixture", "wireguard_fixture", "direct_fixture",
                         "reject_fixture", "dns_fixture", "inline_anytls"})
                       if (nodes.value(name).toObject().value("type").toString().isEmpty())
                           return false;
                   return true;
               }),
               "all official core node types remain visible"))
        return 34;
    runtime.featureCommand("delay", {"anytls_fixture", "Main"});
    const auto delayProgress = [&] {
        return runtime.snapshot().value("features").toObject().value("delayTest").toObject();
    };
    if (!check(delayProgress().value("total") == 1 &&
                   waitFor([&] { return delayProgress().value("state") == "Completed"; }) &&
                   delayProgress().value("completed") == 1,
               "AnyTLS single-node delay reaches the official core"))
        return 35;
    runtime.featureCommand("delay_batch", {"Main"});
    if (!check(delayProgress().value("scope") == "group" && delayProgress().value("total") == 11 &&
                   waitFor([&] { return delayProgress().value("state") == "Completed"; }) &&
                   delayProgress().value("completed") == 11,
               "group delay includes every core-supported protocol and inline node"))
        return 36;
    const auto pid = runtime.snapshot().value("corePid");
    const auto id = saved.value("id").toString();
    const auto validSource = origin.subscription;
    for (const auto& replacement :
         {qMakePair(QByteArray("type: anytls"), QByteArray("type: no_such_protocol")),
          qMakePair(QByteArray("MATCH,Main"), QByteArray("NO-SUCH-RULE,value,Main")),
          qMakePair(QByteArray("dialer-proxy: vless_fixture"),
                    QByteArray("dialer-proxy: missing_fixture"))}) {
        origin.subscription = validSource;
        origin.subscription.replace(replacement.first, replacement.second);
        runtime.updateProfile(id);
        if (!check(done() && !runtime.snapshot().value("error").toString().isEmpty() &&
                       profile().first().toObject().value("sourceHash") ==
                           saved.value("sourceHash") &&
                       runtime.snapshot().value("corePid") == pid &&
                       runtime.snapshot().value("ready").toBool(),
                   "official core rejection preserves source and running session"))
            return 6;
    }
    runtime.stop();
    if (!check(done() && !runtime.snapshot().value("ready").toBool(),
               "core compatibility session stops"))
        return 7;
    origin.payloads.insert("/nodes", "proxies: [{name: keep, type: anytls, server: 127.0.0.1, "
                                     "port: 19887, password: public_dummy}, "
                                     "{name: drop, type: http, server: 127.0.0.1, port: 9}]\n");
    redirectOrigin.payloads.insert("/nodes", origin.payloads.value("/nodes"));
    origin.redirects.insert("/nodes", "/nodes-final");
    origin.redirects.insert("/nodes-final",
                            "https://127.0.0.1:" + QByteArray::number(redirectOrigin.serverPort()) +
                                "/nodes");
    origin.subscription =
        "proxy-providers:\n  native:\n    type: http\n    url: https://127.0.0.1:" +
        QByteArray::number(origin.serverPort()) +
        "/nodes\n    header: {X-Public-Header: [public_dummy_header]}\n"
        "    filter: '^keep$'\nproxy-groups:\n"
        "- {name: Main, type: select, use: [native]}\nrules: ['MATCH,Main']\n";
    runtime.changeProfileSource(id, source, {});
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty() &&
                   profile().first().toObject().value("providerMode") == "runtime",
               "provider options choose native core semantics"))
        return 8;
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(),
               "native provider session starts"))
        return 9;
    runtime.refresh();
    if (!check(waitFor([&] {
                   for (const auto& value : runtime.snapshot().value("groups").toArray())
                       if (value.toObject().value("name") == "Main") {
                           const auto members = value.toObject().value("members").toArray();
                           return members.contains("keep") && !members.contains("drop");
                       }
                   return false;
               }),
               "core provider filter preserves actual membership"))
        return 10;
    runtime.stop();
    if (!check(done(), "native provider session stops"))
        return 11;
    bool headerReceived = false;
    for (const auto& request : origin.requestHeaders)
        if (request.toObject().value("path") == "/nodes" &&
            request.toObject().value("x-public-header") == "public_dummy_header")
            headerReceived = true;
    if (!check(headerReceived, "provider import forwards custom request headers"))
        return 12;
    for (const auto& request : redirectOrigin.requestHeaders)
        if (!check(!request.toObject().contains("x-public-header"),
                   "cross-origin provider redirects omit custom headers"))
            return 27;
    const auto acceptedProvider = profile().first().toObject().value("sourceHash");
    const auto validNodes = redirectOrigin.payloads.value("/nodes");
    auto badNodes = validNodes;
    badNodes.replace("type: anytls", "type: no_such_protocol");
    redirectOrigin.payloads.insert("/nodes", badNodes);
    runtime.updateProfile(id);
    if (!check(done() && !runtime.snapshot().value("error").toString().isEmpty() &&
                   profile().first().toObject().value("sourceHash") == acceptedProvider,
               "native YAML provider failure preserves confirmed source"))
        return 32;
    redirectOrigin.payloads.insert("/nodes", validNodes);
    const QByteArray links = "# Public fixture comment preceding native nodes\n"
                             "ss://YWVzLTEyOC1nY206cHVibGljX2R1bW15@127.0.0.1:19887#link_fixture\n";
    for (const auto& body : {links, links.toBase64()}) {
        origin.subscription = body;
        runtime.changeProfileSource(id, source, {});
        if (!check(done() && runtime.snapshot().value("error").toString().isEmpty(),
                   body == links ? "official core imports URI subscription"
                                 : "official core imports Base64 subscription"))
            return 13;
        runtime.start(false);
        if (!check(done() && runtime.snapshot().value("ready").toBool(),
                   "URI/Base64 subscription starts"))
            return 14;
        runtime.refresh();
        if (!check(waitFor([&] {
                       for (const auto& group : runtime.snapshot().value("groups").toArray())
                           if (group.toObject().value("members").toArray().contains("link_fixture"))
                               return true;
                       return false;
                   }),
                   "native subscription nodes appear in live group"))
            return 15;
        runtime.featureCommand("backup", {"confirmed", "source"});
        const auto sourceBackup =
            runtime.snapshot().value("features").toObject().value("backupUrl").toString();
        runtime.featureCommand("preview_backup", {sourceBackup});
        if (!check(!runtime.snapshot()
                        .value("features")
                        .toObject()
                        .value("backupPreview")
                        .toObject()
                        .value("token")
                        .toString()
                        .isEmpty(),
                   "URI/Base64 source backup keeps an offline restorable snapshot"))
            return 33;
        runtime.featureCommand("cancel_backup_restore", {});
        const auto accepted = profile().first().toObject().value("sourceHash");
        origin.subscription = "ss://invalid\n";
        runtime.updateProfile(id);
        if (!check(done() && !runtime.snapshot().value("error").toString().isEmpty() &&
                       profile().first().toObject().value("sourceHash") == accepted &&
                       runtime.snapshot().value("ready").toBool(),
                   "invalid native body preserves confirmed configuration"))
            return 16;
        runtime.stop();
        if (!done())
            return 17;
    }
    const auto rulesText = QDir(directory.path()).filePath("public_rules.txt");
    const auto rulesMrs = QDir(directory.path()).filePath("public_rules.mrs");
    QFile rulesFile(rulesText);
    if (!rulesFile.open(QIODevice::WriteOnly) || rulesFile.write("+.public.invalid\n") < 0)
        return 18;
    rulesFile.close();
    QProcess converter;
    converter.start(platform::bundledCorePath(QCoreApplication::applicationDirPath()),
                    {"convert-ruleset", "domain", "text", rulesText, rulesMrs});
    if (!converter.waitForFinished(10000) || converter.exitCode() != 0)
        return 19;
    QFile mrs(rulesMrs);
    if (!mrs.open(QIODevice::ReadOnly))
        return 20;
    origin.payloads.insert("/mrs", mrs.readAll());
    origin.subscription = "proxy-providers:\n  native: {type: inline, payload: [{name: "
                          "direct_fixture, type: direct}]}\n"
                          "rule-providers:\n  binary:\n    type: http\n    behavior: domain\n    "
                          "format: mrs\n    url: https://127.0.0.1:" +
                          QByteArray::number(origin.serverPort()) +
                          "/mrs\nproxy-groups: [{name: Main, type: select, use: [native]}]\n"
                          "rules: ['RULE-SET,binary,Main', 'MATCH,Main']\n";
    runtime.changeProfileSource(id, source, {});
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty(),
               "MRS imports through the official binary parser"))
        return 21;
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(), "MRS session starts"))
        return 22;
    const auto acceptedMrs = profile().first().toObject().value("sourceHash");
    origin.payloads.insert("/mrs", "invalid_mrs");
    runtime.updateProfile(id);
    if (!check(done() && !runtime.snapshot().value("error").toString().isEmpty() &&
                   profile().first().toObject().value("sourceHash") == acceptedMrs &&
                   runtime.snapshot().value("ready").toBool(),
               "corrupt MRS preserves source and running session"))
        return 23;
    runtime.stop();
    if (!done())
        return 24;
    const auto localConfig = QDir(directory.path()).filePath("public_config.yaml");
    QFile local(localConfig);
    if (!local.open(QIODevice::WriteOnly))
        return 25;
    auto fileSource = origin.subscription;
    const auto start = fileSource.indexOf("    type: http\n");
    const auto end = fileSource.indexOf("proxy-groups:", start);
    fileSource.replace(start, end - start,
                       "    type: file\n    behavior: domain\n    format: mrs\n"
                       "    path: public_rules.mrs\n");
    local.write(fileSource);
    local.close();
    runtime.importFile(QUrl::fromLocalFile(localConfig).toString(), "Local provider");
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty() &&
                   profile().size() == 2,
               "relative file provider is copied into managed storage"))
        return 26;
    origin.requests = {};
    runtime.featureCommand("backup", {"confirmed", "offline"});
    const auto backupUrl =
        runtime.snapshot().value("features").toObject().value("backupUrl").toString();
    runtime.featureCommand("preview_backup", {backupUrl});
    const auto token = runtime.snapshot()
                           .value("features")
                           .toObject()
                           .value("backupPreview")
                           .toObject()
                           .value("token")
                           .toString();
    if (!check(!token.isEmpty(), "native MRS offline backup preview"))
        return 28;
    runtime.featureCommand("restore_backup", {token, "confirmed"});
    if (!check(done() && origin.requests.isEmpty() &&
                   runtime.snapshot().value("error").toString().isEmpty(),
               "native MRS offline restore revalidates without network"))
        return 29;
    QTcpServer plain;
    QObject::connect(&plain, &QTcpServer::newConnection, &plain, [&] {
        auto* socket = plain.nextPendingConnection();
        QObject::connect(socket, &QIODevice::readyRead, socket, [socket, validSource] {
            const auto request = socket->readAll();
            if (!request.contains("\r\n\r\n"))
                return;
            socket->write("HTTP/1.0 200 OK\r\nContent-Length: " +
                          QByteArray::number(validSource.size()) + "\r\n\r\n" + validSource);
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    if (!plain.listen(QHostAddress::LocalHost))
        return 30;
    runtime.addSubscription(
        QStringLiteral("http://127.0.0.1:%1/subscription").arg(plain.serverPort()),
        "Explicit HTTP source");
    if (!check(done() && runtime.snapshot().value("error").toString().isEmpty(),
               "explicit HTTP subscription follows the same core validation"))
        return 31;
    std::puts("Core compatibility import/start/rejection preservation passed");
    return 0;
}
} // namespace quickclash::test
