#pragma once
#include "infra/storage/json_store.h"

namespace quickclash::platform {
// Current-user LAN/WinINet scope only. Never changes WinHTTP or another user's settings.
class ProxySettings {
  public:
    virtual ~ProxySettings() = default;
    [[nodiscard]] virtual domain::Result<QJsonObject> read() = 0;
    [[nodiscard]] virtual domain::CommandResult write(const QJsonObject& snapshot) = 0;
};
class WindowsProxySettings final : public ProxySettings {
  public:
    [[nodiscard]] domain::Result<QJsonObject> read() override;
    [[nodiscard]] domain::CommandResult write(const QJsonObject& snapshot) override;
};
class ProxyLease final {
  public:
    ProxyLease(infra::JsonStore& store, ProxySettings& settings);
    [[nodiscard]] domain::CommandResult acquire(quint16 port, const QString& owner);
    [[nodiscard]] domain::CommandResult release();
    [[nodiscard]] domain::CommandResult reapply(quint16 port, const QString& owner);
    [[nodiscard]] domain::CommandResult suspend();
    [[nodiscard]] domain::Result<bool> matches();
    [[nodiscard]] bool active() const {
        return active_;
    }
    [[nodiscard]] bool conflict() const {
        return conflict_;
    }

  private:
    infra::JsonStore& store_;
    ProxySettings& settings_;
    bool active_ = false;
    bool conflict_ = false;
};
} // namespace quickclash::platform
