#include "presentation/critical_alerts.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    using quickclash::presentation::CriticalAlerts;
    using Kind = CriticalAlerts::Kind;
    try {
        CriticalAlerts policy;
        QJsonObject profile{
            {"id", "one"},
            {"lastUpdateResult", "Failed"},
            {"usageStatus", QJsonObject{{"quotaState", "Low"}, {"expiryState", "Soon"}}}};
        QJsonObject runtime{
            {"proxyConflict", true}, {"tunState", "Uncertain"}, {"error", "CoreRecoveryExhausted"}};
        check(policy.update(runtime, {profile}, false).isEmpty(), "foreground is quiet");
        check(policy.update(runtime, {profile}, true) ==
                  QList<Kind>{Kind::TunCleanup, Kind::RecoveryStopped, Kind::ProxyConflict,
                              Kind::UpdateFailed, Kind::QuotaLow, Kind::SubscriptionExpiring},
              "background delivery coalesces current conditions in priority order");
        check(policy.update(runtime, {profile}, true).isEmpty(), "unchanged states deduplicated");
        profile.insert("lastUpdateResult", "Updating");
        check(policy.update(runtime, {profile}, true).isEmpty(), "retry remains the same failure");
        profile.insert("lastUpdateResult", "Failed");
        check(policy.update(runtime, {profile}, true).isEmpty(), "failed retry is quiet");
        profile.insert("usageStatus",
                       QJsonObject{{"quotaState", "Exhausted"}, {"expiryState", "Expired"}});
        check(policy.update(runtime, {profile}, true) ==
                  QList<Kind>{Kind::SubscriptionExpired, Kind::QuotaExhausted},
              "exhaustion and expiry escalate once");
        profile.insert("usageStatus", QJsonObject{{"quotaState", "Low"}});
        check(policy.update(runtime, {profile}, true).isEmpty(), "quota downgrade is quiet");
        profile.insert("usageStatus", QJsonObject{{"quotaState", "Exhausted"}});
        check(policy.update(runtime, {profile}, true).isEmpty(), "oscillation does not spam");
        profile.insert("lastUpdateResult", "Updated");
        profile.insert("usageStatus", QJsonObject{{"quotaState", "Available"}});
        check(policy.update({}, {profile}, true).isEmpty(), "resolved conditions removed");
        profile.insert("lastUpdateResult", "Failed");
        check(policy.update({}, {profile}, true) == QList<Kind>{Kind::UpdateFailed},
              "a new failure after success re-arms notification");
        auto second = profile;
        second.insert("id", "two");
        check(policy.update({}, {profile, second}, true) == QList<Kind>{Kind::UpdateFailed},
              "new affected subscriptions still notify using generic bounded text");
        (void)policy.update({}, {}, false);
        check(policy.update(runtime, {}, true) ==
                  QList<Kind>{Kind::TunCleanup, Kind::RecoveryStopped, Kind::ProxyConflict},
              "removed profiles cannot leave stale notification keys");
        QJsonArray many;
        for (int i = 0; i < 150; ++i) {
            auto item = profile;
            item.insert("id", QString::number(i));
            many.append(item);
        }
        check(policy.update({}, many, true) == QList<Kind>{Kind::UpdateFailed} &&
                  policy.update({}, many, true).isEmpty(),
              "many profiles coalesce without repeat delivery");
        std::cout << "Critical alert deduplication, escalation and foreground policy passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
