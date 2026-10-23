#pragma once
#include "domain/contracts.h"

namespace quickclash::infra {
struct PreflightConfig {
    QByteArray semanticYaml;
    std::vector<QString> overriddenFields;
};
// Call on a bounded worker, never in a QML binding. Core -t validation is a separate step.
[[nodiscard]] domain::Result<PreflightConfig>
preflightConfig(const QByteArray& input, const domain::RuntimePreferences& preferences);
[[nodiscard]] domain::Result<QByteArray> materializeSession(const PreflightConfig& config,
                                                            const QByteArray& secret);
} // namespace quickclash::infra
