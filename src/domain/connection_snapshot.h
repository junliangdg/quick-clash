#pragma once
#include "domain/contracts.h"
#include <QJsonArray>
#include <QJsonObject>

namespace quickclash::domain {
// A bounded complete snapshot; never return a truncated view as a complete collection.
[[nodiscard]] Result<QJsonArray> connectionSnapshot(const QJsonObject& response,
                                                    const QJsonArray& previous,
                                                    const QString& epoch, qint64 elapsedMs);
} // namespace quickclash::domain
