#include "application/desktop_runtime.h"
#include "platform/desktop_services.h"
#include <utility>

namespace quickclash::application {
// Desktop composition lives outside application policy. Every adapter is created on
// the storage worker; destruction occurs after DesktopRuntime has stopped its session.
DesktopRuntime::DesktopRuntime(infra::JsonStore& store, QObject* parent,
                               platform::ProxySettings* proxySettings,
                               std::function<QDateTime()> clock, const QUrl& geoSourceBase)
    : DesktopRuntime(store,
                     {platform::makeCredentialStore(store), platform::desktopPlatformInfo(),
                      platform::makeDesktopResources(),
                      platform::makeSystemProxyLease(store, proxySettings),
                      platform::makeDesktopProcess()},
                     parent, std::move(clock), geoSourceBase) {}
} // namespace quickclash::application
