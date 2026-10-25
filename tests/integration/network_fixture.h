#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>

namespace quickclash::test {
// Loopback-only test endpoints. The key in tests/fixtures/network is public test data.
class TlsFixture final : public QTcpServer {
  public:
    explicit TlsFixture(QObject* parent = nullptr);
    bool initialize();
    QByteArray subscription;
    QHash<QByteArray, QByteArray> payloads;
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
