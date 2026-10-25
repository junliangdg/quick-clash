#include "network_fixture.h"
#include <QFile>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

namespace quickclash::test {
TlsFixture::TlsFixture(QObject* parent) : QTcpServer(parent) {}
bool TlsFixture::initialize() {
    QFile cert(QStringLiteral("tests/fixtures/network/server_cert.pem"));
    QFile key(QStringLiteral("tests/fixtures/network/server_key.pem"));
    if (!cert.open(QIODevice::ReadOnly) || !key.open(QIODevice::ReadOnly))
        return false;
    certificate_ = QSslCertificate(cert.readAll());
    key_ = QSslKey(key.readAll(), QSsl::Rsa);
    if (certificate_.isNull() || key_.isNull())
        return false;
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.addCaCertificate(certificate_);
    QSslConfiguration::setDefaultConfiguration(
        configuration); // Test process only; OS trust untouched.
    return listen(QHostAddress::LocalHost);
}
void TlsFixture::incomingConnection(qintptr descriptor) {
    auto* socket = new QSslSocket(this);
    if (!socket->setSocketDescriptor(descriptor)) {
        socket->deleteLater();
        return;
    }
    socket->setLocalCertificate(certificate_);
    socket->setPrivateKey(key_);
    socket->setPeerVerifyMode(
        QSslSocket::VerifyNone); // Server does not require a client certificate.
    auto buffer = std::make_shared<QByteArray>();
    connect(socket, &QIODevice::readyRead, this, [this, socket, buffer] {
        buffer->append(socket->readAll());
        if (buffer->size() > 16384) {
            socket->abort();
            return;
        }
        if (!buffer->contains("\r\n\r\n"))
            return;
        const auto firstLine = buffer->left(buffer->indexOf("\r\n"));
        const auto parts = firstLine.split(' ');
        if (parts.size() < 2) {
            socket->abort();
            return;
        }
        const auto path = parts[1].split('?').first();
        requests.append(QString::fromLatin1(parts[0] + ' ' + path));
        const bool isSubscription = path == "/subscription";
        if (isSubscription) {
            lastUserAgent.clear();
            lastAccept.clear();
            for (const auto& line : buffer->split('\n')) {
                const auto separator = line.indexOf(':');
                const auto key = line.left(separator).trimmed().toLower();
                if (key == "user-agent")
                    lastUserAgent = line.mid(separator + 1).trimmed();
                if (key == "accept")
                    lastAccept = line.mid(separator + 1).trimmed();
            }
        }
        const auto body =
            isSubscription
                ? (invalidSubscription ? QByteArrayLiteral("proxy-providers: {}")
                   : !lastUserAgent.startsWith(requiredUserAgent)
                       ? QByteArrayLiteral("c3M6Ly9wdWJsaWMtZml4dHVyZQ==")
                       : subscription)
                : payloads.value(path, QByteArrayLiteral("quickclash-controlled-origin"));
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nConnection: "
                      "close\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\n\r\n");
        if (parts[0] != "HEAD")
            socket->write(body);
        socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    QTimer::singleShot(15000, socket, [socket] {
        socket->abort();
        socket->deleteLater();
    });
    socket->startServerEncryption();
}
ConnectFixture::ConnectFixture(quint16 originPort, QObject* parent)
    : QTcpServer(parent), originPort_(originPort) {
    connect(this, &QTcpServer::newConnection, this, [this] {
        while (hasPendingConnections()) {
            auto* client = nextPendingConnection();
            auto* upstream = new QTcpSocket(client);
            auto buffer = std::make_shared<QByteArray>();
            auto tunneled = std::make_shared<bool>(false);
            connect(client, &QIODevice::readyRead, client,
                    [this, client, upstream, buffer, tunneled] {
                        if (*tunneled) {
                            upstream->write(client->readAll());
                            return;
                        }
                        buffer->append(client->readAll());
                        if (buffer->size() > 16384) {
                            client->abort();
                            return;
                        }
                        if (!buffer->contains("\r\n\r\n"))
                            return;
                        const auto firstLine = buffer->left(buffer->indexOf("\r\n"));
                        if (!firstLine.startsWith("CONNECT fixture.quickclash.invalid:")) {
                            client->abort();
                            return;
                        }
                        targets.append(QString::fromLatin1(firstLine.split(' ').value(1)));
                        *tunneled = true;
                        upstream->connectToHost(QHostAddress::LocalHost, originPort_);
                    });
            connect(upstream, &QTcpSocket::connected, client,
                    [client] { client->write("HTTP/1.1 200 Connection established\r\n\r\n"); });
            connect(upstream, &QIODevice::readyRead, client,
                    [client, upstream] { client->write(upstream->readAll()); });
            connect(upstream, &QTcpSocket::disconnected, client,
                    [client] { client->disconnectFromHost(); });
            connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
            QTimer::singleShot(20000, client, [client] {
                client->abort();
                client->deleteLater();
            });
        }
    });
}
} // namespace quickclash::test
