#include "infra/storage/runtime_provider_cache.h"
#include "infra/config/config_metadata.h"
#include <QDir>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <yaml-cpp/yaml.h>
namespace quickclash::infra {
domain::CommandResult prepareRuntimeProviderCache(const QByteArray& semantic, JsonStore& store,
                                                  const QString& coreReference) {
    const auto metadata = configMetadata(semantic);
    if (const auto* failure = std::get_if<domain::Error>(&metadata))
        return *failure;
    const auto prefix = coreReference.isEmpty() ? QString{} : coreReference + '/';
    const auto reference = prefix + QStringLiteral("providers");
    const auto resolved = store.path(reference);
    if (const auto* failure = std::get_if<domain::Error>(&resolved))
        return *failure;
    QSet<QString> retained;
    QMap<QString, QByteArray> seeds;
    QSet<QString> immutableFiles;
    qsizetype seedBytes = 0;
    try {
        const auto root = YAML::Load(std::get<ConfigMetadata>(metadata).fieldsYaml.toStdString());
        for (const auto* kind : {"proxy-providers", "rule-providers"})
            for (const auto& entry : root[kind]) {
                if (entry.second["type"] && entry.second["type"].as<std::string>() == "inline")
                    continue;
                const auto path = QString::fromStdString(entry.second["path"].as<std::string>());
                if (!QRegularExpression(QStringLiteral("^providers/[a-f0-9]{64}[.]yaml$"))
                         .match(path)
                         .hasMatch() ||
                    retained.size() >= 32)
                    return storageError("UnsafeDataPath");
                retained.insert(path.section('/', 1));
                if (entry.second["x-quickclash-body"]) {
                    const auto decoded = QByteArray::fromBase64Encoding(
                        QByteArray::fromStdString(
                            entry.second["x-quickclash-body"].as<std::string>()),
                        QByteArray::AbortOnBase64DecodingErrors);
                    if (!decoded || decoded.decoded.isEmpty() ||
                        decoded.decoded.size() > 4 * 1024 * 1024 ||
                        decoded.decoded.size() > 16 * 1024 * 1024 - seedBytes)
                        return storageError("ProviderLimitExceeded");
                    seedBytes += decoded.decoded.size();
                    seeds.insert(path.section('/', 1), decoded.decoded);
                    if (entry.second["type"] && entry.second["type"].as<std::string>() == "file")
                        immutableFiles.insert(path.section('/', 1));
                }
            }
    } catch (const YAML::Exception&) {
        return storageError("InvalidProviderConfig");
    }
    const auto directory = std::get<QString>(resolved);
    if (!QDir().mkpath(directory))
        return storageError("DataDirectoryUnavailable");
    const auto secured = restrictToCurrentUser(directory, true);
    if (std::holds_alternative<domain::Error>(secured))
        return secured;
    const auto files = QDir(directory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                     QDir::Hidden | QDir::System);
    for (const auto& file : files) {
        const auto safe = store.path(reference + '/' + file.fileName());
        if (!std::holds_alternative<QString>(safe) || !file.isFile() ||
            !QRegularExpression(QStringLiteral("^[a-f0-9]{64}[.]yaml$"))
                 .match(file.fileName())
                 .hasMatch())
            return storageError("UnsafeDataPath");
        if (!retained.contains(file.fileName()) || file.size() > 4 * 1024 * 1024) {
            const auto removed = store.remove(reference + '/' + file.fileName());
            if (std::holds_alternative<domain::Error>(removed))
                return removed;
        }
    }
    for (auto it = seeds.begin(); it != seeds.end(); ++it) {
        const auto target = reference + '/' + it.key();
        const auto safe = store.path(target);
        if (!std::holds_alternative<QString>(safe))
            return storageError("UnsafeDataPath");
        // Preserve a live HTTP cache across restarts. An isolated validation home has
        // no cache and always validates the exact immutable import bytes.
        if (immutableFiles.contains(it.key()) || !QFileInfo::exists(std::get<QString>(safe))) {
            const auto saved = store.writeBytes(target, it.value());
            if (const auto* failure = std::get_if<domain::Error>(&saved))
                return *failure;
        }
    }
    return std::monostate{};
}
} // namespace quickclash::infra
