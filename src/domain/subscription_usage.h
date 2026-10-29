#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>

namespace quickclash::domain {
// Missing, ambiguous and malformed fields stay absent; counters are decimal strings.
[[nodiscard]] QJsonObject parseSubscriptionUsage(const QByteArray& header);
// Presentation summary only; provider usage never becomes local traffic telemetry.
[[nodiscard]] QJsonObject subscriptionUsageStatus(const QJsonObject& usage, const QDateTime& now);
} // namespace quickclash::domain
