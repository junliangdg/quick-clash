#pragma once
#include "application/desktop_services.h"
#include "infra/storage/json_store.h"
#include "platform/proxy_settings.h"

namespace quickclash::platform {
// Current-user LAN/WinINet scope only. Never changes WinHTTP or another user's settings.
class WindowsProxySettings final : public ProxySettings {
  public:
    [[nodiscard]] domain::Result<QJsonObject> read() override;
    [[nodiscard]] domain::CommandResult write(const QJsonObject& snapshot) override;
};
class ProxyLease final : public application::ISystemProxyLease {
  public:
    ProxyLease(infra::JsonStore& store, ProxySettings& settings);
    explicit ProxyLease(infra::JsonStore& store);
    [[nodiscard]] domain::CommandResult acquire(quint16 port, const QString& owner) override;
    [[nodiscard]] domain::CommandResult release() override;
    [[nodiscard]] domain::CommandResult reapply(quint16 port, const QString& owner) override;
    [[nodiscard]] domain::CommandResult suspend() override;
    [[nodiscard]] domain::Result<bool> matches() override;
    // Read-only recovery may restore the OS but cannot durably mark the lease Released.
    [[nodiscard]] domain::Result<bool> safeToArchive() override;
    [[nodiscard]] bool active() const override {
        return active_;
    }
    [[nodiscard]] bool conflict() const override {
        return conflict_;
    }

  private:
    infra::JsonStore& store_;
    WindowsProxySettings defaultSettings_;
    ProxySettings& settings_;
    bool active_ = false;
    bool conflict_ = false;
};
} // namespace quickclash::platform
