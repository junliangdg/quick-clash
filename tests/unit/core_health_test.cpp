#include "infra/core/core_health.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <iostream>

using quickclash::domain::Error;
using quickclash::infra::CoreLifecycle;
using quickclash::infra::interpretServiceStatus;
using quickclash::infra::TunObservationTracker;

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    using quickclash::infra::classifyRecoveryFailure;
    using quickclash::infra::RecoveryFailure;
    quickclash::infra::TunRecoveryBudget budget;
    if (budget.failed(true, true) || budget.failed(true, true) || !budget.failed(true, true))
        return 12;
    budget.reset();
    if (budget.failed(true, true) || budget.failed(true, false) || budget.failed(true, true) ||
        budget.failed(false, true) || budget.failed(true, true))
        return 13;
    for (const auto* code :
         {"ServiceStartTimeout", "ServiceTunReadbackFailed", "CoreExitedDuringStart"})
        if (classifyRecoveryFailure(QLatin1String(code)) != RecoveryFailure::Retry)
            return 14;
    for (const auto* code : {"ServiceClientUntrusted", "ServiceInstallationRequired",
                             "ServiceConfigRejected", "CoreIntegrityFailed"})
        if (classifyRecoveryFailure(QLatin1String(code)) != RecoveryFailure::Pause)
            return 15;
    if (classifyRecoveryFailure(QStringLiteral("CoreStopTimeout")) !=
        RecoveryFailure::CleanupUncertain)
        return 16;
    const auto crashed = interpretServiceStatus(quickclash::domain::Result<QJsonObject>{QJsonObject{
        {QStringLiteral("ready"), false}, {QStringLiteral("coreExitCode"), qint64{3221225477}}}});
    if (crashed.exitDiagnostics.value(QStringLiteral("coreExitCode")).toInteger() != 3221225477LL)
        return 17;

    // A healthy core whose TUN adapter cannot be observed must stay alive.
    const auto missing = interpretServiceStatus(quickclash::domain::Result<QJsonObject>{
        QJsonObject{{QStringLiteral("ready"), true},
                    {QStringLiteral("tun"), false},
                    {QStringLiteral("tunState"), QStringLiteral("Missing")}}});
    if (missing.lifecycle != CoreLifecycle::Running ||
        missing.tunState != QStringLiteral("Missing"))
        return 1;

    // A service that is still starting its core is not an exited core.
    const auto starting = interpretServiceStatus(quickclash::domain::Result<QJsonObject>{
        QJsonObject{{QStringLiteral("ready"), false},
                    {QStringLiteral("busy"), true},
                    {QStringLiteral("coreRunning"), true}}});
    if (starting.lifecycle != CoreLifecycle::Unknown ||
        starting.tunState != QStringLiteral("Unknown"))
        return 11;

    // Older services report only the boolean; false is a degraded observation, not an exit.
    const auto legacy = interpretServiceStatus(quickclash::domain::Result<QJsonObject>{
        QJsonObject{{QStringLiteral("ready"), true}, {QStringLiteral("tun"), false}}});
    if (legacy.lifecycle != CoreLifecycle::Running || legacy.tunState != QStringLiteral("Missing"))
        return 2;

    // Only ready=false ends the session, and its bounded diagnostics survive classification.
    const auto stopped = interpretServiceStatus(quickclash::domain::Result<QJsonObject>{
        QJsonObject{{QStringLiteral("ready"), false},
                    {QStringLiteral("coreExitCode"), 3},
                    {QStringLiteral("coreExitedAt"), QStringLiteral("2026-10-31T10:00:00Z")},
                    {QStringLiteral("coreStderr"), QStringLiteral("tail")},
                    {QStringLiteral("stopDetail"), QStringLiteral("detail")}}});
    if (stopped.lifecycle != CoreLifecycle::Exited ||
        stopped.exitDiagnostics.value(QStringLiteral("coreExitCode")).toInt() != 3 ||
        stopped.exitDiagnostics.value(QStringLiteral("coreStderr")) != QStringLiteral("tail") ||
        stopped.exitDiagnostics.value(QStringLiteral("stopDetail")) != QStringLiteral("detail"))
        return 3;

    // A timed-out or rejected status request is not a core exit.
    Error timeout;
    timeout.code = QStringLiteral("ServiceTimeout");
    const auto unavailable =
        interpretServiceStatus(quickclash::domain::Result<QJsonObject>{timeout});
    if (unavailable.lifecycle != CoreLifecycle::Unknown ||
        unavailable.tunState != QStringLiteral("Unknown"))
        return 4;

    TunObservationTracker tracker;
    if (tracker.observe(QStringLiteral("On")) != QStringLiteral("On"))
        return 5;
    if (tracker.observe(QStringLiteral("Missing")) != QStringLiteral("On") ||
        tracker.observe(QStringLiteral("Missing")) != QStringLiteral("On"))
        return 6; // Debounce two transient misses.
    if (tracker.observe(QStringLiteral("Missing")) != QStringLiteral("Missing"))
        return 7;
    if (tracker.observe(QStringLiteral("On")) != QStringLiteral("On"))
        return 8;
    if (tracker.observe(QStringLiteral("Unknown")) != QStringLiteral("On") ||
        tracker.observe(QStringLiteral("Unknown")) != QStringLiteral("On") ||
        tracker.observe(QStringLiteral("Unknown")) != QStringLiteral("Unknown"))
        return 9;
    tracker.reset();
    if (tracker.state() != QStringLiteral("Off"))
        return 10;

    std::cout << "PASS: core lifecycle and TUN observation are independent" << std::endl;
    return 0;
}
