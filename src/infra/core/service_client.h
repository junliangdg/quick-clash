#pragma once
#include "domain/contracts.h"
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>
#include <functional>

namespace quickclash::infra {
// Versioned commands only. The privileged core endpoint and secret never leave the service.
class ServiceClient final : public QObject {
    Q_OBJECT
  public:
    using Handler = std::function<void(domain::Result<QJsonObject>)>;
    explicit ServiceClient(QObject* parent = nullptr);
    void command(const QString& action, const QJsonObject& payload, Handler handler);
    void close();
    [[nodiscard]] bool connected() const;
  signals:
    void lost();

  private:
    void fail(const char* code);
    QLocalSocket socket_;
    QTimer deadline_;
    QTimer heartbeat_;
    QByteArray input_;
    QList<QByteArray> pendingWrites_;
    QHash<QString, Handler> handlers_;
    quint64 nextId_ = 0;
    quint64 closeGeneration_ = 0;
    QString sessionId_;
    QString epoch_;
    QString handshakeId_;
    bool verified_ = false;
    bool failing_ = false;
};
} // namespace quickclash::infra
