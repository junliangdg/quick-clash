#include "desktop_proxy_probe.h"
#include "application_host.h"
#ifdef Q_OS_WIN
#include "platform/windows/system_proxy.h"
#endif
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
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
bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 5000)
        QTest::qWait(10);
    return condition();
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
    ProxyRestore cleanup(ui, std::get<QJsonObject>(before));
    const auto corePid = ui.runtime().value(QStringLiteral("corePid"));
    ui.runtimeCommand(QStringLiteral("system_proxy"), {QStringLiteral("true")});
    if (!waitFor([&] { return ui.runtime().value(QStringLiteral("systemProxy")).toBool(); }))
        return false;
    const auto applied = settings.read();
    if (!std::holds_alternative<QJsonObject>(applied))
        return false;
    cleanup.expect(std::get<QJsonObject>(applied));
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
    ui.runtimeCommand(QStringLiteral("system_proxy"), {QStringLiteral("true")});
    if (!waitFor([&] {
            return ui.runtime().value(QStringLiteral("systemProxy")).toBool() &&
                   !ui.runtime().value(QStringLiteral("proxyConflict")).toBool();
        }))
        return false;
    const auto reapplied = settings.read();
    if (!std::holds_alternative<QJsonObject>(reapplied))
        return false;
    cleanup.expect(std::get<QJsonObject>(reapplied));
    ui.runtimeCommand(QStringLiteral("system_proxy"), {QStringLiteral("false")});
    if (!waitFor([&] { return !ui.runtime().value(QStringLiteral("systemProxy")).toBool(); }) ||
        !cleanup.matches(external))
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
} // namespace quickclash::test
