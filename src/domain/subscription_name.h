#pragma once
#include <QByteArray>
#include <QString>

namespace quickclash::domain {
// Bounded metadata only; an empty result leaves the caller's generic fallback intact.
[[nodiscard]] QString subscriptionName(const QByteArray& title, const QByteArray& disposition);
} // namespace quickclash::domain
