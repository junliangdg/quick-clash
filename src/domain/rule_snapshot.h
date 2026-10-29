#pragma once
#include "domain/contracts.h"
#include <QJsonArray>
#include <QJsonObject>

namespace quickclash::domain {
struct ProviderCategorySnapshot {
    QJsonArray rows;
    QString error;
};
// Project only page metadata. Failed categories retain their previous rows, marked stale.
[[nodiscard]] ProviderCategorySnapshot providerCategorySnapshot(const Result<QJsonObject>& response,
                                                                const QJsonArray& previous,
                                                                const QString& kind);
[[nodiscard]] Result<QJsonArray> ruleSnapshot(const QJsonObject& response,
                                              const QJsonArray& providers = {});
} // namespace quickclash::domain
