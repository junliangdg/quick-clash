#pragma once
#include "domain/contracts.h"
#include <QJsonArray>
#include <QJsonObject>

namespace quickclash::infra {
struct CustomGroupConfig {
    QByteArray yaml;
    QJsonObject view;
};
// Input has already passed the bounded metadata parser. Never mutate subscription bytes.
[[nodiscard]] domain::Result<CustomGroupConfig>
composeCustomGroups(const QByteArray& source, const QJsonArray& definitions,
                    const QJsonObject& observedProviders = {});
[[nodiscard]] domain::Result<QByteArray> composeProfileConfig(const QByteArray& source,
                                                              const QJsonObject& profile);
// Offline archives retain editable rules but freeze the available source names.
[[nodiscard]] QJsonArray archiveCustomGroups(const QJsonArray& definitions,
                                             const QJsonObject& view);
} // namespace quickclash::infra
