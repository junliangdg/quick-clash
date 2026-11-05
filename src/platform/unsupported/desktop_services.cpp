#include "platform/desktop_services.h"
#include "platform/proxy_settings.h"

namespace quickclash::platform {
namespace {
domain::Error unsupported(const char* code) {
    domain::Error error;
    error.code = QString::fromLatin1(code);
    error.category = domain::ErrorCategory::Platform;
    return error;
}
class Process final : public application::IDesktopProcess {
  public:
    domain::CommandResult start(const QString&, const QStringList&, const QString&, bool) override {
        return unsupported("CorePlatformUnsupported");
    }
    domain::CommandResult stop() override {
        return std::monostate{};
    }
    bool running() const override {
        return false;
    }
    quint32 pid() const override {
        return 0;
    }
    QJsonObject memory() const override {
        return {};
    }
    bool ownsListener(quint16) const override {
        return false;
    }
};
class Lease final : public application::ISystemProxyLease {
  public:
    domain::CommandResult acquire(quint16, const QString&) override {
        return unsupported("SystemProxyUnsupported");
    }
    domain::CommandResult release() override {
        return unsupported("SystemProxyUnsupported");
    }
    domain::CommandResult reapply(quint16, const QString&) override {
        return unsupported("SystemProxyUnsupported");
    }
    domain::CommandResult suspend() override {
        return unsupported("SystemProxyUnsupported");
    }
    domain::Result<bool> matches() override {
        return unsupported("SystemProxyUnsupported");
    }
    domain::Result<bool> safeToArchive() override {
        return unsupported("SystemProxyUnsupported");
    }
    bool active() const override {
        return false;
    }
    bool conflict() const override {
        return false;
    }
};
class Resources final : public application::IDesktopResources {
  public:
    QJsonObject host() const override {
        return {};
    }
    QJsonObject service() const override {
        return {{QStringLiteral("complete"), false}};
    }
};
} // namespace
std::unique_ptr<application::IDesktopProcess> makeDesktopProcess() {
    return std::make_unique<Process>();
}
std::unique_ptr<application::ISystemProxyLease> makeSystemProxyLease(infra::JsonStore&,
                                                                     ProxySettings*) {
    return std::make_unique<Lease>();
}
std::unique_ptr<application::IDesktopResources> makeDesktopResources() {
    return std::make_unique<Resources>();
}
application::DesktopPlatformInfo desktopPlatformInfo() {
    return {QStringLiteral("mihomo"),
            {},
            {{QStringLiteral("core"), QStringLiteral("Unsupported")},
             {QStringLiteral("credentials"), QStringLiteral("Unsupported")},
             {QStringLiteral("systemProxy"), QStringLiteral("Unsupported")},
             {QStringLiteral("tun"), QStringLiteral("Unsupported")},
             {QStringLiteral("resources"), QStringLiteral("Unsupported")}}};
}
} // namespace quickclash::platform
