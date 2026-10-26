#pragma once
#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>

namespace quickclash::application {
// GUI-owned leases; revisions advance on both acquisition and release, including same-session
// reuse.
struct RuntimeDemands {
    QSet<QString> active;
    QHash<QString, quint64> generations;
    [[nodiscard]] static QStringList streams() {
        return {QStringLiteral("connections"), QStringLiteral("rules"),
                QStringLiteral("providers"),   QStringLiteral("logs"),
                QStringLiteral("log_feed"),    QStringLiteral("traffic"),
                QStringLiteral("resources"),   QStringLiteral("proxies")};
    }
    void setActive(const QStringList& streams) {
        QSet<QString> next;
        for (const auto& stream : streams)
            if (RuntimeDemands::streams().contains(stream))
                next.insert(stream);
        for (const auto& stream : RuntimeDemands::streams())
            if (active.contains(stream) != next.contains(stream))
                ++generations[stream];
        active = next;
    }
    [[nodiscard]] bool accepts(const QString& stream, quint64 generation) const {
        return generation != 0 && active.contains(stream) &&
               generations.value(stream) == generation;
    }
};
// Small status channels have the same lease as their associated page data.
[[nodiscard]] inline QString channelStream(const QString& channel) {
    if (channel == QStringLiteral("connection_status"))
        return QStringLiteral("connections");
    if (channel == QStringLiteral("log_status"))
        return QStringLiteral("logs");
    if (QStringList{QStringLiteral("connections"), QStringLiteral("rules"),
                    QStringLiteral("providers"), QStringLiteral("logs"), QStringLiteral("traffic"),
                    QStringLiteral("resources")}
            .contains(channel))
        return channel;
    return {};
}
[[nodiscard]] inline QStringList scopedChannels() {
    return {QStringLiteral("connections"), QStringLiteral("connection_status"),
            QStringLiteral("rules"),       QStringLiteral("providers"),
            QStringLiteral("logs"),        QStringLiteral("log_status"),
            QStringLiteral("traffic"),     QStringLiteral("resources")};
}
struct RuntimeRequest {
    QString stream;
    QString epoch;
    quint64 demand = 0;
    quint64 operation = 0;
    quint64 request = 0;
    [[nodiscard]] bool matches(const RuntimeDemands& demands, const QString& currentEpoch,
                               quint64 currentOperation, quint64 currentRequest) const {
        return epoch == currentEpoch && operation == currentOperation &&
               request == currentRequest && demands.accepts(stream, demand);
    }
};
// Each value is a complete channel state, never a delta against a discarded baseline.
struct RuntimeUpdate {
    quint64 generation = 0;
    QString epoch;
    QJsonObject values;
    QHash<QString, quint64> demandGenerations{};
    QSet<QString> invalidated{};
};
} // namespace quickclash::application
Q_DECLARE_METATYPE(quickclash::application::RuntimeUpdate)
