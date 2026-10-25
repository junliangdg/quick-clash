#pragma once
#include "domain/contracts.h"
#include <QHash>
#include <QJsonArray>
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
[[nodiscard]] QJsonObject configurationSummary(const QByteArray& semantic);
[[nodiscard]] QJsonArray orderedProxyGroups(const QJsonArray& configured, const QJsonObject& live);
[[nodiscard]] QJsonObject outboundSummary(const QJsonArray& groups, const QString& defaultPolicy,
                                          const QString& mode, bool ready);
// Provider URLs remain on the worker; only bounded HTTPS snapshots are expanded.
[[nodiscard]] domain::Result<QJsonArray> providerRequests(const QByteArray& input);
[[nodiscard]] domain::Result<QByteArray>
expandProviders(const QByteArray& input, const QHash<QString, QByteArray>& payloads);
// UI only receives a masked semantic template, never a materialized controller secret.
[[nodiscard]] QString redactedConfig(const QByteArray& semantic);
// Only canonical preflight output (or its hash-verified stored artifact), never raw input.
[[nodiscard]] domain::Result<QByteArray> materializeSession(const PreflightConfig& config,
                                                            const QByteArray& secret);
} // namespace quickclash::infra
