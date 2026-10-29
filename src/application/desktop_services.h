#pragma once
#include "application/desktop_process.h"
#include "application/ports.h"
#include <QJsonObject>
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
struct DesktopServices {
    std::unique_ptr<ICredentialStore> credentials;
    DesktopPlatformInfo platformInfo;
    std::unique_ptr<IDesktopResources> resources;
    std::unique_ptr<ISystemProxyLease> proxyLease;
    std::unique_ptr<IDesktopProcess> validator;
};
} // namespace quickclash::application
