#pragma once
#include "domain/contracts.h"
#include <QHash>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>

namespace quickclash::infra {
[[nodiscard]] domain::Result<QByteArray> sessionWithMode(const QByteArray& session,
                                                         const QString& mode);
struct PreflightConfig {
    QByteArray semanticYaml;
    std::vector<QString> overriddenFields;
};
// Detect standard Clash/Mihomo YAML independently of an endpoint's MIME type.
// Empty means a configuration-shaped body; full policy and official -t still follow.
[[nodiscard]] QString subscriptionPayloadError(const QByteArray& input);
// Wrap native URI/Base64 node lists in a managed file provider for official parsing.
[[nodiscard]] QByteArray coreSubscriptionConfig(const QByteArray& input);
// Call on a bounded worker, never in a QML binding. Core -t validation is a separate step.
[[nodiscard]] domain::Result<PreflightConfig>
preflightConfig(const QByteArray& input, const domain::RuntimePreferences& preferences);
// HMACs are local identity tokens, not reversible endpoint/credential descriptions.
[[nodiscard]] QJsonObject nodeIdentities(const QByteArray& semantic, const QByteArray& key);
[[nodiscard]] QJsonObject configurationSummary(const QByteArray& semantic);
[[nodiscard]] QJsonArray orderedProxyGroups(const QJsonArray& configured, const QJsonObject& live);
[[nodiscard]] QJsonObject outboundSummary(const QJsonArray& groups, const QString& defaultPolicy,
                                          const QString& mode, bool ready);
// Provider URLs remain on the worker. Raw HTTP providers require managed materialization.
[[nodiscard]] domain::Result<QJsonArray> providerRequests(const QByteArray& input);
// Preserve provider/group options through the official core instead of flattening them.
[[nodiscard]] bool requiresRuntimeProviders(const QByteArray& input);
[[nodiscard]] bool providerPayloadsRequireRuntime(const QHash<QString, QIODevice*>& payloads);
struct ProviderValidation {
    QByteArray config;
    QList<QByteArray> targets;
};
[[nodiscard]] domain::Result<ProviderValidation>
providerValidationConfig(const QByteArray& semantic, const QString& pipe);
[[nodiscard]] domain::Result<QByteArray>
expandProviders(const QByteArray& input, const QHash<QString, QByteArray>& payloads);
// Preserve managed HTTP providers and RULE-SET; fallback payloads have already passed expansion.
// Paths are derived from provider identity, never copied from a subscription.
[[nodiscard]] domain::Result<QByteArray>
runtimeProviderConfig(const QByteArray& input, const QHash<QString, QIODevice*>& payloads);
// Export the validated import fallback, never mutable core cache content or a remote fetch.
[[nodiscard]] domain::Result<QByteArray> offlineProviderConfig(const QByteArray& input);
[[nodiscard]] QString runtimeProviderPath(const QString& kind, const QString& name,
                                          const QString& url, const QString& format,
                                          const QString& behavior);
// Production path: borrowed rewindable owned files, bounded output, no rule Node graph.
[[nodiscard]] domain::CommandResult expandProviderFiles(QIODevice& input,
                                                        const QHash<QString, QIODevice*>& payloads,
                                                        QIODevice& output);
// UI only receives a masked semantic template, never a materialized controller secret.
[[nodiscard]] QString redactedConfig(const QByteArray& semantic);
// Only canonical preflight output (or its hash-verified stored artifact), never raw input.
[[nodiscard]] domain::Result<QByteArray> materializeSession(const PreflightConfig& config,
                                                            const QByteArray& secret);
} // namespace quickclash::infra
