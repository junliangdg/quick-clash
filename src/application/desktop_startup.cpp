#include "application/desktop_runtime.h"
#include "platform/startup.h"
#include <QCoreApplication>

namespace quickclash::application {
void DesktopRuntime::refreshStartup() {
    const auto saved = store_.read(QStringLiteral("config/startup.json"));
    if (const auto* failure = std::get_if<domain::Error>(&saved)) {
        error(failure->code);
        return;
    }
    const auto preferences = std::get<std::optional<QJsonObject>>(saved).value_or(QJsonObject{});
    startupState_ = platform::StartupRegistration(QCoreApplication::applicationFilePath()).query();
    startupState_.insert(QStringLiteral("requested"),
                         preferences.value(QStringLiteral("enabled")).toBool());
    startupState_.insert(QStringLiteral("hidden"),
                         preferences.value(QStringLiteral("hidden")).toBool(true));
}
void DesktopRuntime::configureStartup(const QString& command, const QStringList& args) {
    if (store_.readOnly() ||
        (command != QStringLiteral("startup_refresh") && args.value(0) != QStringLiteral("true") &&
         args.value(0) != QStringLiteral("false"))) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    refreshStartup();
    if (command == QStringLiteral("startup_refresh")) {
        publish();
        return;
    }
    const bool enabled = command == QStringLiteral("startup_enabled")
                             ? args.value(0) == QStringLiteral("true")
                             : startupState_.value(QStringLiteral("requested")).toBool();
    const bool hidden = command == QStringLiteral("startup_hidden")
                            ? args.value(0) == QStringLiteral("true")
                            : startupState_.value(QStringLiteral("hidden")).toBool();
    platform::StartupRegistration registration(QCoreApplication::applicationFilePath());
    if (command == QStringLiteral("startup_enabled")) {
        const auto changed = registration.setEnabled(enabled);
        if (const auto* failure = std::get_if<domain::Error>(&changed)) {
            const auto reverted = registration.restorePrevious();
            if (std::holds_alternative<domain::Error>(reverted))
                qWarning("Startup registration rollback failed");
            refreshStartup();
            error(failure->code);
            return;
        }
    }
    const auto saved =
        store_.write(QStringLiteral("config/startup.json"),
                     {{QStringLiteral("enabled"), enabled}, {QStringLiteral("hidden"), hidden}});
    if (const auto* failure = std::get_if<domain::Error>(&saved)) {
        if (command == QStringLiteral("startup_enabled")) {
            const auto reverted = registration.restorePrevious();
            if (std::holds_alternative<domain::Error>(reverted))
                qWarning("Startup registration rollback failed");
        }
        refreshStartup();
        error(failure->code);
        return;
    }
    lastError_.clear();
    refreshStartup();
    publish();
}
} // namespace quickclash::application
