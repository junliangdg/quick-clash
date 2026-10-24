#pragma once
#include "domain/contracts.h"
#include <QJsonObject>

namespace quickclash::infra {
struct PreflightConfig {
    QByteArray semanticYaml;
    std::vector<QString> overriddenFields;
};
// Detect standard Clash/Mihomo YAML independently of an endpoint's MIME type.
// Empty means a configuration-shaped body; full policy and official -t still follow.
[[nodiscard]] QString subscriptionPayloadError(const QByteArray& input);
// Call on a bounded worker, never in a QML binding. Core -t validation is a separate step.
[[nodiscard]] domain::Result<PreflightConfig>
preflightConfig(const QByteArray& input, const domain::RuntimePreferences& preferences);
// HMACs are local identity tokens, not reversible endpoint/credential descriptions.
[[nodiscard]] QJsonObject nodeIdentities(const QByteArray& semantic, const QByteArray& key);
// UI only receives a masked semantic template, never a materialized controller secret.
[[nodiscard]] QString redactedConfig(const QByteArray& semantic);
[[nodiscard]] domain::Result<QByteArray> materializeSession(const PreflightConfig& config,
                                                            const QByteArray& secret);
} // namespace quickclash::infra
