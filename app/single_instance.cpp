#include "single_instance.h"
#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QTimer>
#include <QVariant>

namespace quickclash {
SingleInstance::SingleInstance(QString key, QObject* parent) : QObject(parent) {
    const auto digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    endpoint_ = QStringLiteral("quickclash_") + QString::fromLatin1(digest);
    lock_ = std::make_unique<QLockFile>(QDir::temp().filePath(endpoint_ + QStringLiteral(".lock")));
    lock_->setStaleLockTime(0);
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = server_.nextPendingConnection()) {
            socket->setParent(&server_);
            socket->setReadBufferSize(64);
            QTimer::singleShot(1000, socket, &QLocalSocket::abort);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            const auto receive = [this, socket] {
                if (socket->property("handled").toBool()) {
                    return;
                }
                auto message = socket->property("message").toByteArray();
                message += socket->readAll();
                if (message == QByteArrayLiteral("activate\n") ||
                    message == QByteArrayLiteral("background\n")) {
                    socket->setProperty("handled", true);
                    if (message == QByteArrayLiteral("activate\n"))
                        emit activationRequested();
                    socket->write("ok\n");
                    socket->disconnectFromServer();
                } else if (message.size() > 32 || message.contains('\n')) {
                    socket->abort();
                } else {
                    socket->setProperty("message", message);
                }
            };
            connect(socket, &QLocalSocket::readyRead, this, receive);
            if (socket->bytesAvailable() > 0) {
                receive();
            }
        }
    });
}
SingleInstance::Result SingleInstance::start(bool activate) {
    if (!lock_->tryLock(0)) {
        QLocalSocket client;
        client.setReadBufferSize(64);
        client.connectToServer(endpoint_);
        if (!client.waitForConnected(1000)) {
            return Result::Unavailable;
        }
        const QByteArray message =
            activate ? QByteArrayLiteral("activate\n") : QByteArrayLiteral("background\n");
        if (client.write(message) != message.size()) {
            return Result::Unavailable;
        }
        if (client.bytesToWrite() > 0) {
            // A fast peer can reply and close before the write wait reports success.
            // The complete acknowledgement, not this wait's return value, decides success.
            client.waitForBytesWritten(1000);
        }
        const QByteArray expected = QByteArrayLiteral("ok\n");
        QByteArray response;
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            response += client.readAll();
            if (response == expected) {
                return Result::Activated;
            }
            if (!expected.startsWith(response)) {
                return Result::Unavailable;
            }
            const auto remaining = 1000 - timer.elapsed();
            if (remaining <= 0 || (!client.waitForReadyRead(static_cast<int>(remaining)) &&
                                   client.bytesAvailable() == 0)) {
                return Result::Unavailable;
            }
        }
    }
    // Only the process holding the election lock may remove a stale IPC endpoint.
    QLocalServer::removeServer(endpoint_);
    return server_.listen(endpoint_) ? Result::Primary : Result::Unavailable;
}
} // namespace quickclash
