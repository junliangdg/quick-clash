#pragma once
#include "application/ports.h"
#include "infra/storage/json_store.h"

namespace quickclash::infra {
class ConfigRepository final : public application::IConfigurationRepository {
  public:
    explicit ConfigRepository(JsonStore& store);
    [[nodiscard]] domain::CommandResult load();
    [[nodiscard]] domain::Result<std::optional<domain::ConfigSelection>> selection() const override;
    [[nodiscard]] domain::Result<domain::ConfigHistory> history() const override;
    [[nodiscard]] domain::CommandResult
    selectForNextStart(const domain::ConfigSelection& selection,
                       domain::Generation expectedGeneration) override;
    [[nodiscard]] const QJsonObject& manifest() const {
        return manifest_;
    }
    [[nodiscard]] domain::Generation generation() const;
    [[nodiscard]] bool recoveryRequired() const {
        return !pending_.isEmpty();
    }
    [[nodiscard]] const QJsonObject& pending() const {
        return pending_;
    }
    [[nodiscard]] domain::Result<QUuid> prepare(QJsonObject target, const QString& kind,
                                                bool hadRunningSession);
    [[nodiscard]] domain::CommandResult phase(const QString& stage);
    [[nodiscard]] domain::CommandResult commit();
    // Caller first restores external state. This only concludes the uncommitted journal.
    [[nodiscard]] domain::CommandResult rollback();

  private:
    [[nodiscard]] domain::CommandResult finishJournal(const QString& stage);
    JsonStore& store_;
    QJsonObject manifest_;
    QJsonObject pending_;
    std::optional<domain::Error> error_;
};
} // namespace quickclash::infra
