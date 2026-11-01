#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>
#include <QUdpSocket>

namespace quickclash::test {
class DnsFixture final : public QUdpSocket {
  public:
    explicit DnsFixture(QObject* parent = nullptr);
    bool initialize();
    QString resolver;
    int requests = 0;
};
// Loopback-only test endpoints. The key in tests/fixtures/network is public test data.
class TlsFixture final : public QTcpServer {
  public:
    explicit TlsFixture(QObject* parent = nullptr);
    bool initialize();
    QByteArray subscription;
    QByteArray subscriptionUsage;
    QHash<QByteArray, QByteArray> payloads;
    QHash<QByteArray, QByteArray> redirects;
    QHash<QByteArray, int> responseDelays;
    QHash<QByteArray, int> responseStatuses;
    QHash<QByteArray, QByteArray> entityTags, lastModified;
    QHash<QByteArray, QByteArray> extraHeaders;
    QJsonArray requestHeaders;
    QByteArray lastCookie;
    bool invalidSubscription = false;
    QByteArray requiredUserAgent = "clash.meta/";
    QByteArray lastUserAgent;
    QByteArray lastAccept;
    QJsonArray requests;

  protected:
    void incomingConnection(qintptr descriptor) override;

  private:
    QSslCertificate certificate_;
    QSslKey key_;
};
class ConnectFixture final : public QTcpServer {
  public:
    explicit ConnectFixture(quint16 originPort, QObject* parent = nullptr);
    QJsonArray targets;

  private:
    quint16 originPort_;
};
} // namespace quickclash::test
