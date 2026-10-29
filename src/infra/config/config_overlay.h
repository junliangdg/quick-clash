#pragma once
#include "domain/contracts.h"
#include <QJsonObject>

namespace quickclash::infra {
// JSON operations remain separate from the byte-exact imported source.
[[nodiscard]] domain::Result<QByteArray> composeOverlay(const QByteArray& source,
                                                        const QJsonObject& overlay);
} // namespace quickclash::infra
