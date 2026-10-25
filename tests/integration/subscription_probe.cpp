#include "subscription_probe.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpServer>
#include <QThread>
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
        result.insert(QStringLiteral("error"), runtime.snapshot().value("error"));
        result.insert(QStringLiteral("issues"), runtime.snapshot().value("configIssues"));
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
    runtime.setMode(QStringLiteral("global"));
    if (!wait([&] { return !runtime.snapshot().value("busy").toBool(); }))
        return finish("mode", false);
    QNetworkAccessManager client;
    client.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                  static_cast<quint16>(mixed)));
    int attempts = 0;
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
        QNetworkRequest request(
            QUrl(QStringLiteral("https://speed.cloudflare.com/__down?bytes=1048576")));
        request.setTransferTimeout(15000);
        auto* reply = client.get(request);
        bool chain = false;
        QElapsedTimer timer;
        timer.start();
        while (!reply->isFinished() && timer.elapsed() < 16000) {
            const auto connections = readApi("/connections").value("connections").toArray();
            for (const auto& entry : connections) {
                const auto connection = entry.toObject();
                const auto chains = connection.value("chains").toArray();
                if (connection.value("metadata").toObject().value("host") ==
                        "speed.cloudflare.com" &&
                    chains.contains(name) && !chains.contains(QStringLiteral("DIRECT")))
                    chain = true;
            }
            wait([&] { return reply->isFinished(); }, 10);
        }
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes = reply->readAll().size();
        const bool success = reply->isFinished() && reply->error() == QNetworkReply::NoError &&
                             status == 200 && bytes == 1048576 && chain;
        reply->abort();
        reply->deleteLater();
        result.insert(QStringLiteral("attempts"), attempts);
        result.insert(QStringLiteral("http_status"), status);
        result.insert(QStringLiteral("bytes"), static_cast<int>(bytes));
        result.insert(QStringLiteral("selected_node_chain"), chain);
        if (success) {
            result.insert(QStringLiteral("protocol"),
                          QString::fromStdString(node["type"].as<std::string>()));
            result.insert(QStringLiteral("tls_verified"), true);
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
            runtime.testWebsite(QStringLiteral("https://speed.cloudflare.com/"));
            if (!wait(
                    [&] { return !runtime.snapshot().value("testResult").toString().isEmpty(); }) ||
                !runtime.snapshot().value("testResult").toString().startsWith("HTTP 200"))
                return finish("forward_after_update", false);
            result.insert(QStringLiteral("forward_after_update"), true);
            return finish("https_forward", true);
        }
    }
    return finish("https_forward", false);
}
} // namespace quickclash::test
