#include "infra/core/delay_policy.h"
#include "infra/core/pipe_http.h"
#include "infra/core/service_traffic.h"
#include "infra/storage/json_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <iostream>
#include <utility>

namespace {
bool waitFor(const std::function<bool()>& condition, int timeout = 5000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) {
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
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto core = qEnvironmentVariable("QUICKCLASH_TEST_CORE");
    if (core.isEmpty() || !QFile::exists(core))
        return 1;
    QTemporaryDir directory;
    QTcpServer health;
    if (!directory.isValid() || !health.listen(QHostAddress::LocalHost))
        return 2;
    int healthRequests = 0;
    QObject::connect(&health, &QTcpServer::newConnection, &health, [&] {
        while (auto* socket = health.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
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
    const auto pipe =
        QStringLiteral("quickclash_test_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto pipePath = QDir::toNativeSeparators(QStringLiteral("//./pipe/") + pipe);
    const auto mixed = freePort();
    QFile config(directory.filePath(QStringLiteral("core.yaml")));
    const auto yaml = QByteArray("mixed-port: ") + QByteArray::number(mixed) +
                      "\nexternal-controller-pipe: '" + pipePath.toUtf8() +
                      "'\nsecret: fixture\nlog-level: debug\nmode: direct\nproxies:\n"
                      "  - {name: good, type: http, server: 127.0.0.1, port: " +
                      QByteArray::number(health.serverPort()) +
                      "}\n  - {name: bad, type: http, server: 127.0.0.1, port: 9}\n"
                      "proxy-groups:\n  - {name: choose, type: select, proxies: [good, bad]}\n"
                      "rules: ['MATCH,DIRECT']\n";
    if (mixed < 1024 || !config.open(QIODevice::WriteOnly) || config.write(yaml) != yaml.size())
        return 3;
    config.close();
    QProcess process;
    process.setProgram(core);
    process.setArguments(
        {QStringLiteral("-f"), config.fileName(), QStringLiteral("-d"), directory.path()});
    process.start();
    if (!process.waitForStarted(3000))
        return 4;
    QElapsedTimer startup;
    startup.start();
    while (startup.elapsed() < 500) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    QLocalSocket local;
    local.connectToServer(pipe);
    (void)waitFor([&] { return local.state() != QLocalSocket::ConnectingState; }, 2000);
    if (local.state() != QLocalSocket::ConnectedState)
        std::cerr << "Local pipe=" << pipe.toStdString()
                  << " error=" << local.errorString().toStdString() << '\n';
    local.abort();
    auto command = [&](const QByteArray& path) -> quickclash::domain::Result<QJsonObject> {
        quickclash::domain::Result<QJsonObject> result =
            quickclash::infra::storageError("ProbeTimeout");
        bool done = false;
        quickclash::infra::pipeHttp(&app, pipe, "GET", path, {}, [&](auto response) {
            result = std::move(response);
            done = true;
        });
        (void)waitFor([&] { return done; });
        return result;
    };
    bool available = false;
    QString pipeError;
    if (!waitFor(
            [&] {
                const auto result = command("/version");
                available = std::holds_alternative<QJsonObject>(result);
                if (const auto* error = std::get_if<quickclash::domain::Error>(&result))
                    pipeError = error->code;
                return available || process.state() == QProcess::NotRunning;
            },
            6000) ||
        !available) {
        std::cerr << "Core pipe unavailable; error=" << pipeError.toStdString()
                  << " state=" << process.state() << " exit=" << process.exitCode()
                  << " stderr=" << process.readAllStandardError().constData()
                  << " stdout=" << process.readAllStandardOutput().constData() << '\n';
        return 5;
    }

    // Exercise the actual protected-pipe transport and a persistent 1 Hz Mihomo stream.
    int streamsOpened = 0;
    QList<qint64> times;
    QElapsedTimer clock;
    clock.start();
    quickclash::infra::ServiceTrafficRelay relay([&](auto handler) {
        ++streamsOpened;
        return quickclash::infra::pipeJsonStream(&app, pipe, "/traffic", std::move(handler));
    });
    quickclash::infra::ServiceTraffic traffic(
        [&](quickclash::infra::ServiceTraffic::Handler handler) {
            relay.request(std::move(handler));
        },
        [&](const QJsonObject&) { times.append(clock.elapsed()); });
    traffic.setEnabled(true);
    if (!waitFor([&] { return times.size() >= 4; }, 6500) || streamsOpened != 1)
        return 6;
    for (qsizetype i = 1; i < times.size(); ++i)
        if (times[i] - times[i - 1] < 750 || times[i] - times[i - 1] > 1350)
            return 7;
    traffic.setEnabled(false);
    const auto before = times.size();
    relay.close();
    if (times.size() != before)
        return 8;

    const QJsonObject catalog{{"good", QJsonObject{}}, {"bad", QJsonObject{}}};
    const auto target = QStringLiteral("http://fixture.invalid/health");
    const auto good =
        quickclash::infra::serviceDelayPath({{"name", "good"}, {"url", target}}, catalog);
    const auto bad =
        quickclash::infra::serviceDelayPath({{"name", "bad"}, {"url", target}}, catalog);
    if (!good || !bad)
        return 9;
    const auto success = command(*good);
    const auto failure = command(*bad);
    const auto* value = std::get_if<QJsonObject>(&success);
    if (!value || value->value("delay").toInt() <= 0 || healthRequests != 1 ||
        !std::holds_alternative<quickclash::domain::Error>(failure)) {
        std::cerr << "HTTP target did not reach the expected good and bad nodes; requests="
                  << healthRequests << " success="
                  << (value
                          ? QJsonDocument(*value).toJson(QJsonDocument::Compact).constData()
                          : std::get<quickclash::domain::Error>(success).code.toUtf8().constData())
                  << " failure="
                  << (std::holds_alternative<QJsonObject>(failure)
                          ? QJsonDocument(std::get<QJsonObject>(failure))
                                .toJson(QJsonDocument::Compact)
                                .constData()
                          : std::get<quickclash::domain::Error>(failure).code.toUtf8().constData())
                  << " core=" << process.readAllStandardOutput().constData() << '\n';
        return 10;
    }
    process.terminate();
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(3000);
    }
    std::cout << "Real Mihomo pipe: 1 Hz traffic and HTTP delay success/failure passed\n";
    return 0;
}
