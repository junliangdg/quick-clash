#include "subscription_probe.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSslSocket>
#include <QTcpServer>
#include <QThread>
#include <algorithm>
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace quickclash::test {
namespace {
bool wait(const std::function<bool()>& done, int milliseconds = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return done();
}
int freePort() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
} // namespace
int subscriptionProbe(application::DesktopRuntime& runtime, infra::JsonStore& store) {
    QJsonObject result;
    const auto finish = [&](const char* stage, bool success) {
        result.insert(QStringLiteral("stage"), QString::fromLatin1(stage));
        result.insert(QStringLiteral("success"), success);
        result.insert(QStringLiteral("errorPresent"),
                      !runtime.snapshot().value("error").toString().isEmpty());
        result.insert(QStringLiteral("issueCount"),
                      static_cast<int>(runtime.snapshot().value("configIssues").toArray().size()));
        runtime.stop();
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
        return success ? 0 : 1;
    };
    int mixed = freePort(), controller = freePort();
    while (mixed == controller)
        controller = freePort();
    runtime.setPorts(mixed, controller);
    std::string source;
    std::getline(std::cin, source);
    runtime.addSubscription(QString::fromStdString(source), QStringLiteral("Private probe"));
    if (!wait([&] { return !runtime.snapshot().value("busy").toBool(); }, 45000) ||
        runtime.snapshot().value("profiles").toArray().size() != 1)
        return finish("import", false);
    result.insert(QStringLiteral("imported"), true);
    runtime.selectProfile(
        runtime.snapshot().value("profiles").toArray()[0].toObject().value("id").toString());
    if (!wait([&] { return !runtime.snapshot().value("busy").toBool(); }))
        return finish("select", false);
    runtime.start(false);
    if (!wait([&] { return runtime.snapshot().value("ready").toBool(); }))
        return finish("start", false);
    result.insert(QStringLiteral("ready"), true);
    const auto manifest = store.read(QStringLiteral("data/runtime/current.json"));
    const auto selection =
        std::get<std::optional<QJsonObject>>(manifest)->value("selection").toObject();
    const auto semantic = store.readBytes(selection.value("artifactRef").toString());
    const auto root = YAML::Load(std::get<QByteArray>(semantic).toStdString());
    result.insert(QStringLiteral("nodes"), static_cast<int>(root["proxies"].size()));
    result.insert(QStringLiteral("groups"), static_cast<int>(root["proxy-groups"].size()));
    // Session secret stays inside this process; no raw API body is logged.
    QNetworkAccessManager api;
    api.setProxy(QNetworkProxy::NoProxy);
    const auto readApi = [&](const QByteArray& path) {
        const auto paths = store.path(QStringLiteral("data/sessions"));
        const QDir sessions(std::get<QString>(paths));
        auto session = sessions.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        session.removeIf([&](const QString& entry) {
            return !QFile::exists(sessions.filePath(entry + QStringLiteral("/config.yaml")));
        });
        if (session.size() != 1)
            return QJsonObject{};
        QFile config(sessions.filePath(session[0] + QStringLiteral("/config.yaml")));
        if (!config.open(QIODevice::ReadOnly))
            return QJsonObject{};
        const auto sessionYaml = YAML::Load(config.readAll().toStdString());
        const auto secret = QByteArray::fromStdString(sessionYaml["secret"].as<std::string>());
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(controller) +
                                     QString::fromLatin1(path)));
        request.setRawHeader("Authorization", "Bearer " + secret);
        request.setTransferTimeout(3000);
        auto* reply = api.get(request);
        wait([&] { return reply->isFinished(); }, 4000);
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        reply->abort();
        reply->deleteLater();
        return object;
    };
    // Ready can be published while startup is still restoring saved selections.
    if (!wait([&] {
            return runtime.snapshot().value("ready").toBool() &&
                   !runtime.snapshot().value("busy").toBool();
        }))
        return finish("startup_settled", false);
    runtime.setMode(QStringLiteral("global"));
    if (!wait([&] { return !runtime.snapshot().value("busy").toBool(); }) ||
        runtime.snapshot().value("preferences").toObject().value("mode") !=
            QStringLiteral("global") ||
        readApi("/configs").value("mode") != QStringLiteral("global"))
        return finish("mode", false);
    result.insert(QStringLiteral("global_mode_confirmed"), true);
    int attempts = 0;
    int tlsAttempts = 0, matchedSourceAttempts = 0, httpAttempts = 0;
    int mismatchedNodeAttempts = 0, otherImportedNodeAttempts = 0, emptyChainAttempts = 0;
    // Interleave protocols so an unavailable UDP route cannot consume all attempts.
    QList<YAML::Node> candidates;
    for (std::size_t index = 0; index < 3; ++index)
        for (const auto* type : {"hysteria2", "vless"}) {
            std::size_t found = 0;
            for (const auto& node : root["proxies"])
                if (node["type"].as<std::string>() == type && found++ == index)
                    candidates.append(node);
        }
    for (const auto& node : candidates) {
        ++attempts;
        const auto name = QString::fromStdString(node["name"].as<std::string>());
        runtime.selectProxy(QStringLiteral("GLOBAL"), name);
        if (!wait([&] { return !runtime.snapshot().value("busy").toBool(); }))
            continue;
        const auto selected = readApi("/proxies/GLOBAL").value("now").toString() == name;
        if (!selected)
            continue;
        QSslSocket socket;
        socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
        // Backpressure keeps this exact connection open while the controller is queried.
        socket.setReadBufferSize(65536);
        socket.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                      static_cast<quint16>(mixed)));
        socket.connectToHostEncrypted(QStringLiteral("speed.cloudflare.com"), 443);
        const bool encrypted = wait(
                                   [&] {
                                       return socket.isEncrypted() ||
                                              socket.state() == QAbstractSocket::UnconnectedState;
                                   },
                                   16000) &&
                               socket.isEncrypted();
        const auto sourcePort = socket.localPort();
        if (encrypted)
            socket.write("GET /__down?bytes=1048576 HTTP/1.1\r\n"
                         "Host: speed.cloudflare.com\r\nConnection: keep-alive\r\n\r\n");
        bool chain = false;
        bool sourcePortObserved = false;
        bool hostObserved = false;
        bool selectedNodeObserved = false;
        bool directObserved = false;
        bool globalObserved = false;
        bool anotherNodeObserved = false;
        int chainLength = 0;
        int connectionCount = 0;
        int controllerPolls = 0;
        QElapsedTimer timer;
        timer.start();
        // Bound the wait; this connection can finish before its chain is observed.
        while (encrypted && sourcePort > 0 && !chain && timer.elapsed() < 10000 &&
               socket.state() != QAbstractSocket::UnconnectedState) {
            const auto connections = readApi("/connections").value("connections").toArray();
            ++controllerPolls;
            connectionCount = std::max(connectionCount, static_cast<int>(connections.size()));
            for (const auto& entry : connections) {
                const auto connection = entry.toObject();
                const auto metadata = connection.value("metadata").toObject();
                const auto chains = connection.value("chains").toArray();
                if (metadata.value("sourcePort").toVariant().toInt() != sourcePort)
                    continue;
                sourcePortObserved = true;
                hostObserved = metadata.value("host") == QStringLiteral("speed.cloudflare.com");
                selectedNodeObserved = chains.contains(name);
                directObserved = chains.contains(QStringLiteral("DIRECT"));
                globalObserved = chains.contains(QStringLiteral("GLOBAL"));
                chainLength = static_cast<int>(chains.size());
                for (const auto& chainEntry : chains) {
                    for (const auto& candidate : root["proxies"]) {
                        const auto candidateName =
                            QString::fromStdString(candidate["name"].as<std::string>());
                        if (candidateName != name && chainEntry.toString() == candidateName)
                            anotherNodeObserved = true;
                    }
                }
                chain = hostObserved && selectedNodeObserved && !directObserved;
            }
            wait([&] { return socket.bytesAvailable() > 0; }, 100);
        }
        const bool responseAvailable =
            encrypted && wait([&] { return socket.peek(8192).contains("\r\n\r\n"); }, 16000);
        const auto response = socket.readAll();
        const bool httpOk =
            response.startsWith("HTTP/1.1 200 ") || response.startsWith("HTTP/1.0 200 ");
        // Validate a complete payload separately, while the global selection is unchanged.
        QNetworkAccessManager client;
        client.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                      static_cast<quint16>(mixed)));
        QNetworkRequest request(
            QUrl(QStringLiteral("https://speed.cloudflare.com/__down?bytes=1048576")));
        request.setTransferTimeout(15000);
        auto* reply =
            encrypted && responseAvailable && httpOk && chain ? client.get(request) : nullptr;
        const bool completed = reply && wait([&] { return reply->isFinished(); }, 16000);
        const auto status =
            reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
        const auto bytes = reply ? reply->readAll().size() : 0;
        const bool sameSelection = readApi("/proxies/GLOBAL").value("now").toString() == name;
        const bool success = completed && reply->error() == QNetworkReply::NoError &&
                             status == 200 && bytes == 1048576 && sameSelection;
        tlsAttempts += encrypted ? 1 : 0;
        matchedSourceAttempts += sourcePortObserved ? 1 : 0;
        httpAttempts += httpOk ? 1 : 0;
        mismatchedNodeAttempts += sourcePortObserved && !selectedNodeObserved ? 1 : 0;
        otherImportedNodeAttempts += anotherNodeObserved ? 1 : 0;
        emptyChainAttempts += sourcePortObserved && chainLength == 0 ? 1 : 0;
        if (reply) {
            reply->abort();
            reply->deleteLater();
        }
        socket.abort();
        result.insert(QStringLiteral("attempts"), attempts);
        result.insert(QStringLiteral("http_status"), status);
        result.insert(QStringLiteral("bytes"), static_cast<int>(bytes));
        result.insert(QStringLiteral("response_bytes"), static_cast<int>(response.size()));
        result.insert(QStringLiteral("tls_verified"), encrypted);
        result.insert(QStringLiteral("chain_http_ok"), httpOk);
        result.insert(QStringLiteral("selected_node_chain"), chain);
        result.insert(QStringLiteral("controller_polls"), controllerPolls);
        result.insert(QStringLiteral("controller_max_connections"), connectionCount);
        result.insert(QStringLiteral("source_port_observed"), sourcePortObserved);
        result.insert(QStringLiteral("target_host_observed"), hostObserved);
        result.insert(QStringLiteral("selected_node_observed"), selectedNodeObserved);
        result.insert(QStringLiteral("direct_chain_observed"), directObserved);
        result.insert(QStringLiteral("global_chain_observed"), globalObserved);
        result.insert(QStringLiteral("other_imported_node_observed"), anotherNodeObserved);
        result.insert(QStringLiteral("chain_length"), chainLength);
        result.insert(QStringLiteral("tls_attempts"), tlsAttempts);
        result.insert(QStringLiteral("source_match_attempts"), matchedSourceAttempts);
        result.insert(QStringLiteral("http_ok_attempts"), httpAttempts);
        result.insert(QStringLiteral("node_mismatch_attempts"), mismatchedNodeAttempts);
        result.insert(QStringLiteral("other_node_attempts"), otherImportedNodeAttempts);
        result.insert(QStringLiteral("empty_chain_attempts"), emptyChainAttempts);
        result.insert(QStringLiteral("selection_during_download"), sameSelection);
        if (success) {
            result.insert(QStringLiteral("protocol"),
                          QString::fromStdString(node["type"].as<std::string>()));
            result.insert(QStringLiteral("mode"), QStringLiteral("global"));
            const auto id = runtime.snapshot().value("selectedId").toString();
            const auto pid = runtime.snapshot().value("corePid");
            runtime.updateProfile(id);
            if (!wait([&] { return !runtime.snapshot().value("busy").toBool(); }, 45000) ||
                runtime.snapshot()
                        .value("profiles")
                        .toArray()[0]
                        .toObject()
                        .value("lastUpdateResult") != "Updated" ||
                !runtime.snapshot().value("ready").toBool() ||
                readApi("/proxies/GLOBAL").value("now").toString() != name)
                return finish("update", false);
            result.insert(QStringLiteral("updated"), true);
            result.insert(QStringLiteral("unchanged_kept_session"),
                          runtime.snapshot().value("corePid") == pid);
            // The website test deliberately limits response bodies to 64 KiB;
            // the site's normal landing page can exceed that limit.
            runtime.testWebsite(QStringLiteral("https://speed.cloudflare.com/__down?bytes=1024"));
            const bool websiteFinished = wait([&] {
                const auto state = runtime.snapshot()
                                       .value("features")
                                       .toObject()
                                       .value("websiteTest")
                                       .toObject()
                                       .value("state")
                                       .toString();
                return !state.isEmpty() && state != QStringLiteral("Running");
            });
            const auto website =
                runtime.snapshot().value("features").toObject().value("websiteTest").toObject();
            result.insert(QStringLiteral("website_state"), website.value("state"));
            result.insert(QStringLiteral("website_status"), website.value("status"));
            result.insert(QStringLiteral("website_bytes"), website.value("bytes"));
            if (!websiteFinished || website.value("state") != QStringLiteral("Reachable") ||
                website.value("status").toInt() != 200)
                return finish("forward_after_update", false);
            result.insert(QStringLiteral("forward_after_update"), true);
            const auto profileBeforeFailure =
                runtime.snapshot().value("profiles").toArray().first().toObject();
            const auto selectionBeforeFailure =
                store.read(QStringLiteral("data/runtime/current.json"));
            QTcpServer unavailable;
            if (!unavailable.listen(QHostAddress::LocalHost))
                return finish("failure_fixture", false);
            const auto unavailableUrl =
                QStringLiteral("https://127.0.0.1:%1/unavailable").arg(unavailable.serverPort());
            unavailable.close();
            runtime.changeProfileSource(id, unavailableUrl, {});
            const bool failed = wait([&] { return !runtime.snapshot().value("busy").toBool(); });
            const auto afterFailure = runtime.snapshot();
            const auto profileAfterFailure =
                afterFailure.value("profiles").toArray().first().toObject();
            const auto selectionAfterFailure =
                store.read(QStringLiteral("data/runtime/current.json"));
            const auto* beforeSelection =
                std::get_if<std::optional<QJsonObject>>(&selectionBeforeFailure);
            const auto* afterSelection =
                std::get_if<std::optional<QJsonObject>>(&selectionAfterFailure);
            const bool samePersistedSelection = beforeSelection && afterSelection &&
                                                *beforeSelection && *afterSelection &&
                                                **beforeSelection == **afterSelection;
            const bool preserved =
                failed &&
                afterFailure.value("error") == QStringLiteral("SubscriptionDownloadFailed") &&
                profileAfterFailure.value("sourceHash") ==
                    profileBeforeFailure.value("sourceHash") &&
                profileAfterFailure.value("sourceRef") == profileBeforeFailure.value("sourceRef") &&
                profileAfterFailure.value("secretRef") == profileBeforeFailure.value("secretRef") &&
                samePersistedSelection && afterFailure.value("selectedId") == id &&
                afterFailure.value("corePid") == pid && afterFailure.value("ready").toBool() &&
                readApi("/proxies/GLOBAL").value("now").toString() == name;
            result.insert(QStringLiteral("failed_source_preserved"), preserved);
            if (!preserved)
                return finish("failure_preservation", false);
            return finish("https_forward", true);
        }
    }
    return finish("https_forward", false);
}
} // namespace quickclash::test
