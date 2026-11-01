#include "platform/startup.h"

namespace quickclash::platform {
StartupRegistration::StartupRegistration(QString executable, QString runKey, QString approvedKey)
    : executable_(std::move(executable)), runKey_(std::move(runKey)),
      approvedKey_(std::move(approvedKey)) {}
QJsonObject StartupRegistration::query() const {
    return {{QStringLiteral("supported"), false},
            {QStringLiteral("effective"), false},
            {QStringLiteral("state"), QStringLiteral("Unsupported")}};
}
domain::CommandResult StartupRegistration::setEnabled(bool) {
    domain::Error error;
    error.code = QStringLiteral("StartupUnsupported");
    return error;
}
domain::CommandResult StartupRegistration::restorePrevious() {
    return std::monostate{};
}
} // namespace quickclash::platform
