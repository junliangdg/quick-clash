#pragma once
#include "domain/contracts.h"
#include <QElapsedTimer>
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
    ~ServiceClient() override;
    void command(const QString& action, const QJsonObject& payload, Handler handler);
    void cancelQueuedDelays();
    void close();
    [[nodiscard]] bool connected() const;
    [[nodiscard]] bool hadSession() const;
  signals:
    // The failure that ended the session, so callers can offer the right recovery instead of
    // collapsing every transport error into one generic message.
    void lost(const QString& reason);

  private:
    void fail(const char* code);
    void connectService(int attempt = 0);
    void sendPendingCommands();
    QLocalSocket socket_;
    QTimer deadline_;
    QTimer heartbeat_;
    QTimer writeTimer_;
    QElapsedTimer writeClock_;
    QElapsedTimer activity_;
    bool heartbeatPending_ = false;
    QByteArray input_;
    QList<QByteArray> pendingWrites_;
    QHash<QString, Handler> handlers_;
    quint64 nextId_ = 0;
    quint64 closeGeneration_ = 0;
    QString sessionId_;
    QString epoch_;
    QString handshakeId_;
    QString expectedServiceBuild_;
    bool verified_ = false;
    bool everVerified_ = false;
    bool failing_ = false;
};
} // namespace quickclash::infra
