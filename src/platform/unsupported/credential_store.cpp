#include "platform/desktop_services.h"

namespace quickclash::platform {
namespace {
domain::Error unsupported(const char* code) {
    domain::Error error;
    error.code = QString::fromLatin1(code);
    error.category = domain::ErrorCategory::Platform;
    return error;
}
class Credentials final : public application::ICredentialStore {
  public:
    domain::Result<QByteArray> read(const QString&) const override {
        return unsupported("CredentialStoreUnsupported");
    }
    domain::CommandResult write(const QString&, const QByteArray&) override {
        return unsupported("CredentialStoreUnsupported");
    }
    domain::CommandResult remove(const QString&) override {
        return unsupported("CredentialStoreUnsupported");
    }
};
} // namespace
std::unique_ptr<application::ICredentialStore> makeCredentialStore(infra::JsonStore&) {
    return std::make_unique<Credentials>();
}
} // namespace quickclash::platform
