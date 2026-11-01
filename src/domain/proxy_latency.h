#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace quickclash::domain {
// Latest observations per target; private URLs never enter UI/diagnostic data.
class ProxyLatency final {
  public:
    void ingest(const QJsonObject& proxies, const QDateTime& now);
    void manual(const QString& name, const QString& target, const QString& state,
                const QDateTime& time, int delay = -1);
    void invalidateManual(const QString& target = {});
    void clear();
    [[nodiscard]] QJsonObject details(const QDateTime& now) const;
    [[nodiscard]] static QString targetHash(const QString& target);
    [[nodiscard]] static QString text(const QJsonObject& observation);

  private:
    void insert(const QString& name, QJsonObject observation);
    void trim(const QString& name);
    QJsonObject records_;
    QJsonObject manual_;
};
} // namespace quickclash::domain
