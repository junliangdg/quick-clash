#pragma once
#include "application/desktop_process.h"
#include "application/ports.h"
#include <QJsonObject>
#include <functional>
#include <memory>

namespace quickclash::application {
class ISystemProxyLease {
  public:
    virtual ~ISystemProxyLease() = default;
    [[nodiscard]] virtual domain::CommandResult acquire(quint16 port, const QString& owner) = 0;
    [[nodiscard]] virtual domain::CommandResult release() = 0;
    [[nodiscard]] virtual domain::CommandResult reapply(quint16 port, const QString& owner) = 0;
    [[nodiscard]] virtual domain::CommandResult suspend() = 0;
    [[nodiscard]] virtual domain::Result<bool> matches() = 0;
    [[nodiscard]] virtual domain::Result<bool> safeToArchive() = 0;
    [[nodiscard]] virtual bool active() const = 0;
    [[nodiscard]] virtual bool conflict() const = 0;
};
class IDesktopResources {
  public:
    virtual ~IDesktopResources() = default;
    [[nodiscard]] virtual QJsonObject host() const = 0;
    [[nodiscard]] virtual QJsonObject service() const = 0;
};
struct DesktopPlatformInfo {
    QString coreFileName;
    QByteArray coreSha256;
    // Stable names and Supported/RequiresAuthorization/Unsupported values for UI projection.
    QJsonObject capabilities;
};
// The administrator actions the GUI can request for TUN. Install copies a reviewed
// package into the fixed installation and registers the service; Authorize extends trust
// to the folder the ordinary user actually launched from, starting the service if needed.
enum class ServiceSetupMode { Install, Update, Authorize };
class IServiceSetup {
  public:
    virtual ~IServiceSetup() = default;
    // Starts the elevated helper. Exactly one of the callbacks runs on the owning thread.
    virtual void start(ServiceSetupMode mode, const QString& clientRoot,
                       std::function<void(int exitCode)> completed,
                       std::function<void(const QString& error)> failed) = 0;
    [[nodiscard]] virtual QString logPath() const = 0;
};
struct DesktopServices {
    std::unique_ptr<ICredentialStore> credentials;
    DesktopPlatformInfo platformInfo;
    std::unique_ptr<IDesktopResources> resources;
    std::unique_ptr<ISystemProxyLease> proxyLease;
    std::unique_ptr<IDesktopProcess> validator;
    std::unique_ptr<IServiceSetup> serviceSetup;
};
} // namespace quickclash::application
