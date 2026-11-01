#pragma once
#include <QJsonObject>
#include <QString>

namespace quickclash::platform {
// Catalog is compiled into both GUI and service. Disk metadata never authorizes execution.
// Default selects the candidate. Empty/"bundled" preserve legacy package selections.
[[nodiscard]] QJsonObject coreArtifact(const QString& id = QStringLiteral("candidate"));
[[nodiscard]] QString bundledCorePath(const QString& applicationDirectory);
[[nodiscard]] QString managedCorePath(const QString& bundledPath, const QString& id);
[[nodiscard]] bool verifyCoreArtifact(const QString& path, const QString& id);
} // namespace quickclash::platform
