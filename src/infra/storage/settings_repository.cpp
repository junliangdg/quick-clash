#include "infra/storage/settings_repository.h"

namespace quickclash::infra {
namespace {
const QString kSettings = QStringLiteral("config/settings.json");
bool validTheme(const QString& theme) {
    return theme == QStringLiteral("system") || theme == QStringLiteral("light") ||
           theme == QStringLiteral("dark");
}
} // namespace
domain::Result<UiPreferences> SettingsRepository::load() {
    const auto result = store_.read(kSettings);
    if (const auto* error = std::get_if<domain::Error>(&result)) {
        return *error;
    }
    const auto object = std::get<std::optional<QJsonObject>>(result);
    if (!object) {
        return UiPreferences{};
    }
    if (!validTheme(object->value(QStringLiteral("theme")).toString()) ||
        !object->value(QStringLiteral("unloadWhenHidden")).isBool() ||
        !object->value(QStringLiteral("closeAcknowledged")).isBool()) {
        store_.freeze();
        return storageError("SettingsRecoveryRequired");
    }
    UiPreferences preferences{object->value(QStringLiteral("theme")).toString(),
                              object->value(QStringLiteral("unloadWhenHidden")).toBool(),
                              object->value(QStringLiteral("closeAcknowledged")).toBool()};
    const auto dashboard = object->value(QStringLiteral("dashboard"));
    if (!dashboard.isUndefined() && !dashboard.isObject()) {
        store_.freeze();
        return storageError("SettingsRecoveryRequired");
    }
    const auto options = dashboard.toObject();
    for (auto it = preferences.dashboard.begin(); it != preferences.dashboard.end(); ++it) {
        const auto value = options.value(it.key());
        if (value.isUndefined())
            continue;
        if (!value.isBool()) {
            store_.freeze();
            return storageError("SettingsRecoveryRequired");
        }
        it.value() = value;
    }
    return preferences;
}
domain::CommandResult SettingsRepository::save(const UiPreferences& preferences) {
    if (!validTheme(preferences.theme)) {
        return storageError("InvalidTheme");
    }
    return store_.write(kSettings,
                        {{QStringLiteral("theme"), preferences.theme},
                         {QStringLiteral("unloadWhenHidden"), preferences.unloadWhenHidden},
                         {QStringLiteral("closeAcknowledged"), preferences.closeAcknowledged},
                         {QStringLiteral("dashboard"), preferences.dashboard}});
}
QJsonObject SettingsRepository::runtimeDefaults() {
    return {{QStringLiteral("mode"), QStringLiteral("rule")},
            {QStringLiteral("tunDesired"), false},
            {QStringLiteral("systemProxyDesired"), false},
            {QStringLiteral("mixedPort"), 7890},
            {QStringLiteral("controllerPort"), 9090}};
}
} // namespace quickclash::infra
