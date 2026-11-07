#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <QWinEventNotifier>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
constexpr qint64 kBufferLimit = 64 * 1024;
class Tunnel final : public QObject {
  public:
    Tunnel(QTcpSocket* client, const QByteArray& password, QObject* parent)
        : QObject(parent), client_(client), password_(password) {
        client_->setParent(this);
        client_->setReadBufferSize(kBufferLimit);
        client_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        remote_.setReadBufferSize(kBufferLimit);
        remote_.setProxy(QNetworkProxy::NoProxy);
        deadline_.setSingleShot(true);
        deadline_.start(10000);
        connect(&deadline_, &QTimer::timeout, this, &Tunnel::close);
        connect(client_, &QTcpSocket::readyRead, this, [this] {
            if (connected_)
                pump(*client_, remote_);
            else if (!connecting_)
                request();
        });
        connect(&remote_, &QTcpSocket::connected, this, [this] {
            deadline_.stop();
            connected_ = true;
            remote_.setSocketOption(QAbstractSocket::LowDelayOption, 1);
            if (client_->write("HTTP/1.1 200 Connection established\r\n\r\n") < 0) {
                close();
                return;
            }
            pump(*client_, remote_);
        });
        connect(&remote_, &QTcpSocket::readyRead, this, [this] { pump(remote_, *client_); });
        connect(&remote_, &QTcpSocket::bytesWritten, this, [this] { pump(*client_, remote_); });
        connect(client_, &QTcpSocket::bytesWritten, this, [this] {
            pump(remote_, *client_);
            finishRemote();
        });
        connect(client_, &QTcpSocket::disconnected, this, &Tunnel::close);
        connect(client_, &QTcpSocket::errorOccurred, this, &Tunnel::close);
        connect(&remote_, &QTcpSocket::disconnected, this, [this] {
            pump(remote_, *client_);
            finishRemote();
        });
        connect(&remote_, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError error) {
                    if (error != QAbstractSocket::RemoteHostClosedError)
                        close();
                });
    }

  private:
    void close() {
        if (closing_)
            return;
        closing_ = true;
        deadline_.stop();
        client_->abort();
        remote_.abort();
        deleteLater();
    }
    void pump(QTcpSocket& from, QTcpSocket& to) {
        if (closing_ || to.state() != QAbstractSocket::ConnectedState)
            return;
        const auto count = std::min(from.bytesAvailable(), kBufferLimit - to.bytesToWrite());
        if (count > 0) {
            const auto bytes = from.read(count);
            if (to.write(bytes) != bytes.size())
                close();
        }
    }
    void finishRemote() {
        if (connected_ && remote_.state() == QAbstractSocket::UnconnectedState &&
            !remote_.bytesAvailable() && !client_->bytesToWrite() &&
            client_->state() == QAbstractSocket::ConnectedState) {
            client_->disconnectFromHost();
        }
    }
    void request() {
        // Peek only CONNECT headers. Leave the TLS stream unread until the
        // upstream connection is ready, preserving bounded flow control.
        const auto header = client_->peek(16385);
        const auto end = header.indexOf("\r\n\r\n");
        if (header.size() > 16384 || (end < 0 && header.size() == 16384)) {
            close();
            return;
        }
        if (end < 0)
            return;
        const auto lines = header.left(end).split('\n');
        const auto fields = lines.first().trimmed().split(' ');
        const auto expected = "Basic " + (QByteArray("quickclash:") + password_).toBase64();
        bool authenticated = false;
        for (const auto& line : lines) {
            const auto split = line.indexOf(':');
            if (split > 0 && line.left(split).toLower() == "proxy-authorization")
                authenticated = line.mid(split + 1).trimmed() == expected;
        }
        if (!authenticated) {
            (void)client_->read(end + 4);
            (void)client_->write("HTTP/1.1 407 Proxy Authentication Required\r\n"
                                 "Proxy-Authenticate: Basic realm=\"QuickClash\"\r\n"
                                 "Content-Length: 0\r\nConnection: close\r\n\r\n");
            client_->disconnectFromHost();
            return;
        }
        if (fields.size() != 3 || fields[0] != "CONNECT" ||
            (fields[2] != "HTTP/1.1" && fields[2] != "HTTP/1.0")) {
            close();
            return;
        }
        const QUrl target(QStringLiteral("https://") + QString::fromLatin1(fields[1]));
        if (!target.isValid() || target.host().isEmpty() || !target.userInfo().isEmpty() ||
            !target.path().isEmpty() || target.hasQuery() || target.hasFragment() ||
            fields[1].size() > 512 || target.port() < 1 || target.port() > 65535) {
            close();
            return;
        }
        (void)client_->read(end + 4);
        connecting_ = true;
        remote_.connectToHost(target.host(), static_cast<quint16>(target.port()));
    }
    QTcpSocket* client_;
    QByteArray password_;
    QTcpSocket remote_;
    QTimer deadline_;
    bool connecting_ = false, connected_ = false, closing_ = false;
};
} // namespace
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    bool validParent = false;
    const auto arguments = application.arguments();
    const auto parent = arguments.value(1).mid(9).toULongLong(&validParent);
    if (arguments.size() != 2 || !arguments[1].startsWith(QStringLiteral("--parent=")) ||
        !validParent || !parent)
        return 2;
#ifdef Q_OS_WIN
    const auto owner = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parent));
    if (!owner || WaitForSingleObject(owner, 0) != WAIT_TIMEOUT) {
        if (owner)
            CloseHandle(owner);
        return 3;
    }
    QWinEventNotifier ownerExit(owner);
    QObject::connect(&ownerExit, &QWinEventNotifier::activated, &application,
                     &QCoreApplication::quit);
#else
    if (static_cast<quint64>(getppid()) != parent)
        return 3;
    QTimer ownerCheck;
    QObject::connect(&ownerCheck, &QTimer::timeout, &application, [&] {
        if (static_cast<quint64>(getppid()) != parent)
            application.quit();
    });
    ownerCheck.start(1000);
#endif
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly))
        return 4;
    const auto bytes = input.readLine(4097);
    const auto settings = QJsonDocument::fromJson(bytes).object();
    const auto password = settings.value(QStringLiteral("password")).toString().toLatin1();
    if (bytes.size() > 4096 || password.size() != 64)
        return 5;
    for (const auto character : password)
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
            return 5;
    // The server owns tunnels whose destruction updates this count.
    int tunnels = 0;
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return 6;
    QObject::connect(&server, &QTcpServer::newConnection, &application, [&] {
        while (server.hasPendingConnections()) {
            auto* client = server.nextPendingConnection();
            if (tunnels >= 24) {
                client->abort();
                client->deleteLater();
                continue;
            }
            auto* tunnel = new Tunnel(client, password, &server);
            ++tunnels;
            QObject::connect(tunnel, &QObject::destroyed, &application, [&] { --tunnels; });
        }
    });
    const auto response = QJsonDocument(QJsonObject{{QStringLiteral("ready"), true},
                                                    {QStringLiteral("port"), server.serverPort()}})
                              .toJson(QJsonDocument::Compact) +
                          '\n';
    if (std::fwrite(response.constData(), 1, static_cast<size_t>(response.size()), stdout) !=
            static_cast<size_t>(response.size()) ||
        std::fflush(stdout) != 0)
        return 7;
    const auto result = application.exec();
#ifdef Q_OS_WIN
    ownerExit.setEnabled(false);
    CloseHandle(owner);
#endif
    return result;
}
