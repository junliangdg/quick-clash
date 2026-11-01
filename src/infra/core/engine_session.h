#pragma once
#include "application/desktop_process.h"
#include "application/ports.h"
#include "infra/core/core_health.h"
#include "infra/core/service_client.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>
#include <memory>
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
    EngineSession(std::unique_ptr<application::IDesktopProcess> process, QObject* parent);
    ~EngineSession() override;
    // All paths have been resolved by the storage worker; caller owns validation/transactions.
    void start(const QString& executable, const QString& config, const QString& directory,
               const QByteArray& secret, quint16 mixedPort, quint16 controllerPort,
               const QString& expectedMode, bool serviceMode = false,
               const QJsonObject& geoSnapshot = {}, const QString& storageRoot = {},
               const QString& coreArtifactId = {});
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
        return process_->running() || serviceMode_;
    }
    [[nodiscard]] bool ready() const {
        return ready_;
    }
    [[nodiscard]] QJsonObject listenerIssue() const {
        return listenerIssue_;
    }
    [[nodiscard]] QString tunDevice() const {
        return tunDevice_;
    }
    [[nodiscard]] bool cleanupUncertain() const {
        return serviceCleanupUncertain_;
    }
    [[nodiscard]] bool tunActive() const {
        return tunState_ == QStringLiteral("On");
    }
    [[nodiscard]] QString tunState() const {
        return tunState_;
    }
    [[nodiscard]] QJsonObject coreExitDiagnostics() const {
        return coreExitDiagnostics_;
    }
    [[nodiscard]] bool usingService() const {
        return serviceMode_;
    }
    [[nodiscard]] quint32 pid() const {
        return serviceMode_ ? servicePid_ : process_->pid();
    }
    [[nodiscard]] QJsonObject memory() const {
        return serviceMode_ ? QJsonObject{} : process_->memory();
    }
  signals:
    void becameReady(const QString& version);
    void stopped(bool unexpected);
    void failed(const QString& code);
    void groupsChanged(const QJsonObject& groups);
    void groupsRefreshFailed(const QString& code);
    void mutationFinished(const QString& kind, bool success);
    void streamData(const QString& name, const QJsonObject& data);
    void tunStateChanged(const QString& state);

  private:
    using ReplyHandler = std::function<void(domain::Result<QJsonObject>)>;
    void request(const QByteArray& method, const QByteArray& path, const QJsonObject& body,
                 ReplyHandler handler);
    void probe();
    void fail(const QString& code);
    void syncServiceLogs();
    void setTunState(const QString& state);
    std::unique_ptr<application::IDesktopProcess> process_;
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
    QJsonObject listenerIssue_;
    QString tunDevice_;
    QJsonObject coreExitDiagnostics_;
    QString tunState_ = QStringLiteral("Off");
    TunObservationTracker tunTracker_;
    int statusFailures_ = 0;
    QString expectedMode_;
    QString expectedVersion_;
    quint16 mixedPort_ = 0;
    quint16 controllerPort_ = 0;
    quint64 generation_ = 0;
    bool ready_ = false;
    bool probing_ = false;
    bool listenersPending_ = false;
    bool configPending_ = false;
};
} // namespace quickclash::infra
