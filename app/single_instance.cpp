#include "single_instance.h"
#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
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
        client.connectToServer(endpoint_);
        if (!client.waitForConnected(1000)) {
            return Result::Unavailable;
        }
        client.write(activate ? "activate\n" : "background\n");
        if ((client.bytesToWrite() > 0 && !client.waitForBytesWritten(1000)) ||
            (client.bytesAvailable() == 0 && !client.waitForReadyRead(1000))) {
            return Result::Unavailable;
        }
        return client.readAll() == QByteArrayLiteral("ok\n") ? Result::Activated
                                                             : Result::Unavailable;
    }
    // Only the process holding the election lock may remove a stale IPC endpoint.
    QLocalServer::removeServer(endpoint_);
    return server_.listen(endpoint_) ? Result::Primary : Result::Unavailable;
}
} // namespace quickclash
