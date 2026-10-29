#include "network_fixture.h"
#include <QFile>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

namespace quickclash::test {
DnsFixture::DnsFixture(QObject* parent) : QUdpSocket(parent) {
    connect(this, &QUdpSocket::readyRead, this, [this] {
        while (hasPendingDatagrams()) {
            QByteArray query(4096, Qt::Uninitialized);
            QHostAddress sender;
            quint16 port = 0;
            const auto size = readDatagram(query.data(), query.size(), &sender, &port);
            if (size < 17 || size > 4096)
                continue;
            query.resize(size);
            ++requests;
            QByteArray response = query.left(2) + QByteArray::fromHex("81800001000100000000");
            qsizetype end = 12;
            while (end < query.size() && static_cast<unsigned char>(query[end]) > 0) {
                const auto length = static_cast<unsigned char>(query[end]);
                if (length > 63)
                    break;
                end += 1 + length;
            }
            if (end + 5 > query.size())
                continue;
            response += query.mid(12, end + 5 - 12);
            response += QByteArray::fromHex("c00c000100010000003c0004cb007107");
            writeDatagram(response, sender, port);
        }
    });
}
bool DnsFixture::initialize() {
    for (const auto& address : QNetworkInterface::allAddresses()) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback() ||
            address == QHostAddress::AnyIPv4)
            continue;
        if (bind(address, 0)) {
            resolver = QStringLiteral("udp://%1:%2").arg(address.toString()).arg(localPort());
            return true;
        }
    }
    return false;
}
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
        QJsonObject received{{QStringLiteral("path"), QString::fromLatin1(path)}};
        for (const auto& line : buffer->split('\n')) {
            const auto separator = line.indexOf(':');
            if (separator > 0)
                received.insert(QString::fromLatin1(line.left(separator).trimmed().toLower()),
                                QString::fromLatin1(line.mid(separator + 1).trimmed()));
        }
        requestHeaders.append(received);
        lastCookie.clear();
        for (const auto& line : buffer->split('\n'))
            if (line.toLower().startsWith("cookie:"))
                lastCookie = line.mid(7).trimmed();
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
        auto body = isSubscription
                        ? (invalidSubscription ? QByteArrayLiteral("proxy-providers: {}")
                           : !lastUserAgent.startsWith(requiredUserAgent)
                               ? QByteArrayLiteral("c3M6Ly9wdWJsaWMtZml4dHVyZQ==")
                               : subscription)
                        : payloads.value(path, QByteArrayLiteral("quickclash-controlled-origin"));
        const auto redirect = redirects.value(path);
        const auto etag = entityTags.value(path);
        const auto modified = lastModified.value(path);
        int status = responseStatuses.value(path, redirect.isEmpty() ? 200 : 302);
        if (status == 200 &&
            ((!etag.isEmpty() && received.value("if-none-match") == QString::fromLatin1(etag)) ||
             (etag.isEmpty() && !modified.isEmpty() &&
              received.value("if-modified-since") == QString::fromLatin1(modified))))
            status = 304;
        if (status == 304)
            body.clear();
        const auto header =
            QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " Fixture\r\n" +
            (redirect.isEmpty() ? QByteArray{} : QByteArray("Location: ") + redirect + "\r\n") +
            (etag.isEmpty() ? QByteArray{} : QByteArray("ETag: ") + etag + "\r\n") +
            (modified.isEmpty() ? QByteArray{}
                                : QByteArray("Last-Modified: ") + modified + "\r\n") +
            (isSubscription && !subscriptionUsage.isEmpty()
                 ? QByteArray("subscription-userinfo: ") + subscriptionUsage + "\r\n"
                 : QByteArray{}) +
            "Content-Type: text/html; charset=UTF-8\r\nConnection: "
            "close\r\nContent-Length: " +
            QByteArray::number(body.size()) +
            "\r\nSet-Cookie: fixture_cookie=private; Secure; Path=/\r\n\r\n";
        const auto send = [socket, header, body, head = parts[0] == "HEAD"] {
            socket->write(header);
            if (!head)
                socket->write(body);
            socket->disconnectFromHost();
        };
        QTimer::singleShot(responseDelays.value(path), socket, send);
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
