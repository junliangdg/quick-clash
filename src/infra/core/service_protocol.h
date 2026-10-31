#pragma once
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

namespace quickclash::infra {
inline constexpr int kServiceHandshakeTimeoutMs = 3000;
inline constexpr qint64 kServiceHandshakeMaxBytes = 16 * 1024;
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

  private:
    quint64 lastId_ = 0;
    QHash<QString, QByteArray> fingerprints_;
    QHash<QString, QJsonObject> results_;
    QSet<QString> pending_;
    QStringList order_;
};
} // namespace quickclash::infra
