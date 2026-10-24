#pragma once
#include "domain/contracts.h"
#include <QJsonObject>

namespace quickclash::infra {
// Fixed public snapshot references; no subscription-controlled resource paths or URLs.
[[nodiscard]] QJsonObject geoResourceManifest(const QByteArray& semantic);
// Verify bundled bytes, then atomically copy the same snapshot into an isolated core home.
[[nodiscard]] domain::CommandResult prepareGeoResources(const QByteArray& semantic,
                                                        const QString& directory);
} // namespace quickclash::infra
