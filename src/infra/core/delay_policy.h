#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <optional>

namespace quickclash::infra {
// A usable node should answer within a few hundred milliseconds. Two seconds leaves room
// for slow but valid nodes while cutting the all-timeout tail of a large batch.
inline constexpr int kNodeDelayTimeoutMs = 2000;
// Both transports use the same queue limit; the local HTTP controller has six connections.
inline constexpr int kNodeDelayConcurrency = 6;
// A sent service request can still reach the core while the client discards its reply.
inline constexpr int kNodeDelayHistoryFenceMs = 5000;

[[nodiscard]] inline bool validNodeDelayTarget(const QString& target) {
    const QUrl url(target);
    return target.size() <= 8192 && url.isValid() &&
           (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https")) &&
           !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment();
}

// The service only forwards a selected catalog entry and a validated target to the core.
[[nodiscard]] inline std::optional<QByteArray> serviceDelayPath(const QJsonObject& payload,
                                                                const QJsonObject& catalog) {
    const auto name = payload.value(QStringLiteral("name"));
    const auto target = payload.value(QStringLiteral("url"));
    if (!name.isString() || !target.isString() || !catalog.contains(name.toString()) ||
        !validNodeDelayTarget(target.toString()))
        return std::nullopt;
    return "/proxies/" + QUrl::toPercentEncoding(name.toString()) +
           "/delay?timeout=" + QByteArray::number(kNodeDelayTimeoutMs) +
           "&url=" + QUrl::toPercentEncoding(target.toString());
}
} // namespace quickclash::infra
