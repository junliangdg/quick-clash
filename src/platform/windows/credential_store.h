#pragma once
#include "application/ports.h"
#include "infra/storage/json_store.h"

namespace quickclash::platform {
// Call on the storage worker. References are UUIDs; plaintext never enters metadata.
class CredentialStore final : public application::ICredentialStore {
  public:
    explicit CredentialStore(infra::JsonStore& store) : store_(store) {}
    [[nodiscard]] domain::Result<QByteArray> read(const QString& reference) const override;
    [[nodiscard]] domain::CommandResult write(const QString& reference,
                                              const QByteArray& secret) override;
    [[nodiscard]] domain::CommandResult remove(const QString& reference) override;

  private:
    [[nodiscard]] domain::Result<QString> referencePath(const QString& reference) const;
    infra::JsonStore& store_;
};
} // namespace quickclash::platform
