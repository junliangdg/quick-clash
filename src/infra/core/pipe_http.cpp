#include "infra/core/pipe_http.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/json_store.h"
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include <utility>

namespace quickclash::infra {
QObject* pipeJsonStream(QObject* context, const QString& pipe, const QByteArray& path,
                        std::function<void(const QJsonObject&)> handler) {
    auto* socket = new QLocalSocket(context);
    socket->setReadBufferSize(kReadChunkSize);
    auto pending = std::make_shared<QByteArray>();
    auto headers = std::make_shared<bool>(false);
    QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    QObject::connect(socket, &QLocalSocket::errorOccurred, socket,
                     [socket](QLocalSocket::LocalSocketError) { socket->deleteLater(); });
    QObject::connect(socket, &QLocalSocket::connected, socket, [socket, path] {
        socket->write("GET " + path + " HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    });
    QObject::connect(socket, &QIODevice::readyRead, socket, [socket, pending, headers, handler] {
        if (!appendAvailable(*socket, *pending, 1024 * 1024)) {
            socket->abort();
            return;
        }
        if (!*headers) {
            const auto end = pending->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const auto status = pending->left(pending->indexOf("\r\n")).split(' ').value(1).toInt();
            if (status != 200) {
                socket->abort();
                return;
            }
            pending->remove(0, end + 4);
            *headers = true;
        }
        qsizetype newline;
        int count = 0;
        while ((newline = pending->indexOf('\n')) >= 0 && count++ < 200) {
            if (newline <= 16384) {
                const auto document = QJsonDocument::fromJson(pending->first(newline));
                if (document.isObject())
                    handler(document.object());
            }
            pending->remove(0, newline + 1);
        }
        // Bound one event-loop turn and explicitly drop excess telemetry.
        if (count > 200) {
            pending->clear();
            handler({{QStringLiteral("type"), QStringLiteral("warning")},
                     {QStringLiteral("payload"),
                      QStringLiteral("Service log stream dropped excess entries")}});
        }
    });
    socket->connectToServer(pipe);
    return socket;
}
void pipeHttp(QObject* context, const QString& pipe, const QByteArray& method,
              const QByteArray& path, const QJsonObject& body,
              std::function<void(domain::Result<QJsonObject>)> handler) {
    auto* socket = new QLocalSocket(context);
    socket->setReadBufferSize(kReadChunkSize);
    auto* deadline = new QTimer(socket);
    deadline->setSingleShot(true);
    deadline->start(
        method == "PUT" && (path.startsWith("/providers/") || path.startsWith("/configs")) ? 12500
                                                                                           : 3500);
    auto bytes = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);
    auto finish = [socket, done, handler = std::move(handler)](domain::Result<QJsonObject> value) {
        if (std::exchange(*done, true))
            return;
        socket->abort();
        socket->deleteLater();
        handler(std::move(value));
    };
    const auto payload =
        body.isEmpty() ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact);
    QObject::connect(socket, &QLocalSocket::connected, socket, [socket, method, path, payload] {
        socket->write(method + ' ' + path +
                      " HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\nContent-Type: "
                      "application/json\r\nContent-Length: " +
                      QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
    });
    auto parse = [socket, bytes, done, finish](bool ended) {
        if (*done)
            return;
        if (!appendAvailable(*socket, *bytes, 2 * 1024 * 1024)) {
            finish(storageError("CoreResponseTooLarge"));
            return;
        }
        const auto headerEnd = bytes->indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (ended)
                finish(storageError("CoreResponseInvalid"));
            return;
        }
        const auto status = bytes->left(bytes->indexOf("\r\n")).split(' ').value(1).toInt();
        if (status < 200 || status >= 300) {
            finish(storageError(status == 404 ? "CoreTargetNotFound" : "CoreRequestFailed"));
            return;
        }
        auto payload = bytes->mid(headerEnd + 4);
        if (status == 204) {
            finish(QJsonObject{});
            return;
        }
        // HTTP/1.0 avoids chunked encoding. Streams yield one complete JSON line.
        const auto newline = payload.indexOf('\n');
        const auto doc = QJsonDocument::fromJson(newline >= 0 ? payload.first(newline) : payload);
        if (doc.isObject()) {
            payload = {};
            *bytes = {};
            finish(doc.object());
        } else if (ended)
            finish(storageError("CoreResponseInvalid"));
    };
    QObject::connect(socket, &QIODevice::readyRead, socket, [parse] { parse(false); });
    QObject::connect(socket, &QLocalSocket::disconnected, socket, [parse] { parse(true); });
    QObject::connect(socket, &QLocalSocket::errorOccurred, socket,
                     [parse](QLocalSocket::LocalSocketError) { parse(true); });
    QObject::connect(deadline, &QTimer::timeout, socket,
                     [finish] { finish(storageError("CoreRequestTimeout")); });
    socket->connectToServer(pipe);
}
} // namespace quickclash::infra
