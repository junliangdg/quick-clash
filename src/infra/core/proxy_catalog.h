#pragma once
#include <QJsonObject>

namespace quickclash::infra {
// Service authorization needs live names and selectable members, never latency histories.
[[nodiscard]] inline QJsonObject proxySelectionCatalog(const QJsonObject& proxies) {
    QJsonObject result;
    for (auto it = proxies.constBegin(); it != proxies.constEnd(); ++it) {
        const auto proxy = it.value().toObject();
        QJsonObject entry;
        if (proxy.value(QStringLiteral("type")) == QStringLiteral("Selector")) {
            entry.insert(QStringLiteral("type"), QStringLiteral("Selector"));
            entry.insert(QStringLiteral("all"), proxy.value(QStringLiteral("all")));
        }
        result.insert(it.key(), entry);
    }
    return result;
}
} // namespace quickclash::infra
