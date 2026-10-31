#include "infra/core/core_health.h"

#include <QJsonValue>
#include <algorithm>

namespace quickclash::infra {
namespace {
QString normalizedTunState(const QJsonObject& status) {
    const auto reported = status.value(QStringLiteral("tunState")).toString();
    if (reported == QStringLiteral("On") || reported == QStringLiteral("Missing") ||
        reported == QStringLiteral("Unknown") || reported == QStringLiteral("Off"))
        return reported;
    // Older services only report the boolean. A false value means the adapter could not be
    // observed; it does not prove that the core exited.
    return status.value(QStringLiteral("tun")).toBool() ? QStringLiteral("On")
                                                        : QStringLiteral("Missing");
}

QJsonObject exitDiagnostics(const QJsonObject& status) {
    QJsonObject result;
    const auto code = status.value(QStringLiteral("coreExitCode"));
    if (code.isDouble() && code.toDouble() == code.toInt() && code.toInt() >= 0)
        result.insert(QStringLiteral("coreExitCode"), code.toInt());
    const auto exitedAt = status.value(QStringLiteral("coreExitedAt")).toString();
    if (!exitedAt.isEmpty() && exitedAt.size() <= 64)
        result.insert(QStringLiteral("coreExitedAt"), exitedAt);
    const auto stderrTail = status.value(QStringLiteral("coreStderr")).toString();
    if (!stderrTail.isEmpty())
        result.insert(QStringLiteral("coreStderr"), stderrTail.left(4096));
    const auto stopReason = status.value(QStringLiteral("stopReason")).toString();
    if (!stopReason.isEmpty() && stopReason.size() <= 32)
        result.insert(QStringLiteral("stopReason"), stopReason);
    const auto stopDetail = status.value(QStringLiteral("stopDetail")).toString();
    if (!stopDetail.isEmpty())
        result.insert(QStringLiteral("stopDetail"), stopDetail.left(1024));
    return result;
}
} // namespace

CoreHealth interpretServiceStatus(const domain::Result<QJsonObject>& result) {
    CoreHealth health;
    const auto* value = std::get_if<QJsonObject>(&result);
    if (!value)
        return health;
    if (value->value(QStringLiteral("ready")).toBool()) {
        health.lifecycle = CoreLifecycle::Running;
        health.tunState = normalizedTunState(*value);
    } else if (value->value(QStringLiteral("busy")).toBool()) {
        // A service that is still validating or bringing up its core has not exited; status
        // polling must not tear the session down during that window.
        health.lifecycle = CoreLifecycle::Unknown;
        health.tunState = QStringLiteral("Unknown");
    } else {
        health.lifecycle = CoreLifecycle::Exited;
        health.tunState = QStringLiteral("Off");
        health.exitDiagnostics = exitDiagnostics(*value);
    }
    return health;
}

QString TunObservationTracker::observe(const QString& observed) {
    auto next = observed;
    if (next != QStringLiteral("On") && next != QStringLiteral("Missing") &&
        next != QStringLiteral("Unknown") && next != QStringLiteral("Off"))
        next = QStringLiteral("Unknown");
    if (next == QStringLiteral("On") || next == QStringLiteral("Off")) {
        state_ = next;
        degradedPolls_ = 0;
        return state_;
    }
    if (state_ == QStringLiteral("On")) {
        ++degradedPolls_;
        if (degradedPolls_ < 3)
            return state_;
    }
    state_ = next;
    return state_;
}

void TunObservationTracker::reset() {
    state_ = QStringLiteral("Off");
    degradedPolls_ = 0;
}

} // namespace quickclash::infra
