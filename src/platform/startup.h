#pragma once
#include "domain/contracts.h"
#include <QJsonObject>
#include <QVariant>

namespace quickclash::platform {
// Optional registry locations are for isolated tests; production always uses the defaults.
class StartupRegistration final {
  public:
    explicit StartupRegistration(QString executable, QString runKey = {}, QString approvedKey = {});
    [[nodiscard]] QJsonObject query() const;
    [[nodiscard]] domain::CommandResult setEnabled(bool enabled);
    [[nodiscard]] domain::CommandResult restorePrevious();

  private:
    QString executable_, runKey_, approvedKey_;
    QVariant previousCommand_;
    bool previousCaptured_ = false;
};
} // namespace quickclash::platform
