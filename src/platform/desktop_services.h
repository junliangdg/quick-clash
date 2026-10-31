#pragma once
#include "application/desktop_process.h"
#include "application/desktop_services.h"
#include <memory>

namespace quickclash::infra {
class JsonStore;
}
namespace quickclash::platform {
class ProxySettings;
// CMake selects exactly one platform implementation. Returned objects have one owner
// and are created/destroyed on the caller's worker thread.
[[nodiscard]] std::unique_ptr<application::ICredentialStore>
makeCredentialStore(infra::JsonStore& store);
[[nodiscard]] std::unique_ptr<application::IDesktopProcess> makeDesktopProcess();
[[nodiscard]] std::unique_ptr<application::ISystemProxyLease>
makeSystemProxyLease(infra::JsonStore& store, ProxySettings* settings = nullptr);
[[nodiscard]] std::unique_ptr<application::IDesktopResources> makeDesktopResources();
[[nodiscard]] application::DesktopPlatformInfo desktopPlatformInfo();
[[nodiscard]] std::unique_ptr<application::IServiceSetup> makeServiceSetup();
} // namespace quickclash::platform
