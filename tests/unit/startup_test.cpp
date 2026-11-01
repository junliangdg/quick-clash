#include "platform/startup.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>
#include <cstdio>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto root = QStringLiteral("HKEY_CURRENT_USER\\Software\\QuickClashTests\\") +
                      QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto runKey = root + QStringLiteral("\\Run");
    const auto approvalKey = root + QStringLiteral("\\Approval");
    quickclash::platform::StartupRegistration startup(app.applicationFilePath(), runKey,
                                                      approvalKey);
    QSettings run(runKey, QSettings::NativeFormat), approval(approvalKey, QSettings::NativeFormat);
    const auto check = [&](bool good, const char* message) {
        if (!good)
            std::fprintf(stderr, "%s\n", message);
        return good;
    };
    bool passed = check(startup.query().value("state") == "Off", "initially off");
    passed &= check(std::holds_alternative<std::monostate>(startup.setEnabled(true)) &&
                        startup.query().value("effective").toBool(),
                    "register and read back");
    approval.setValue("QuickClash", QByteArray::fromHex("0300000000000000"));
    approval.sync();
    passed &= check(startup.query().value("state") == "DisabledBySystem" &&
                        !startup.query().value("effective").toBool(),
                    "system disable is observable");
    run.setValue("QuickClash", "\"C:\\old folder\\quickclash.exe\" --autostart");
    run.sync();
    passed &= check(startup.query().value("state") == "PathMismatch",
                    "moved path requires explicit repair");
    const auto oldCommand = run.value("QuickClash");
    passed &= check(std::holds_alternative<std::monostate>(startup.setEnabled(true)) &&
                        std::holds_alternative<std::monostate>(startup.restorePrevious()) &&
                        run.value("QuickClash") == oldCommand,
                    "persistence compensation preserves the exact stale-path command");
    passed &= check(std::holds_alternative<std::monostate>(startup.setEnabled(true)) &&
                        startup.query().value("registered").toBool(),
                    "repair path without overriding system policy");
    passed &= check(std::holds_alternative<std::monostate>(startup.setEnabled(false)) &&
                        startup.query().value("state") == "Off",
                    "disable removes owned entry");
    QSettings cleanup(root, QSettings::NativeFormat);
    cleanup.remove("");
    cleanup.sync();
    return passed ? 0 : 1;
}
