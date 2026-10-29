#include "platform/desktop_services.h"
#include "platform/process_memory.h"
#include "platform/windows/system_proxy.h"

namespace quickclash::platform {
namespace {
class DesktopResources final : public application::IDesktopResources {
  public:
    QJsonObject host() const override {
        return currentProcessMemory();
    }
    QJsonObject service() const override {
        return installedServiceMemory();
    }
};
} // namespace
std::unique_ptr<application::ISystemProxyLease> makeSystemProxyLease(infra::JsonStore& store,
                                                                     ProxySettings* settings) {
    if (settings)
        return std::make_unique<ProxyLease>(store, *settings);
    return std::make_unique<ProxyLease>(store);
}
std::unique_ptr<application::IDesktopResources> makeDesktopResources() {
    return std::make_unique<DesktopResources>();
}
application::DesktopPlatformInfo desktopPlatformInfo() {
    return {QStringLiteral("mihomo.exe"),
            QByteArrayLiteral("1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a"),
            {{QStringLiteral("core"), QStringLiteral("Supported")},
             {QStringLiteral("credentials"), QStringLiteral("Supported")},
             {QStringLiteral("systemProxy"), QStringLiteral("Supported")},
             {QStringLiteral("tun"), QStringLiteral("RequiresAuthorization")},
             {QStringLiteral("resources"), QStringLiteral("Supported")}}};
}
} // namespace quickclash::platform
