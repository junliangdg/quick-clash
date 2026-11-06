#include "desktop_proxy_probe.h"
#include "application_host.h"
#ifdef Q_OS_WIN
#include "platform/windows/system_proxy.h"
#endif
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>
#include <cstdio>
#include <functional>

namespace quickclash::test {
int desktopExternalProxyMutation() {
#ifdef Q_OS_WIN
    platform::WindowsProxySettings settings;
    const auto current = settings.read();
    if (!std::holds_alternative<QJsonObject>(current))
        return 1;
    auto external = std::get<QJsonObject>(current);
    const auto digest =
        QCryptographicHash::hash(QJsonDocument(external).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex();
    if (!QCoreApplication::arguments().contains(QStringLiteral("--expected-proxy=") +
                                                QString::fromLatin1(digest)))
        return 2;
    external.insert(QStringLiteral("bypass"), external.value(QStringLiteral("bypass")).toString() +
                                                  QStringLiteral(";quickclash-probe.invalid"));
    return std::holds_alternative<std::monostate>(settings.write(external)) ? 0 : 3;
#else
    return 77;
#endif
}
#ifdef Q_OS_WIN
namespace {
bool waitFor(const std::function<bool()>& condition, int milliseconds = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds)
        QTest::qWait(10);
    return condition();
}
QString option(const char* name) {
    const auto prefix = QString::fromLatin1(name) + QLatin1Char('=');
    for (const auto& argument : QCoreApplication::arguments())
        if (argument.startsWith(prefix))
            return argument.mid(prefix.size());
    return {};
}
QJsonObject load(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                          : QJsonObject{};
}
bool clickProxySwitch(ApplicationHost& host) {
    if (!host.state().navigate(QStringLiteral("home")) || !host.window() ||
        !QTest::qWaitForWindowExposed(host.window(), 5000))
        return false;
    QPointer<QQuickItem> control;
    if (!waitFor([&] {
            control = host.window()->findChild<QQuickItem*>(QStringLiteral("home_system_proxy"));
            return control && control->isVisible() && control->isEnabled();
        }))
        return false;
    QSignalSpy clicked(control, SIGNAL(clicked()));
    control->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(host.window(), Qt::Key_Space);
    return clicked.isValid() && clicked.count() == 1;
}
class ProxyRestore final {
  public:
    ProxyRestore(presentation::UiState& ui, QJsonObject before)
        : ui_(ui), before_(std::move(before)), expected_(before_) {}
    ~ProxyRestore() {
        if (!restored_ && !restore())
            std::fputs("Proxy probe could not restore: preserve later external settings.\n",
                       stderr);
    }
    bool matches(const QJsonObject& expected) {
        const auto current = settings_.read();
        return std::holds_alternative<QJsonObject>(current) &&
               std::get<QJsonObject>(current) == expected;
    }
    void expect(QJsonObject value) {
        expected_ = std::move(value);
    }
    void releaseToHost() {
        restored_ = true;
    }
    bool restore() {
        ui_.runtimeCommand(QStringLiteral("system_proxy"), {QStringLiteral("false")});
        if (!waitFor([&] { return !ui_.runtime().value(QStringLiteral("systemProxy")).toBool(); }))
            return false;
        const auto current = settings_.read();
        if (!std::holds_alternative<QJsonObject>(current))
            return false;
        if (std::get<QJsonObject>(current) == before_) {
            restored_ = true;
            return true;
        }
        if (std::get<QJsonObject>(current) != expected_)
            return false;
        restored_ = std::holds_alternative<std::monostate>(settings_.write(before_));
        return restored_ && matches(before_);
    }

  private:
    presentation::UiState& ui_;
    platform::WindowsProxySettings settings_;
    QJsonObject before_, expected_;
    bool restored_ = false;
};
} // namespace
#endif
bool desktopProxyProbe(ApplicationHost& host, QJsonObject& result) {
#ifdef Q_OS_WIN
    auto& ui = host.state();
    platform::WindowsProxySettings settings;
    const auto before = settings.read();
    if (!std::holds_alternative<QJsonObject>(before))
        return false;
    const auto flowPath = option("--desktop-proxy-flow-fixture");
    const auto fixture = load(flowPath);
    if (!flowPath.isEmpty() && fixture.value("originPort").toInt() <= 0)
        return false;
    ProxyRestore cleanup(ui, std::get<QJsonObject>(before));
    const auto toggle = [&](bool enabled) {
        if (flowPath.isEmpty())
            ui.runtimeCommand(QStringLiteral("system_proxy"),
                              {enabled ? QStringLiteral("true") : QStringLiteral("false")});
        else if (!clickProxySwitch(host))
            return false;
        return waitFor(
            [&] { return ui.runtime().value(QStringLiteral("systemProxy")).toBool() == enabled; });
    };
    const auto corePid = ui.runtime().value(QStringLiteral("corePid"));
    const bool initiallyEnabled = toggle(true);
    const auto applied = settings.read();
    if (initiallyEnabled && std::holds_alternative<QJsonObject>(applied))
        cleanup.expect(std::get<QJsonObject>(applied));
    if (!initiallyEnabled)
        return false;
    if (!std::holds_alternative<QJsonObject>(applied))
        return false;
    auto external = std::get<QJsonObject>(applied);
    external.insert(QStringLiteral("bypass"), external.value(QStringLiteral("bypass")).toString() +
                                                  QStringLiteral(";quickclash-probe.invalid"));
    // A separate process writes the real current-user WinINet scope outside
    // the runtime's lease. Keep the proxy endpoint usable throughout.
    if (!cleanup.matches(std::get<QJsonObject>(applied)))
        return false;
    cleanup.expect(external);
    QProcess externalWriter;
    const auto digest =
        QCryptographicHash::hash(
            QJsonDocument(std::get<QJsonObject>(applied)).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256)
            .toHex();
    externalWriter.start(QCoreApplication::applicationFilePath(),
                         {QStringLiteral("--desktop-proxy-external"),
                          QStringLiteral("--expected-proxy=") + QString::fromLatin1(digest)});
    if (!waitFor([&] { return externalWriter.state() == QProcess::NotRunning; }) ||
        externalWriter.exitStatus() != QProcess::NormalExit || externalWriter.exitCode() != 0)
        return false;
    ui.runtimeCommand(QStringLiteral("reconcile"));
    if (!waitFor([&] {
            return ui.runtime().value(QStringLiteral("proxyConflict")).toBool() &&
                   !ui.runtime().value(QStringLiteral("systemProxy")).toBool();
        }) ||
        !cleanup.matches(external) || ui.runtime().value(QStringLiteral("corePid")) != corePid ||
        !ui.runtime().value(QStringLiteral("ready")).toBool())
        return false;
    result.insert(QStringLiteral("nativeConflictPreserved"), true);
    ui.runtimeCommand(QStringLiteral("system_proxy"), {QStringLiteral("false")});
    ui.runtimeCommand(QStringLiteral("reconcile"));
    QTest::qWait(1700);
    if (!cleanup.matches(external))
        return false;
    const bool explicitlyEnabled = toggle(true);
    const auto reapplied = settings.read();
    if (explicitlyEnabled && std::holds_alternative<QJsonObject>(reapplied))
        cleanup.expect(std::get<QJsonObject>(reapplied));
    if (!explicitlyEnabled ||
        !waitFor([&] { return !ui.runtime().value(QStringLiteral("proxyConflict")).toBool(); }))
        return false;
    if (!std::holds_alternative<QJsonObject>(reapplied))
        return false;
    if (!flowPath.isEmpty()) {
        const auto beforeTargets = load(flowPath).value("targets").toArray().size();
        const auto beforeRequests = load(flowPath).value("requests").toArray().size();
        QProcess native;
        native.start(
            QCoreApplication::applicationFilePath(),
            {QStringLiteral("--desktop-flow-native=%1").arg(fixture.value("originPort").toInt())});
        const bool completed =
            waitFor([&] { return native.state() == QProcess::NotRunning; }, 15000);
        if (!completed) {
            native.kill();
            native.waitForFinished(2000);
        }
        const auto authority = QStringLiteral("fixture.quickclash.invalid:%1")
                                   .arg(fixture.value("originPort").toInt());
        const bool forwarded = completed && native.exitStatus() == QProcess::NormalExit &&
                               native.exitCode() == 0 && waitFor([&] {
                                   const auto current = load(flowPath);
                                   const auto targets = current.value("targets").toArray();
                                   const auto requests = current.value("requests").toArray();
                                   return targets.size() > beforeTargets &&
                                          targets.contains(authority) &&
                                          requests.size() > beforeRequests &&
                                          requests.contains(QStringLiteral("GET /system-client"));
                               });
        result.insert(QStringLiteral("nativeRebaselineForwarded"), forwarded);
        result.insert(QStringLiteral("nativeRebaselineGuiSwitch"), true);
        if (!forwarded || ui.runtime().value(QStringLiteral("corePid")) != corePid)
            return false;
    }
    if (!toggle(false) || !cleanup.matches(external))
        return false;
    result.insert(QStringLiteral("nativeRebaselineRestoredExternal"), true);
    cleanup.expect(external);
    const bool restored = cleanup.restore();
    result.insert(QStringLiteral("nativeOriginalSnapshotRestored"), restored);
    return restored;
#else
    Q_UNUSED(host)
    Q_UNUSED(result)
    return false;
#endif
}
bool desktopProxyHoldProbe(ApplicationHost& host, QJsonObject& result) {
#ifdef Q_OS_WIN
    auto& ui = host.state();
    platform::WindowsProxySettings settings;
    const auto before = settings.read();
    if (!std::holds_alternative<QJsonObject>(before))
        return false;
    // The isolated store is required to be new, so startup cannot have an old lease.
    const auto leasePath =
        QDir(option("--desktop-root")).filePath(QStringLiteral("data/leases/windows_lan.json"));
    if (QFile::exists(leasePath))
        return false;
    ProxyRestore cleanup(ui, std::get<QJsonObject>(before));
    if (!clickProxySwitch(host) ||
        !waitFor([&] { return ui.runtime().value(QStringLiteral("systemProxy")).toBool(); }))
        return false;
    const auto applied = settings.read();
    if (!std::holds_alternative<QJsonObject>(applied))
        return false;
    cleanup.expect(std::get<QJsonObject>(applied));
    const auto lease = load(leasePath);
    const bool matched =
        lease.value(QStringLiteral("status")) == QStringLiteral("Applied") &&
        lease.value(QStringLiteral("before")).toObject() == std::get<QJsonObject>(before) &&
        lease.value(QStringLiteral("applied")).toObject() == std::get<QJsonObject>(applied);
    if (!matched)
        return false;
    // Normal exit after the hold still uses the application's own lease cleanup.
    result.insert(QStringLiteral("proxyLeaseApplied"), true);
    cleanup.releaseToHost();
    return true;
#else
    Q_UNUSED(host)
    Q_UNUSED(result)
    return false;
#endif
}
bool desktopProxyReopenProbe(ApplicationHost& host, QJsonObject& result) {
#ifdef Q_OS_WIN
    auto& ui = host.state();
    const auto leasePath =
        QDir(option("--desktop-root")).filePath(QStringLiteral("data/leases/windows_lan.json"));
    // The driver records the old lease before killing the whole GUI. The worker
    // initializes independently of the window and may already have acquired a
    // new lease by the time the probe can observe it.
    const auto interrupted = load(option("--desktop-interrupted-lease"));
    const auto oldId = interrupted.value(QStringLiteral("leaseId")).toString();
    const auto before = interrupted.value(QStringLiteral("before")).toObject();
    if (interrupted.value(QStringLiteral("status")) != QStringLiteral("Applied") ||
        oldId.isEmpty() || before.isEmpty() ||
        before == interrupted.value(QStringLiteral("applied")).toObject())
        return false;
    platform::WindowsProxySettings settings;
    bool releasedObserved = false;
    const bool resumed = waitFor(
        [&] {
            const auto lease = load(leasePath);
            const auto observed = settings.read();
            const auto runtime = ui.runtime();
            releasedObserved |=
                lease.value(QStringLiteral("leaseId")).toString() == oldId &&
                lease.value(QStringLiteral("status")) == QStringLiteral("Released") &&
                std::holds_alternative<QJsonObject>(observed) &&
                std::get<QJsonObject>(observed) == before &&
                !runtime.value(QStringLiteral("systemProxy")).toBool();
            return runtime.value(QStringLiteral("ready")).toBool() &&
                   !runtime.value(QStringLiteral("busy")).toBool() &&
                   runtime.value(QStringLiteral("systemProxy")).toBool();
        },
        20000);
    const auto reapplied = settings.read();
    const auto newLease = load(leasePath);
    // A distinct lease with the original snapshot as its before image proves
    // that acquisition read that baseline after the interrupted lease ended,
    // even if the brief Released state was not observed by the UI thread.
    const bool sameBaseline =
        resumed && std::holds_alternative<QJsonObject>(reapplied) &&
        !newLease.value(QStringLiteral("leaseId")).toString().isEmpty() &&
        newLease.value(QStringLiteral("leaseId")).toString() != oldId &&
        newLease.value(QStringLiteral("status")) == QStringLiteral("Applied") &&
        newLease.value(QStringLiteral("before")).toObject() == before &&
        newLease.value(QStringLiteral("applied")).toObject() == std::get<QJsonObject>(reapplied);
    result.insert(QStringLiteral("proxyRestoreReleasedObserved"), releasedObserved);
    result.insert(QStringLiteral("proxyLeaseRestored"), sameBaseline);
    result.insert(QStringLiteral("proxyIntentResumed"), sameBaseline);
    if (!sameBaseline) {
        result.insert(QStringLiteral("proxyRestoreLeaseStatus"),
                      newLease.value(QStringLiteral("status")));
        result.insert(QStringLiteral("proxyRestoreLeaseIdChanged"),
                      newLease.value(QStringLiteral("leaseId")).toString() != oldId);
        result.insert(QStringLiteral("proxyRestoreBaselineMatches"),
                      newLease.value(QStringLiteral("before")).toObject() == before);
        result.insert(QStringLiteral("proxyRestoreObservedReadable"),
                      std::holds_alternative<QJsonObject>(reapplied));
        result.insert(QStringLiteral("proxyRestoreUiActive"),
                      ui.runtime().value(QStringLiteral("systemProxy")).toBool());
        return false;
    }
    ProxyRestore cleanup(ui, before);
    if (std::holds_alternative<QJsonObject>(reapplied))
        cleanup.expect(std::get<QJsonObject>(reapplied));
    if (!clickProxySwitch(host) ||
        !waitFor([&] { return !ui.runtime().value(QStringLiteral("systemProxy")).toBool(); }))
        return false;
    const auto finalLease = load(leasePath);
    const auto finalSettings = settings.read();
    const bool released =
        finalLease.value(QStringLiteral("status")) == QStringLiteral("Released") &&
        finalLease.value(QStringLiteral("leaseId")) == newLease.value(QStringLiteral("leaseId")) &&
        std::holds_alternative<QJsonObject>(finalSettings) &&
        std::get<QJsonObject>(finalSettings) == before;
    result.insert(QStringLiteral("proxyExplicitlyReleased"), released);
    if (released)
        cleanup.releaseToHost();
    return released;
#else
    Q_UNUSED(host)
    Q_UNUSED(result)
    return false;
#endif
}
} // namespace quickclash::test
