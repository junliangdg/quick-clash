#include "domain/connection_snapshot.h"
#include <QHash>
#include <QSet>
#include <cmath>

namespace quickclash::domain {
Result<QJsonArray> connectionSnapshot(const QJsonObject& response, const QJsonArray& previous,
                                      const QString& epoch, qint64 elapsedMs) {
    const auto failure = [](const char* code) {
        Error error;
        error.code = QString::fromLatin1(code);
        return error;
    };
    // Go's nil slice is serialized as null by the pinned core when no connections exist.
    if (!response.contains(QStringLiteral("connections")) ||
        (!response.value(QStringLiteral("connections")).isArray() &&
         !response.value(QStringLiteral("connections")).isNull()))
        return failure("CoreResponseInvalid");
    auto next = response.value(QStringLiteral("connections")).toArray();
    if (next.size() > 10000)
        return failure("CoreResponseTooLarge");
    QHash<QString, QJsonObject> old;
    for (const auto& value : previous) {
        const auto entry = value.toObject();
        if (entry.value(QStringLiteral("epoch")) == epoch)
            old.insert(entry.value(QStringLiteral("id")).toString(), entry);
    }
    QSet<QString> seen;
    for (qsizetype i = 0; i < next.size(); ++i) {
        auto entry = next[i].toObject();
        const auto id = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || id.size() > 256 || seen.contains(id) ||
            !entry.value(QStringLiteral("metadata")).isObject())
            return failure("CoreResponseInvalid");
        seen.insert(id);
        const auto before = old.value(id);
        for (const auto* field : {"upload", "download"}) {
            const auto key = QLatin1String(field);
            const auto counter = entry.value(key);
            const auto rateKey = QString::fromLatin1(field) + QStringLiteral("Rate");
            entry.remove(rateKey);
            if (!counter.isDouble() || counter.toDouble() < 0 || !std::isfinite(counter.toDouble()))
                return failure("CoreResponseInvalid");
            const auto prior = before.value(key);
            if (prior.isDouble() && counter.toDouble() >= prior.toDouble() && elapsedMs >= 100 &&
                elapsedMs <= 10000)
                entry.insert(rateKey, (counter.toDouble() - prior.toDouble()) * 1000 / elapsedMs);
        }
        entry.insert(QStringLiteral("epoch"), epoch);
        next[i] = entry;
    }
    return next;
}
} // namespace quickclash::domain
