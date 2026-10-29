#include "domain/rule_snapshot.h"
#include <QHash>
#include <cmath>

namespace quickclash::domain {
namespace {
Error invalid() {
    Error error;
    error.code = QStringLiteral("CoreResponseInvalid");
    return error;
}
bool text(const QJsonValue& value, int limit, bool empty = true) {
    return value.isString() && value.toString().size() <= limit &&
           (empty || !value.toString().isEmpty());
}
Result<QJsonArray> projectProviders(const QJsonObject& response, const QString& kind) {
    if (!response.value(QStringLiteral("providers")).isObject())
        return invalid();
    const auto objects = response.value(QStringLiteral("providers")).toObject();
    if (objects.size() > 256)
        return invalid();
    QJsonArray next;
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if (!it.value().isObject() || it.key().isEmpty() || it.key().size() > 256)
            return invalid();
        const auto provider = it.value().toObject();
        // Compatible/inline core providers have no remote refresh operation.
        if (provider.value(QStringLiteral("vehicleType")) != QStringLiteral("HTTP"))
            continue;
        QJsonObject row{{QStringLiteral("name"), it.key()},
                        {QStringLiteral("kind"), kind},
                        {QStringLiteral("mode"), QStringLiteral("runtime")},
                        {QStringLiteral("type"), QStringLiteral("HTTP")},
                        {QStringLiteral("stale"), false}};
        const auto updated = provider.value(QStringLiteral("updatedAt"));
        if (text(updated, 64))
            row.insert(QStringLiteral("updatedAt"), updated);
        const auto behavior = provider.value(QStringLiteral("behavior"));
        if (text(behavior, 64))
            row.insert(QStringLiteral("behavior"), behavior);
        const auto count =
            kind == QStringLiteral("proxies")
                ? (provider.value(QStringLiteral("proxies")).isArray()
                       ? QJsonValue(provider.value(QStringLiteral("proxies")).toArray().size())
                       : QJsonValue{})
                : provider.value(QStringLiteral("ruleCount"));
        if (count.isDouble() && count.toDouble() >= 0 &&
            std::floor(count.toDouble()) == count.toDouble() && count.toDouble() <= 1000000000)
            row.insert(QStringLiteral("count"), count);
        next.append(row);
    }
    return next;
}
} // namespace
ProviderCategorySnapshot providerCategorySnapshot(const Result<QJsonObject>& response,
                                                  const QJsonArray& previous, const QString& kind) {
    Result<QJsonArray> projected = std::holds_alternative<QJsonObject>(response)
                                       ? projectProviders(std::get<QJsonObject>(response), kind)
                                       : Result<QJsonArray>{std::get<Error>(response)};
    if (const auto* rows = std::get_if<QJsonArray>(&projected))
        return {*rows, {}};
    ProviderCategorySnapshot retained{{}, std::get<Error>(projected).code};
    for (const auto& value : previous) {
        auto row = value.toObject();
        if (row.value(QStringLiteral("kind")) != kind)
            continue;
        row.insert(QStringLiteral("stale"), true);
        retained.rows.append(row);
    }
    return retained;
}
Result<QJsonArray> ruleSnapshot(const QJsonObject& response, const QJsonArray& providers) {
    const auto raw = response.value(QStringLiteral("rules"));
    if (!raw.isArray() && !raw.isNull())
        return invalid();
    auto rows = raw.toArray();
    if (rows.size() > 50000)
        return invalid();
    QHash<QString, QJsonObject> references;
    for (const auto& value : providers) {
        const auto provider = value.toObject();
        if (provider.value(QStringLiteral("kind")) == QStringLiteral("rules"))
            references.insert(provider.value(QStringLiteral("name")).toString(), provider);
    }
    for (qsizetype index = 0; index < rows.size(); ++index) {
        const auto rule = rows[index].toObject();
        if (!text(rule.value(QStringLiteral("type")), 64, false) ||
            !text(rule.value(QStringLiteral("payload")), 8192) ||
            !text(rule.value(QStringLiteral("proxy")), 256, false))
            return invalid();
        QJsonObject row{{QStringLiteral("position"), index + 1},
                        {QStringLiteral("type"), rule.value(QStringLiteral("type"))},
                        {QStringLiteral("payload"), rule.value(QStringLiteral("payload"))},
                        {QStringLiteral("proxy"), rule.value(QStringLiteral("proxy"))},
                        {QStringLiteral("source"), QStringLiteral("effective")}};
        const auto type = rule.value(QStringLiteral("type")).toString();
        if (type.compare(QStringLiteral("RuleSet"), Qt::CaseInsensitive) == 0 ||
            type.compare(QStringLiteral("RULE-SET"), Qt::CaseInsensitive) == 0) {
            const auto name = rule.value(QStringLiteral("payload")).toString();
            row.insert(QStringLiteral("source"), QStringLiteral("provider"));
            row.insert(QStringLiteral("providerName"), name);
            const auto provider = references.value(name);
            for (const auto* field : {"count", "updatedAt"})
                if (provider.contains(QLatin1String(field)))
                    row.insert(QLatin1String(field), provider.value(QLatin1String(field)));
        }
        rows[index] = row;
    }
    return rows;
}
} // namespace quickclash::domain
