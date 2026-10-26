#pragma once
#include "application/ports.h"
#include "infra/core/service_client.h"
#include "platform/windows/owned_process.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>
#include <optional>

namespace quickclash::infra {
struct ServiceLogDemand {
    [[nodiscard]] std::optional<bool> begin(quint64 generation, bool enabled) {
        if (pending || (generation == acknowledgedGeneration && enabled == acknowledgedEnabled))
            return std::nullopt;
        pending = true;
        // A reused lease must clear the old service buffer before enabling reception again.
        return acknowledgedEnabled && generation != acknowledgedGeneration ? false : enabled;
    }
    void finish(quint64 generation, bool enabled, bool success) {
        pending = false;
        if (success) {
            acknowledgedGeneration = generation;
            acknowledgedEnabled = enabled;
        }
    }
    [[nodiscard]] bool receiving(quint64 generation) const {
        return !pending && acknowledgedEnabled && acknowledgedGeneration == generation;
    }
    quint64 acknowledgedGeneration = 0;
    bool acknowledgedEnabled = false;
    bool pending = false;
};

class EngineSession final : public QObject {
    Q_OBJECT
  public:
    using ApiHandler = std::function<void(domain::Result<QJsonObject>)>;
    explicit EngineSession(QObject* parent = nullptr);
    ~EngineSession() override;
    // All paths have been resolved by the storage worker; caller owns validation/transactions.
    void start(const QString& executable, const QString& config, const QString& directory,
               const QByteArray& secret, quint16 mixedPort, quint16 controllerPort,
               const QString& expectedMode, bool serviceMode = false);
    void probeService(ApiHandler handler);
    void serviceResources(ApiHandler handler);
    void stop();
    void refreshGroups();
    void select(const QString& group, const QString& member);
    void setMode(const QString& mode);
    void reload(const QString& config);
    void api(const QByteArray& method, const QByteArray& path, const QJsonObject& body,
             ApiHandler handler);
    void setStream(const QString& name, bool enabled);
    [[nodiscard]] bool running() const {
        return process_.running() || serviceMode_;
    }
    [[nodiscard]] bool ready() const {
        return ready_;
    }
    [[nodiscard]] bool tunActive() const {
        return serviceMode_ && ready_;
    }
    [[nodiscard]] quint32 pid() const {
        return serviceMode_ ? servicePid_ : process_.pid();
    }
  signals:
    void becameReady(const QString& version);
    void stopped(bool unexpected);
    void failed(const QString& code);
    void groupsChanged(const QJsonObject& groups);
    void mutationFinished(const QString& kind, bool success);
    void streamData(const QString& name, const QJsonObject& data);

  private:
    using ReplyHandler = std::function<void(domain::Result<QJsonObject>)>;
    void request(const QByteArray& method, const QByteArray& path, const QJsonObject& body,
                 ReplyHandler handler);
    void probe();
    void fail(const QString& code);
    void syncServiceLogs();
    platform::OwnedProcess process_;
    ServiceClient service_;
    QTimer servicePoll_;
    QSet<QString> serviceStreams_;
    QHash<QString, quint64> streamGenerations_;
    ServiceLogDemand serviceLogDemand_;
    bool serviceMode_ = false;
    quint32 servicePid_ = 0;
    bool serviceStopping_ = false;
    bool serviceCleanupUncertain_ = false;
    std::function<void()> afterServiceStop_;
    QNetworkAccessManager network_;
    QTimer probeTimer_;
    QTimer deadline_;
    QList<QPointer<QNetworkReply>> replies_;
    QHash<QString, QPointer<QNetworkReply>> streams_;
    QByteArray secret_;
    QString expectedMode_;
    quint16 mixedPort_ = 0;
    quint16 controllerPort_ = 0;
    quint64 generation_ = 0;
    bool ready_ = false;
    bool probing_ = false;
};
} // namespace quickclash::infra
