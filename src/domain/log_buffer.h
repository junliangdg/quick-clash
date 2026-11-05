#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QString>

namespace quickclash::domain {
// Budgets count compact UTF-8 JSON bytes, including escaped messages and metadata.
class LogBuffer final {
  public:
    explicit LogBuffer(qsizetype entryLimit = 2000, qint64 byteLimit = 4 * 1024 * 1024);
    void append(const QJsonObject& entry);
    void clear();
    void markGap();
    void addDropped(quint64 count);
    [[nodiscard]] const QJsonArray& entries() const;
    [[nodiscard]] QJsonObject status() const;

  private:
    QJsonArray entries_;
    QQueue<qint64> sizes_;
    qsizetype entryLimit_;
    qint64 byteLimit_;
    qint64 bytes_ = 2;
    quint64 dropped_ = 0, truncated_ = 0, gaps_ = 0;
};
[[nodiscard]] QJsonObject redactedLogEntry(quint64 sequence, const QString& time,
                                           const QString& source, const QString& level,
                                           const QString& message);
[[nodiscard]] QJsonObject redactedLogEntry(quint64 sequence, const QString& time,
                                           const QString& source, const QString& level,
                                           const QString& message, bool retainTail);
} // namespace quickclash::domain
