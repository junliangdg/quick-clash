#pragma once
#include "infra/storage/json_store.h"
namespace quickclash::infra {
// Call only while the corresponding core is stopped. At most 32 current cache files remain.
[[nodiscard]] domain::CommandResult prepareRuntimeProviderCache(const QByteArray& semantic,
                                                                JsonStore& store,
                                                                const QString& coreReference);
} // namespace quickclash::infra
