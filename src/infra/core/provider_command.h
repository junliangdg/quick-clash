#pragma once
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <optional>

namespace quickclash::infra {
// Service IPC accepts only an active provider identity, never a caller path or URL.
[[nodiscard]] inline std::optional<QByteArray> providerRefreshPath(const QJsonObject& payload,
                                                                   const QSet<QString>& allowed) {
    if (payload.size() != 2 || !payload.value("kind").isString() ||
        !payload.value("name").isString())
        return std::nullopt;
    const auto kind = payload.value("kind").toString(), name = payload.value("name").toString();
    if ((kind != "proxies" && kind != "rules") || name.isEmpty() ||
        !allowed.contains(kind + ':' + name))
        return std::nullopt;
    return "/providers/" + kind.toUtf8() + '/' + QUrl::toPercentEncoding(name);
}
} // namespace quickclash::infra
