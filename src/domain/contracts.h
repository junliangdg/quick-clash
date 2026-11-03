#pragma once
#include <QByteArray>
#include <QString>
#include <QUuid>
#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace quickclash::domain {
using Revision = std::uint64_t;
using Generation = std::uint64_t;
using Sequence = std::uint64_t;
using MonotonicTime = std::chrono::steady_clock::time_point;
enum class ErrorCategory {
    Input,
    Network,
    Auth,
    Core,
    Platform,
    Conflict,
    Storage,
    Integrity,
    Cancelled,
    Uncertain
};
struct ConfigIssue {
    QString code;
    QString path;
    QString feature;
    QString phase;
    int occurrences = 1;
};
struct Error {
    QString code;
    ErrorCategory category = ErrorCategory::Input;
    QString userMessageKey;
    bool retryable = false;
    QUuid operationId;
    QString stage;
    QString redactedDetail;
    std::optional<std::int64_t> osCode;
    std::vector<ConfigIssue> configIssues;
};
template <typename T> using Result = std::variant<T, Error>;
using CommandResult = Result<std::monostate>;
enum class ProxyMode { Rule, Global, Direct };
enum class Capability { Supported, Unsupported, RequiresAuthorization, TemporarilyUnavailable };
enum class Lifecycle {
    Stopped,
    Validating,
    Starting,
    Ready,
    Reloading,
    Recovering,
    Stopping,
    Failed
};
enum class Certainty { Confirmed, Uncertain, Unknown };
enum class Ownership { None, Owned, ServiceOwned };
enum class StopReason { UserStop, AppExit, SuspendForMutation };
struct ConfigSelection {
    Revision selectedRevision = 0;
    QString preferenceSnapshotRef;
    QString resourceManifestRef;
    Generation generation = 0;
};
struct ConfigHistory {
    std::optional<Revision> activeRevision;
    std::optional<Revision> lastGoodRevision;
};
struct RuntimeConfig {
    Revision revision = 0;
    QUuid profileId;
    QString effectiveHash;
    QString artifactRef;
    QString resourceManifestRef;
    QString preferenceSnapshotRef;
};
struct SessionConfig {
    RuntimeConfig semantic;
    QUuid epoch;
    QString launchConfigRef;
    QString materializedHash;
};
struct RuntimePreferences {
    ProxyMode mode = ProxyMode::Rule;
    bool tunDesired = false;
    bool systemProxyDesired = false;
    std::uint16_t mixedPort = 7890;
    std::uint16_t controllerPort = 9090;
    Generation version = 0;
    QString dnsMode = QStringLiteral("profile");
};
struct EngineStatus {
    QUuid epoch;
    Lifecycle lifecycle = Lifecycle::Stopped;
    Ownership ownership = Ownership::None;
    QString version;
    std::optional<Revision> configRevision;
    Certainty configCertainty = Certainty::Unknown;
    bool tunObserved = false;
    std::optional<Error> lastError;
};
struct RequestContext {
    QUuid epoch;
    Revision expectedRevision = 0;
    Generation generation = 0;
    [[nodiscard]] bool matches(const RequestContext& current) const noexcept {
        return epoch == current.epoch && expectedRevision == current.expectedRevision &&
               generation == current.generation;
    }
};
struct SubscriptionContext {
    QUuid subscriptionId;
    Generation sourceGeneration = 0;
    QString sourceHash;
    [[nodiscard]] bool matches(const SubscriptionContext& current) const noexcept {
        return subscriptionId == current.subscriptionId &&
               sourceGeneration == current.sourceGeneration && sourceHash == current.sourceHash;
    }
};
struct ProxyGroup {
    QString name;
    QString type;
    std::vector<QString> members;
    QString current;
    bool selectable = false;
};
struct TelemetryDemand {
    QUuid token;
    bool traffic = false;
    bool connections = false;
    bool logs = false;
    std::chrono::milliseconds interval{1000};
};
[[nodiscard]] inline QString formatBytes(std::uint64_t bytes) {
    return QString::number(bytes);
}
} // namespace quickclash::domain
