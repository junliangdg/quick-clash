#pragma once
#include "application/ports.h"
#include "platform/windows/owned_process.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace quickclash::infra {
class EngineSession final : public QObject {
    Q_OBJECT
  public:
    explicit EngineSession(QObject* parent = nullptr);
    ~EngineSession() override;
    // All paths have been resolved by the storage worker; caller owns validation/transactions.
    void start(const QString& executable, const QString& config, const QString& directory,
               const QByteArray& secret, quint16 mixedPort, quint16 controllerPort,
               const QString& expectedMode);
    void stop();
    void refreshGroups();
    void select(const QString& group, const QString& member);
    void setMode(const QString& mode);
    void reload(const QString& config);
    [[nodiscard]] bool running() const {
        return process_.running();
    }
    [[nodiscard]] bool ready() const {
        return ready_;
    }
    [[nodiscard]] quint32 pid() const {
        return process_.pid();
    }
  signals:
    void becameReady(const QString& version);
    void stopped(bool unexpected);
    void failed(const QString& code);
    void groupsChanged(const QJsonObject& groups);
    void mutationFinished(const QString& kind, bool success);

  private:
    using ReplyHandler = std::function<void(domain::Result<QJsonObject>)>;
    void request(const QByteArray& method, const QByteArray& path, const QJsonObject& body,
                 ReplyHandler handler);
    void probe();
    void fail(const QString& code);
    platform::OwnedProcess process_;
    QNetworkAccessManager network_;
    QTimer probeTimer_;
    QTimer deadline_;
    QList<QPointer<QNetworkReply>> replies_;
    QByteArray secret_;
    QString expectedMode_;
    quint16 mixedPort_ = 0;
    quint16 controllerPort_ = 0;
    quint64 generation_ = 0;
    bool ready_ = false;
    bool probing_ = false;
};
} // namespace quickclash::infra
