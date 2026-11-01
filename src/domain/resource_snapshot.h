#pragma once
#include <QJsonObject>

namespace quickclash::domain {
// Missing expected processes make the result partial; their counters are never synthesized.
[[nodiscard]] QJsonObject resourceSnapshot(const QJsonObject& host,
                                           const QJsonObject& serviceObservation,
                                           const QJsonObject& core, bool coreExpected,
                                           const QJsonObject& validator, bool validatorExpected,
                                           const QJsonObject& coreExtractor = {},
                                           bool extractorExpected = false);
} // namespace quickclash::domain
