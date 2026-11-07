#include "domain/resource_snapshot.h"
#include <limits>
#include <utility>

namespace quickclash::domain {
namespace {
constexpr const char* metrics[] = {"privateWorkingSet", "workingSet", "privateCommit"};
bool validMetric(const QJsonValue& value) {
    const auto text = value.toString();
    if (!value.isString() || text.isEmpty())
        return false;
    for (const auto character : text)
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return false;
    bool ok = false;
    (void)text.toULongLong(&ok);
    return ok;
}
QJsonObject knownMetrics(const QJsonObject& process, bool& complete) {
    QJsonObject known;
    bool hasMetric = false;
    for (const auto* field : metrics) {
        const auto key = QLatin1String(field);
        const auto value = process.value(key);
        if (validMetric(value)) {
            known.insert(key, value);
            hasMetric = true;
        } else
            complete = false;
    }
    if (hasMetric) {
        for (const auto* field : {"pid", "created"}) {
            const auto key = QLatin1String(field);
            if (process.contains(key))
                known.insert(key, process.value(key));
        }
    }
    return known;
}
} // namespace
QJsonObject resourceSnapshot(const QJsonObject& host, const QJsonObject& serviceObservation,
                             const QJsonObject& core, bool coreExpected,
                             const QJsonObject& validator, bool validatorExpected,
                             const QJsonObject& coreExtractor, bool extractorExpected,
                             const QJsonObject& networkRelay, bool relayExpected) {
    QJsonObject memory;
    bool complete = serviceObservation.value(QStringLiteral("complete")).toBool();
    const auto knownHost = knownMetrics(host, complete);
    QJsonObject client;
    if (!knownHost.isEmpty()) {
        memory.insert(QStringLiteral("host"), knownHost);
        for (const auto* field : metrics) {
            const auto key = QLatin1String(field);
            if (knownHost.contains(key))
                client.insert(key, knownHost.value(key));
        }
    }
    const auto service = serviceObservation.value(QStringLiteral("service")).toObject();
    for (const auto& helper : {std::pair{QStringLiteral("service"), service},
                               std::pair{QStringLiteral("coreExtractor"), coreExtractor},
                               std::pair{QStringLiteral("networkRelay"), networkRelay}}) {
        if (helper.second.isEmpty()) {
            if (helper.first == QStringLiteral("coreExtractor") && extractorExpected)
                complete = false;
            if (helper.first == QStringLiteral("networkRelay") && relayExpected)
                complete = false;
            continue;
        }
        const auto known = knownMetrics(helper.second, complete);
        if (!known.isEmpty())
            memory.insert(helper.first, known);
        for (const auto* field : metrics) {
            const auto key = QLatin1String(field);
            if (!client.contains(key) || !known.contains(key))
                continue;
            const auto left = client.value(key).toString().toULongLong();
            const auto right = known.value(key).toString().toULongLong();
            if (right > std::numeric_limits<quint64>::max() - left) {
                client.remove(key);
                complete = false;
            } else
                client.insert(key, QString::number(left + right));
        }
    }
    if (!client.isEmpty())
        memory.insert(QStringLiteral("client"), client);
    for (const auto& item :
         {std::pair{QStringLiteral("core"), std::pair{core, coreExpected}},
          std::pair{QStringLiteral("validator"), std::pair{validator, validatorExpected}},
          std::pair{
              QStringLiteral("serviceValidator"),
              std::pair{serviceObservation.value(QStringLiteral("validator")).toObject(),
                        serviceObservation.value(QStringLiteral("validatorExpected")).toBool()}}}) {
        if (!item.second.second)
            continue;
        const auto known = knownMetrics(item.second.first, complete);
        if (!known.isEmpty())
            memory.insert(item.first, known);
    }
    return {{QStringLiteral("memory"), memory}, {QStringLiteral("memoryComplete"), complete}};
}
} // namespace quickclash::domain
