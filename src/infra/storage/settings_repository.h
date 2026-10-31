#pragma once
#include "infra/storage/json_store.h"

namespace quickclash::infra {
struct UiPreferences {
    QString theme = QStringLiteral("system");
    bool unloadWhenHidden = false;
    bool closeAcknowledged = false;
    QJsonObject dashboard{
        {QStringLiteral("curve"), true},    {QStringLiteral("ip"), true},
        {QStringLiteral("sites"), true},    {QStringLiteral("resources"), false},
        {QStringLiteral("details"), false}, {QStringLiteral("hideAddresses"), false}};
};
class SettingsRepository final {
  public:
    explicit SettingsRepository(JsonStore& store) : store_(store) {}
    [[nodiscard]] domain::Result<UiPreferences> load();
    [[nodiscard]] domain::CommandResult save(const UiPreferences& preferences);
    [[nodiscard]] static QJsonObject runtimeDefaults();

  private:
    JsonStore& store_;
};
} // namespace quickclash::infra
