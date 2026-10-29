#include "infra/storage/storage_maintenance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace quickclash::infra {
QJsonObject StorageInventory::summary() const {
    return {{"totalBytes", QString::number(total)},
            {"cacheBytes", QString::number(cache)},
            {"logBytes", QString::number(logs)},
            {"fileCount", files.size()},
            {"totalBudget", QString::number(kStorageBudget)},
            {"cacheBudget", QString::number(kCacheBudget)},
            {"logBudget", QString::number(kLogBudget)},
            {"overBudget", total > kStorageBudget || cache > kCacheBudget || logs > kLogBudget}};
}
domain::Result<StorageInventory> inspectStorage(JsonStore& store, bool hashContents) {
    StorageInventory result;
    qsizetype entries = 0;
    QDirIterator iterator(
        store.root(), QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        if (++entries > 20000)
            return storageError("RepositoryFileLimitExceeded");
        const auto info = iterator.fileInfo();
        const auto reference = QDir(store.root()).relativeFilePath(info.absoluteFilePath());
        if (std::holds_alternative<domain::Error>(store.path(reference)) ||
            (!info.isDir() && !info.isFile()))
            return storageError("UnsafeDataPath");
        if (info.isDir() || reference == QStringLiteral("writer.lock"))
            continue;
        if (result.files.size() >= 10000 || info.size() < 0 ||
            info.size() > 8LL * kStorageBudget - result.total)
            return storageError("RepositoryFileLimitExceeded");
        result.files.append({reference, info.size(), info.lastModified()});
        result.total += info.size();
        if (reference.startsWith(QStringLiteral("cache/")))
            result.cache += info.size();
        if (reference.startsWith(QStringLiteral("logs/")))
            result.logs += info.size();
    }
    std::sort(result.files.begin(), result.files.end(),
              [](const auto& a, const auto& b) { return a.reference < b.reference; });
    QCryptographicHash signature(QCryptographicHash::Sha256);
    for (const auto& file : result.files) {
        signature.addData(file.reference.toUtf8());
        signature.addData(QByteArray(1, '\0'));
        signature.addData(QByteArray::number(file.bytes));
        signature.addData(QByteArray(1, '\0'));
        signature.addData(QByteArray::number(file.modified.toMSecsSinceEpoch()));
        if (hashContents) {
            const auto path = store.path(file.reference);
            if (!std::holds_alternative<QString>(path))
                return storageError("UnsafeDataPath");
            QFile input(std::get<QString>(path));
            if (!input.open(QIODevice::ReadOnly))
                return storageError("DataReadFailed");
            qint64 read = 0;
            while (!input.atEnd()) {
                const auto chunk = input.read(64 * 1024);
                if (chunk.isEmpty() || read + chunk.size() > file.bytes)
                    return storageError("StoragePreviewStale");
                signature.addData(chunk);
                read += chunk.size();
            }
            if (input.error() != QFileDevice::NoError || read != file.bytes)
                return storageError("DataReadFailed");
        }
    }
    result.fingerprint = QString::fromLatin1(signature.result().toHex());
    return result;
}
domain::Result<QJsonObject> trimStorage(JsonStore& store, const StorageInventory& inventory) {
    if (store.readOnly())
        return storageError("RepositoryReadOnly");
    const auto current = inspectStorage(store);
    if (const auto* failure = std::get_if<domain::Error>(&current))
        return *failure;
    if (std::get<StorageInventory>(current).fingerprint != inventory.fingerprint)
        return storageError("StoragePreviewStale");
    auto ordered = inventory.files;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.modified == b.modified ? a.reference < b.reference : a.modified < b.modified;
    });
    qint64 total = inventory.total, cache = inventory.cache, logs = inventory.logs, reclaimed = 0;
    int removed = 0;
    for (const auto& file : ordered) {
        const auto& ref = file.reference;
        // Fixed client staging namespaces; core-owned caches and backups are never candidates.
        const bool disposableCache = ref.startsWith("cache/downloads/") ||
                                     ref.startsWith("cache/config_stage/") ||
                                     ref.startsWith("cache/geo/");
        const bool disposableLog = ref.startsWith("logs/support_") ||
                                   ref == "logs/diagnostics.json" ||
                                   ref == "logs/redacted_logs.json";
        if ((!disposableCache || (cache <= kCacheBudget && total <= kStorageBudget)) &&
            (!disposableLog || (logs <= kLogBudget && total <= kStorageBudget)))
            continue;
        const auto result = store.remove(ref);
        if (const auto* failure = std::get_if<domain::Error>(&result))
            return *failure;
        total -= file.bytes;
        if (disposableCache)
            cache -= file.bytes;
        if (disposableLog)
            logs -= file.bytes;
        reclaimed += file.bytes;
        ++removed;
    }
    return QJsonObject{{"removedFiles", removed},
                       {"reclaimedBytes", QString::number(reclaimed)},
                       {"budgetSatisfied",
                        total <= kStorageBudget && cache <= kCacheBudget && logs <= kLogBudget}};
}
} // namespace quickclash::infra
