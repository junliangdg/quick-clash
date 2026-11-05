#include "domain/log_buffer.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <limits>

namespace quickclash::domain {
namespace {
void increment(quint64& value, quint64 amount = 1) {
    value += std::min(amount, std::numeric_limits<quint64>::max() - value);
}
} // namespace
LogBuffer::LogBuffer(qsizetype entryLimit, qint64 byteLimit)
    : entryLimit_(std::max(qsizetype{1}, entryLimit)), byteLimit_(std::max(qint64{2}, byteLimit)) {}
void LogBuffer::append(const QJsonObject& entry) {
    if (entry.value(QStringLiteral("truncated")).toBool())
        increment(truncated_);
    const auto size = QJsonDocument(entry).toJson(QJsonDocument::Compact).size() + 1;
    if (size + 2 > byteLimit_) {
        increment(dropped_);
        return;
    }
    entries_.append(entry);
    sizes_.enqueue(size);
    bytes_ += size;
    while (entries_.size() > entryLimit_ || bytes_ > byteLimit_) {
        bytes_ -= sizes_.dequeue();
        entries_.removeFirst();
        increment(dropped_);
    }
}
void LogBuffer::clear() {
    entries_ = {};
    sizes_.clear();
    bytes_ = 2;
    dropped_ = truncated_ = gaps_ = 0;
}
void LogBuffer::markGap() {
    increment(gaps_);
}
void LogBuffer::addDropped(quint64 count) {
    increment(dropped_, count);
}
const QJsonArray& LogBuffer::entries() const {
    return entries_;
}
QJsonObject LogBuffer::status() const {
    return {{QStringLiteral("logEntries"), entries_.size()},
            {QStringLiteral("logBytes"), QString::number(bytes_)},
            {QStringLiteral("droppedLogs"), QString::number(dropped_)},
            {QStringLiteral("truncatedLogs"), QString::number(truncated_)},
            {QStringLiteral("logGaps"), QString::number(gaps_)}};
}
QJsonObject redactedLogEntry(quint64 sequence, const QString& time, const QString& source,
                             const QString& level, const QString& message) {
    return redactedLogEntry(sequence, time, source, level, message, false);
}
QJsonObject redactedLogEntry(quint64 sequence, const QString& time, const QString& source,
                             const QString& level, const QString& message, bool retainTail) {
    // Bound regex work by the transport line budget; redact before truncating output.
    auto safe = message.left(65536);
    static const QRegularExpression url(QStringLiteral(R"qc((?i)[a-z][a-z0-9+.-]*://[^\s]+)qc"));
    static const QRegularExpression credential(QStringLiteral(
        R"qc((?i)["']?(password|passwd|secret|token|authorization|proxy-authorization|uuid|api[-_]?key|private[-_]?key|access[-_]?key)["']?\s*[:=]\s*("[^"]*"|'[^']*'|[^\r\n,;}]+))qc"));
    static const QRegularExpression bearer(
        QStringLiteral(R"qc((?i)\b(bearer|basic)\s+[^\s,;]+)qc"));
    static const QRegularExpression spacedCredential(
        QStringLiteral(R"qc((?i)\b(password|passwd|secret|token|uuid)\s+[^\s,;]+)qc"));
    safe.replace(url, QStringLiteral("[url]"));
    safe.replace(credential, QStringLiteral("[credential]"));
    safe.replace(bearer, QStringLiteral("[credential]"));
    safe.replace(spacedCredential, QStringLiteral("[credential]"));
    const bool truncated = message.size() > 2048 || safe.size() > 2048;
    safe = retainTail ? safe.right(2048) : safe.left(2048);
    auto normalized = level.left(32).toLower();
    if (normalized == QStringLiteral("warn"))
        normalized = QStringLiteral("warning");
    else if (normalized == QStringLiteral("fatal"))
        normalized = QStringLiteral("error");
    else if (!QStringList{QStringLiteral("debug"), QStringLiteral("info"),
                          QStringLiteral("warning"), QStringLiteral("error"),
                          QStringLiteral("silent")}
                  .contains(normalized))
        normalized = QStringLiteral("unknown");
    QJsonObject result{{QStringLiteral("localSeq"), QString::number(sequence)},
                       {QStringLiteral("time"), time},
                       {QStringLiteral("source"), source},
                       {QStringLiteral("level"), normalized},
                       {QStringLiteral("message"), safe},
                       {QStringLiteral("truncated"), truncated}};
    static const QRegularExpression levelName(QStringLiteral("^[A-Za-z]{1,32}$"));
    if (levelName.match(level).hasMatch())
        result.insert(QStringLiteral("originalLevel"), level);
    return result;
}
} // namespace quickclash::domain
