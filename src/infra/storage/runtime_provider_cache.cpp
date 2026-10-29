#include "infra/storage/runtime_provider_cache.h"
#include "infra/config/config_metadata.h"
#include <QDir>
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
    try {
        const auto root = YAML::Load(std::get<ConfigMetadata>(metadata).fieldsYaml.toStdString());
        for (const auto* kind : {"proxy-providers", "rule-providers"})
            for (const auto& entry : root[kind]) {
                const auto path = QString::fromStdString(entry.second["path"].as<std::string>());
                if (!QRegularExpression(QStringLiteral("^providers/[a-f0-9]{64}[.]yaml$"))
                         .match(path)
                         .hasMatch() ||
                    retained.size() >= 32)
                    return storageError("UnsafeDataPath");
                retained.insert(path.section('/', 1));
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
    return std::monostate{};
}
} // namespace quickclash::infra
