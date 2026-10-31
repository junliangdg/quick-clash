#include "platform/desktop_services.h"

namespace quickclash::platform {
namespace {
class ServiceSetup final : public application::IServiceSetup {
  public:
    void start(application::ServiceSetupMode, const QString&, std::function<void(int)>,
               std::function<void(const QString&)> failed) override {
        failed(QStringLiteral("ServicePlatformUnsupported"));
    }
    [[nodiscard]] QString logPath() const override {
        return {};
    }
};
} // namespace

std::unique_ptr<application::IServiceSetup> makeServiceSetup() {
    return std::make_unique<ServiceSetup>();
}

} // namespace quickclash::platform
