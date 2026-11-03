#pragma once
#include "domain/contracts.h"
#include <QJsonObject>

namespace quickclash::infra {
// JSON operations remain separate from the byte-exact imported source.
[[nodiscard]] domain::Result<QByteArray> composeOverlay(const QByteArray& source,
                                                        const QJsonObject& overlay);
// User rules are stored separately from profile overlays. The profile ID selects its
// local rules and the explicit group used by global PROXY rules.
[[nodiscard]] domain::Result<QByteArray>
composeUserRules(const QByteArray& source, const QJsonObject& rulebook, const QString& profileId);
[[nodiscard]] domain::Result<QJsonObject> userRuleTargets(const QByteArray& source);
} // namespace quickclash::infra
