#include "presentation/critical_alerts.h"
#include <algorithm>
#include <utility>

namespace quickclash::presentation {
QList<CriticalAlerts::Kind> CriticalAlerts::update(const QJsonObject& runtime,
                                                   const QJsonArray& profiles, bool deliver) {
    struct Condition {
        Kind kind;
        int severity;
    };
    QHash<QString, Condition> active;
    if (runtime.value("proxyConflict").toBool())
        active.insert(QStringLiteral("proxy"), {Kind::ProxyConflict, 1});
    if (runtime.value("tunState") == QStringLiteral("Uncertain"))
        active.insert(QStringLiteral("tun"), {Kind::TunCleanup, 1});
    const auto error = runtime.value("error").toString();
    if (error == QStringLiteral("CoreRecoveryExhausted") ||
        error == QStringLiteral("TunRecoveryExhausted"))
        active.insert(QStringLiteral("recovery"), {Kind::RecoveryStopped, 1});
    QSet<QString> failures;
    for (qsizetype i = 0; i < std::min(profiles.size(), qsizetype{100}); ++i) {
        const auto profile = profiles[i].toObject();
        const auto id = profile.value("id").toString();
        if (id.isEmpty() || id.size() > 128)
            continue;
        const auto result = profile.value("lastUpdateResult").toString();
        // An automatic retry must not re-arm the same failure on every Updating publication.
        if (result == QStringLiteral("Failed") ||
            (result == QStringLiteral("Updating") && failedUpdates_.contains(id))) {
            failures.insert(id);
            active.insert(QStringLiteral("update:") + id, {Kind::UpdateFailed, 1});
        }
        const auto usage = profile.value("usageStatus").toObject();
        const auto quota = usage.value("quotaState").toString();
        if (quota == QStringLiteral("Low") || quota == QStringLiteral("Exhausted"))
            active.insert(QStringLiteral("quota:") + id,
                          {quota == QStringLiteral("Low") ? Kind::QuotaLow : Kind::QuotaExhausted,
                           quota == QStringLiteral("Low") ? 1 : 2});
        const auto expiry = usage.value("expiryState").toString();
        if (expiry == QStringLiteral("Soon") || expiry == QStringLiteral("Expired"))
            active.insert(QStringLiteral("expiry:") + id,
                          {expiry == QStringLiteral("Soon") ? Kind::SubscriptionExpiring
                                                            : Kind::SubscriptionExpired,
                           expiry == QStringLiteral("Soon") ? 1 : 2});
    }
    failedUpdates_ = std::move(failures);
    for (auto it = delivered_.begin(); it != delivered_.end();) {
        if (!active.contains(it.key()))
            it = delivered_.erase(it);
        else
            ++it;
    }
    QList<Kind> alerts;
    if (!deliver)
        return alerts;
    for (auto it = active.cbegin(); it != active.cend(); ++it) {
        if (it->severity > delivered_.value(it.key(), 0) && !alerts.contains(it->kind))
            alerts.append(it->kind);
        // Keep the highest delivered level until fully resolved; downgrades are quiet.
        delivered_.insert(it.key(), std::max(it->severity, delivered_.value(it.key(), 0)));
    }
    std::sort(alerts.begin(), alerts.end());
    return alerts;
}
} // namespace quickclash::presentation
