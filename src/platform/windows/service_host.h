#pragma once
#include "domain/log_buffer.h"
#include "infra/core/pipe_http.h"
#include "infra/core/service_protocol.h"
#include "infra/storage/json_store.h"
#include "platform/windows/owned_process.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSet>
#include <QTimer>

namespace quickclash::platform {
class ServiceHost final : public QObject {
    Q_OBJECT
  public:
    explicit ServiceHost(QObject* parent = nullptr);
    [[nodiscard]] bool listen();
    void shutdown();

  private:
    [[nodiscard]] bool authenticated(QLocalSocket* socket);
    void receive(QLocalSocket* socket);
    void dispatch(QLocalSocket* socket, const QString& id, const QString& action,
                  const QJsonObject& payload);
    void answer(QLocalSocket* socket, const QString& id, domain::Result<QJsonObject> result);
    void start(QLocalSocket* socket, const QString& id, const QJsonObject& payload);
    void probe(QLocalSocket* socket, const QString& id, int attempt);
    void api(QLocalSocket* socket, const QString& id, const QByteArray& method,
             const QByteArray& path, const QJsonObject& body = {});
    void stop();
    void confirmStopped(QLocalSocket* socket, const QString& id, int attempt);
    [[nodiscard]] bool tunObserved() const;
    [[nodiscard]] bool tunClean() const;
    QLocalServer server_;
    QPointer<QLocalSocket> owner_;
    QHash<QLocalSocket*, QByteArray> buffers_;
    QHash<QLocalSocket*, QByteArray> identities_;
    QHash<QLocalSocket*, QString> sessions_;
    infra::ServiceProtocol protocol_;
    infra::JsonStore store_;
    OwnedProcess core_;
    OwnedProcess validator_;
    QTimer heartbeat_;
    QElapsedTimer lastHeartbeat_;
    QElapsedTimer tick_;
    QElapsedTimer requestWindow_;
    int requestCount_ = 0;
    QString epoch_;
    QString corePath_;
    QString configPath_;
    QString directory_;
    QString pipe_;
    QJsonObject groups_;
    QSet<QString> providers_;
    bool providerRefreshPending_ = false;
    quint16 mixedPort_ = 0;
    bool busy_ = false;
    bool ready_ = false;
    bool tunDesired_ = false;
    bool configClean_ = false;
    QPointer<QObject> logStream_;
    domain::LogBuffer logBuffer_{200, 512 * 1024};
    quint64 logSequence_ = 0;
    QMetaObject::Connection validationConnection_;
};
} // namespace quickclash::platform
