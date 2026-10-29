#pragma once
#include "infra/storage/json_store.h"
#include <QDateTime>
#include <QList>

namespace quickclash::infra {
struct StorageFile {
    QString reference;
    qint64 bytes = 0;
    QDateTime modified;
};
struct StorageInventory {
    QList<StorageFile> files;
    qint64 total = 0;
    qint64 cache = 0;
    qint64 logs = 0;
    QString fingerprint;
    [[nodiscard]] QJsonObject summary() const;
};
// Total budget is advisory: referenced/user data is never sacrificed to satisfy it.
inline constexpr qint64 kStorageBudget = 1024LL * 1024 * 1024;
inline constexpr qint64 kCacheBudget = 64LL * 1024 * 1024;
inline constexpr qint64 kLogBudget = 15LL * 1024 * 1024;
[[nodiscard]] domain::Result<StorageInventory> inspectStorage(JsonStore& store,
                                                              bool hashContents = false);
// Only called while all client writers are idle. Never touches data/, config/ or recovery/.
[[nodiscard]] domain::Result<QJsonObject> trimStorage(JsonStore& store,
                                                      const StorageInventory& inventory);
} // namespace quickclash::infra
