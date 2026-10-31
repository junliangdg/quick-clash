#pragma once

#include "domain/contracts.h"

#include <QJsonObject>
#include <QString>

namespace quickclash::infra {

enum class CoreLifecycle { Unknown, Running, Exited };

struct CoreHealth {
    CoreLifecycle lifecycle = CoreLifecycle::Unknown;
    QString tunState = QStringLiteral("Unknown");
    QJsonObject exitDiagnostics;
};

// A status request can time out or report a transient network observation failure without the
// core having exited. Only an explicit ready=false response means the owned core is gone.
[[nodiscard]] CoreHealth interpretServiceStatus(const domain::Result<QJsonObject>& result);

// TUN observation is separate from core health. Debounce sustained loss so a single transient
// adapter or route-table enumeration failure does not immediately surface to the user.
class TunObservationTracker final {
  public:
    [[nodiscard]] QString observe(const QString& observed);
    void reset();
    [[nodiscard]] QString state() const {
        return state_;
    }

  private:
    QString state_ = QStringLiteral("Off");
    int degradedPolls_ = 0;
};

} // namespace quickclash::infra
