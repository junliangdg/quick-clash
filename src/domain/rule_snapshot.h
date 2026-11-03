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
// Mark only the exact, contiguous custom-rule prefix present in the confirmed runtime snapshot.
void markCustomRulePrefix(QJsonArray& rows, const QJsonObject& userRules,
                          const QString& profileId);
} // namespace quickclash::domain
