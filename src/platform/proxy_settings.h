#pragma once
#include "domain/contracts.h"
#include <QJsonObject>

namespace quickclash::platform {
// Adapter-owned snapshot. The lease implementation determines its scope and format.
class ProxySettings {
  public:
    virtual ~ProxySettings() = default;
    [[nodiscard]] virtual domain::Result<QJsonObject> read() = 0;
    [[nodiscard]] virtual domain::CommandResult write(const QJsonObject& snapshot) = 0;
};
} // namespace quickclash::platform
