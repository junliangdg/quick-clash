#include "domain/resource_snapshot.h"
#include <limits>
#include <utility>

namespace quickclash::domain {
namespace {
constexpr const char* metrics[] = {"privateWorkingSet", "workingSet", "privateCommit"};
bool valid(const QJsonObject& process) {
    for (const auto* field : metrics) {
        const auto value = process.value(QLatin1String(field));
        bool ok = false;
        const auto text = value.toString();
        if (!value.isString() || text.isEmpty())
            return false;
        for (const auto character : text)
            if (character < QLatin1Char('0') || character > QLatin1Char('9'))
                return false;
        (void)text.toULongLong(&ok);
        if (!ok)
            return false;
    }
    return true;
}
} // namespace
QJsonObject resourceSnapshot(const QJsonObject& host, const QJsonObject& serviceObservation,
                             const QJsonObject& core, bool coreExpected,
                             const QJsonObject& validator, bool validatorExpected) {
    QJsonObject memory;
    bool complete = valid(host) && serviceObservation.value(QStringLiteral("complete")).toBool();
    if (valid(host)) {
        memory.insert(QStringLiteral("host"), host);
        QJsonObject client;
        for (const auto* field : metrics)
            client.insert(QLatin1String(field), host.value(QLatin1String(field)));
        memory.insert(QStringLiteral("client"), client);
    }
    const auto service = serviceObservation.value(QStringLiteral("service")).toObject();
    if (!service.isEmpty()) {
        complete &= valid(service);
        if (valid(service)) {
            memory.insert(QStringLiteral("service"), service);
            if (valid(host)) {
                auto client = memory.value(QStringLiteral("client")).toObject();
                for (const auto* field : metrics) {
                    const auto key = QLatin1String(field);
                    const auto left = client.value(key).toString().toULongLong();
                    const auto right = service.value(key).toString().toULongLong();
                    if (right > std::numeric_limits<quint64>::max() - left) {
                        client.remove(key);
                        complete = false;
                    } else
                        client.insert(key, QString::number(left + right));
                }
                memory.insert(QStringLiteral("client"), client);
            }
        }
    }
    for (const auto& item :
         {std::pair{QStringLiteral("core"), std::pair{core, coreExpected}},
          std::pair{QStringLiteral("validator"), std::pair{validator, validatorExpected}},
          std::pair{
              QStringLiteral("serviceValidator"),
              std::pair{serviceObservation.value(QStringLiteral("validator")).toObject(),
                        serviceObservation.value(QStringLiteral("validatorExpected")).toBool()}}}) {
        if (!item.second.second)
            continue;
        if (valid(item.second.first))
            memory.insert(item.first, item.second.first);
        else
            complete = false;
    }
    return {{QStringLiteral("memory"), memory}, {QStringLiteral("memoryComplete"), complete}};
}
} // namespace quickclash::domain
