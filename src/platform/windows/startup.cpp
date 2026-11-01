#include "platform/startup.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace quickclash::platform {
StartupRegistration::StartupRegistration(QString executable, QString runKey, QString approvedKey)
    : executable_(QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath())),
      runKey_(runKey.isEmpty()
                  ? QStringLiteral(
                        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run")
                  : std::move(runKey)),
      approvedKey_(approvedKey.isEmpty()
                       ? QStringLiteral("HKEY_CURRENT_"
                                        "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explor"
                                        "er\\StartupApproved\\Run")
                       : std::move(approvedKey)) {}
QJsonObject StartupRegistration::query() const {
    QSettings run(runKey_, QSettings::NativeFormat);
    const auto command = run.value(QStringLiteral("QuickClash")).toString();
    const auto expected = QLatin1Char('"') + executable_ + QStringLiteral("\" --autostart");
    QSettings approval(approvedKey_, QSettings::NativeFormat);
    const auto bytes = approval.value(QStringLiteral("QuickClash")).toByteArray();
    const bool disabled = !bytes.isEmpty() && (static_cast<unsigned char>(bytes[0]) & 1U);
    const bool registered = command.compare(expected, Qt::CaseInsensitive) == 0;
    const auto state = run.status() != QSettings::NoError || approval.status() != QSettings::NoError
                           ? QStringLiteral("Unknown")
                       : command.isEmpty() ? QStringLiteral("Off")
                       : !registered       ? QStringLiteral("PathMismatch")
                       : disabled          ? QStringLiteral("DisabledBySystem")
                                           : QStringLiteral("On");
    return {{QStringLiteral("supported"), true},
            {QStringLiteral("registered"), registered},
            {QStringLiteral("effective"), state == QStringLiteral("On")},
            {QStringLiteral("state"), state}};
}
domain::CommandResult StartupRegistration::setEnabled(bool enabled) {
    if (executable_.contains(QLatin1Char('"')) || !QFileInfo(executable_).isFile()) {
        domain::Error error;
        error.code = QStringLiteral("StartupRegistrationFailed");
        return error;
    }
    QSettings run(runKey_, QSettings::NativeFormat);
    previousCommand_ = run.value(QStringLiteral("QuickClash"));
    if (run.status() != QSettings::NoError) {
        domain::Error error;
        error.code = QStringLiteral("StartupRegistrationFailed");
        return error;
    }
    previousCaptured_ = true;
    if (enabled)
        run.setValue(QStringLiteral("QuickClash"),
                     QLatin1Char('"') + executable_ + QStringLiteral("\" --autostart"));
    else
        run.remove(QStringLiteral("QuickClash"));
    run.sync();
    if (run.status() != QSettings::NoError ||
        query().value(QStringLiteral("registered")).toBool() != enabled) {
        domain::Error error;
        error.code = QStringLiteral("StartupRegistrationFailed");
        return error;
    }
    return std::monostate{};
}
domain::CommandResult StartupRegistration::restorePrevious() {
    if (!previousCaptured_)
        return std::monostate{};
    QSettings run(runKey_, QSettings::NativeFormat);
    if (previousCommand_.isValid())
        run.setValue(QStringLiteral("QuickClash"), previousCommand_);
    else
        run.remove(QStringLiteral("QuickClash"));
    run.sync();
    if (run.status() != QSettings::NoError ||
        run.value(QStringLiteral("QuickClash")) != previousCommand_) {
        domain::Error error;
        error.code = QStringLiteral("StartupRegistrationFailed");
        return error;
    }
    previousCaptured_ = false;
    return std::monostate{};
}
} // namespace quickclash::platform
