#include "platform/core_artifacts.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#ifdef Q_OS_WIN
static void initializeCoreCatalog() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(quickclash_core_catalog);
        return true;
    }();
    Q_UNUSED(initialized)
}
#endif
namespace quickclash::platform {
QJsonObject coreArtifact(const QString& id) {
#ifdef Q_OS_WIN
    initializeCoreCatalog();
    QFile file(QStringLiteral(":/metadata/core_updates.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto catalog = QJsonDocument::fromJson(file.readAll()).object();
    const auto selectedId = id == QStringLiteral("candidate")
                                ? catalog.value(QStringLiteral("candidateId")).toString()
                            : id.isEmpty() || id == QStringLiteral("bundled")
                                ? catalog.value(QStringLiteral("bundledId")).toString()
                                : id;
    const auto artifacts = catalog.value(QStringLiteral("artifacts")).toArray();
    for (const auto& entry : artifacts) {
        const auto artifact = entry.toObject();
        if (artifact.value(QStringLiteral("id")) == selectedId)
            return artifact;
    }
#else
    Q_UNUSED(id)
#endif
    return {};
}
QString bundledCorePath(const QString& applicationDirectory) {
    const QDir directory(applicationDirectory);
    const auto packaged = directory.filePath(QStringLiteral("../core/mihomo.exe"));
    if (QFileInfo::exists(packaged) ||
        QFileInfo::exists(directory.filePath(QStringLiteral("../metadata/release_matrix.json"))))
        return QFileInfo(packaged).absoluteFilePath();
#ifdef QUICKCLASH_DEVELOPMENT_CORE
    return QStringLiteral(QUICKCLASH_DEVELOPMENT_CORE);
#else
    return directory.filePath(QStringLiteral("core/mihomo.exe"));
#endif
}
QString managedCorePath(const QString& bundledPath, const QString& id) {
    if (id.isEmpty() || id == QStringLiteral("bundled"))
        return bundledPath;
    const auto artifact = coreArtifact(id);
    if (artifact.isEmpty())
        return {};
    const auto root = QFileInfo(bundledPath).absolutePath();
    const QFileInfo rootInfo(root);
    if (rootInfo.isSymbolicLink() || rootInfo.isJunction())
        return {};
    const auto path =
        QDir(root).filePath(QStringLiteral("versions/%1/mihomo.exe")
                                .arg(artifact.value(QStringLiteral("id")).toString()));
    // Reject links and junctions in every existing component below the core directory.
    for (auto info = QFileInfo(path); info.absoluteFilePath() != QFileInfo(root).absoluteFilePath();
         info = QFileInfo(info.absolutePath())) {
        if (info.isSymbolicLink() || info.isJunction())
            return {};
        if (info.isRoot())
            return {};
    }
    return path;
}
bool verifyCoreArtifact(const QString& path, const QString& id) {
    const auto artifact = coreArtifact(id);
    QFile file(path);
    if (artifact.isEmpty() || !file.open(QIODevice::ReadOnly) ||
        file.size() != artifact.value(QStringLiteral("executableBytes")).toInteger())
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) &&
           hash.result().toHex() == artifact.value(QStringLiteral("sha256")).toString().toLatin1();
}
} // namespace quickclash::platform
