#pragma once
#include "domain/contracts.h"
#include <QFile>
#include <QJsonObject>
#include <functional>

namespace quickclash::infra {
// Fixed public snapshot references; no subscription-controlled resource paths or URLs.
[[nodiscard]] QJsonObject geoResourceManifest(const QByteArray& semantic,
                                              const QJsonObject& snapshot = {});
// Verify bundled/selected bytes and reuse matching files, otherwise copy into the owned core home.
[[nodiscard]] domain::CommandResult
prepareGeoResources(const QByteArray& semantic, const QString& directory,
                    const QJsonObject& snapshot = {}, const QString& storageRoot = {},
                    const std::function<bool(QFile&)>& openSource = {});
} // namespace quickclash::infra
