#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>

namespace quickclash::presentation {
// Event-driven policy: no timers, page demands, names, URLs or raw errors in notifications.
class CriticalAlerts final {
  public:
    enum class Kind {
        TunCleanup,
        RecoveryStopped,
        ProxyConflict,
        SubscriptionExpired,
        QuotaExhausted,
        UpdateFailed,
        QuotaLow,
        SubscriptionExpiring
    };
    // Foreground observations clear resolved conditions but defer delivery until backgrounded.
    [[nodiscard]] QList<Kind> update(const QJsonObject& runtime, const QJsonArray& profiles,
                                     bool deliver);

  private:
    QHash<QString, int> delivered_;
    QSet<QString> failedUpdates_;
};
} // namespace quickclash::presentation
