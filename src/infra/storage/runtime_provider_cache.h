#pragma once
#include "infra/storage/json_store.h"
namespace quickclash::infra {
// Call only while the corresponding core is stopped. At most 32 current cache files remain.
[[nodiscard]] domain::CommandResult prepareRuntimeProviderCache(const QByteArray& semantic,
                                                                JsonStore& store,
                                                                const QString& coreReference);
// Stage candidate bodies under a fresh child of the live home. Never mutate active caches.
[[nodiscard]] domain::Result<QByteArray>
prepareRuntimeProviderRevision(const QByteArray& semantic, JsonStore& store,
                               const QString& coreReference, const QString& revision,
                               const QString& cacheRevision = {});
// Preserve the last live cache for the next start, only after the core has stopped.
[[nodiscard]] domain::CommandResult preserveRuntimeProviderCache(JsonStore& store,
                                                                 const QString& coreReference,
                                                                 const QString& revision);
// Retain active/compensation caches only. Reject unknown entries before deleting any files.
[[nodiscard]] domain::CommandResult collectRuntimeProviderRevisions(JsonStore& store,
                                                                    const QString& coreReference,
                                                                    const QStringList& retained);
} // namespace quickclash::infra
