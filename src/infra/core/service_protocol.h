#pragma once
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

namespace quickclash::infra {
inline constexpr int kServiceHandshakeTimeoutMs = 3000;
inline constexpr qint64 kServiceHandshakeMaxBytes = 16 * 1024;
inline constexpr int kServiceRateWindowMs = 1000;
inline constexpr int kServiceRateLimit = 24;
// Pace all commands, including status and telemetry, below the service's shared rate limit.
// Concurrent probes keep their own deadlines, starting only after dispatch to the service.
inline constexpr int kServiceRequestSpacingMs = 50;
static_assert(kServiceRateWindowMs / kServiceRequestSpacingMs < kServiceRateLimit);
// One GUI session may have several bounded feature requests and a node-delay batch in
// flight. Keep the service backstop above the delay batch ceiling without making it
// unbounded; the privileged service must still reject command storms.
inline constexpr int kServiceRequestLimit = 40;
inline constexpr qsizetype kServiceReplayByteLimit = 4 * 1024 * 1024;
struct ServiceRequest {
    QString id;
    QString action;
    QString session;
    QString epoch;
    QJsonObject payload;
};
// One authenticated connection owns this bounded replay ledger. Reconnects never resume it.
class ServiceProtocol final {
  public:
    enum class Disposition { Execute, Cached, Pending, Reject, Disconnect };
    struct Decision {
        Disposition disposition = Disposition::Reject;
        ServiceRequest request;
        QJsonObject response;
        QString error;
    };
    [[nodiscard]] Decision accept(const QJsonObject& message);
    void complete(const QString& id, const QJsonObject& response);
    [[nodiscard]] bool pending(const QString& id) const;
    void clear();
    [[nodiscard]] static bool validEnvelope(const QJsonObject& message);
    [[nodiscard]] static bool validResponse(const QJsonObject& message);
    [[nodiscard]] static QString executableBuild(const QString& path);
    [[nodiscard]] static QJsonObject capabilities(const QString& serviceBuild);
    [[nodiscard]] static QString capabilityError(const QJsonObject& actual,
                                                 const QString& expectedBuild);

  private:
    quint64 lastId_ = 0;
    QHash<QString, QByteArray> fingerprints_;
    QHash<QString, QByteArray> results_;
    qsizetype resultBytes_ = 0;
    QSet<QString> pending_;
    QStringList order_;
};
} // namespace quickclash::infra
