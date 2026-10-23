#pragma once
#include "domain/contracts.h"
#include "domain/operation.h"

namespace quickclash::application {
// IDs are returned immediately; adapters publish correlated completion events through this sink.
class OperationSink {
  public:
    virtual ~OperationSink() = default;
    virtual void completed(const QUuid& id, domain::OperationState state,
                           const std::optional<domain::Error>& error) = 0;
};
class IProxyApplication {
  public:
    virtual ~IProxyApplication() = default;
    [[nodiscard]] virtual QUuid startCore(domain::Generation selectionGeneration) = 0;
    [[nodiscard]] virtual QUuid startProxy(domain::Generation selectionGeneration) = 0;
    [[nodiscard]] virtual QUuid stopProxy(domain::StopReason reason) = 0;
    [[nodiscard]] virtual QUuid setSystemProxy(bool desired) = 0;
};
class IEngineSession {
  public:
    virtual ~IEngineSession() = default;
    [[nodiscard]] virtual domain::EngineStatus observedStatus() const = 0;
    [[nodiscard]] virtual domain::Capability capability(const QString& name) const = 0;
    [[nodiscard]] virtual QUuid start(const domain::SessionConfig& config) = 0;
    [[nodiscard]] virtual QUuid stop(domain::StopReason reason) = 0;
    [[nodiscard]] virtual QUuid validate(const domain::SessionConfig& config) = 0;
    [[nodiscard]] virtual QUuid apply(const domain::SessionConfig& config,
                                      const domain::RequestContext& context) = 0;
    [[nodiscard]] virtual QUuid selectProxy(const QString& group, const QString& member,
                                            const domain::RequestContext& context) = 0;
    [[nodiscard]] virtual QUuid setMode(domain::ProxyMode mode,
                                        const domain::RequestContext& context) = 0;
    virtual void setTelemetryDemand(const domain::TelemetryDemand& demand) = 0;
};
class IConfigurationRepository {
  public:
    virtual ~IConfigurationRepository() = default;
    [[nodiscard]] virtual domain::Result<std::optional<domain::ConfigSelection>>
    selection() const = 0;
    [[nodiscard]] virtual domain::Result<domain::ConfigHistory> history() const = 0;
    [[nodiscard]] virtual domain::CommandResult
    selectForNextStart(const domain::ConfigSelection& selection,
                       domain::Generation expectedGeneration) = 0;
};
class ICredentialStore {
  public:
    virtual ~ICredentialStore() = default;
    [[nodiscard]] virtual domain::Result<QByteArray> read(const QString& reference) const = 0;
    [[nodiscard]] virtual domain::CommandResult write(const QString& reference,
                                                      const QByteArray& secret) = 0;
    [[nodiscard]] virtual domain::CommandResult remove(const QString& reference) = 0;
};
} // namespace quickclash::application
