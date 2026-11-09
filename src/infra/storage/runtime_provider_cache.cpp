#include "infra/storage/runtime_provider_cache.h"
#include "infra/config/config_metadata.h"
#include <QDir>
#include <QDirIterator>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <yaml-cpp/yaml.h>
namespace quickclash::infra {
domain::Result<QByteArray> prepareRuntimeProviderRevision(const QByteArray& semantic,
                                                          JsonStore& store,
                                                          const QString& coreReference,
                                                          const QString& revision,
                                                          const QString& cacheRevision) {
    if (!QRegularExpression(QStringLiteral("^[a-f0-9-]{1,64}$")).match(revision).hasMatch())
        return storageError("UnsafeDataPath");
    const auto prefix = QStringLiteral("revisions/%1").arg(revision);
    const auto prepared = prepareRuntimeProviderCache(
        semantic, store, coreReference.isEmpty() ? prefix : coreReference + '/' + prefix);
    if (const auto* error = std::get_if<domain::Error>(&prepared))
        return *error;
    try {
        auto root = YAML::Load(semantic.toStdString());
        for (const auto* kind : {"proxy-providers", "rule-providers"})
            for (auto entry : root[kind]) {
                auto value = entry.second;
                if (value["path"]) {
                    const auto relative = QString::fromStdString(value["path"].as<std::string>());
                    const auto old = coreReference + '/' +
                                     (cacheRevision.isEmpty()
                                          ? QString{}
                                          : QStringLiteral("revisions/%1/").arg(cacheRevision)) +
                                     relative;
                    const auto source = store.path(old);
                    if (const auto* failure = std::get_if<domain::Error>(&source))
                        return *failure;
                    if (value["type"] && value["type"].as<std::string>() == "http" &&
                        QFileInfo::exists(std::get<QString>(source))) {
                        const auto body = store.readBytes(old, 4 * 1024 * 1024);
                        if (const auto* failure = std::get_if<domain::Error>(&body))
                            return *failure;
                        const auto saved =
                            store.writeBytes(coreReference + '/' + prefix + '/' + relative,
                                             std::get<QByteArray>(body));
                        if (const auto* failure = std::get_if<domain::Error>(&saved))
                            return *failure;
                    }
                    value["path"] = prefix.toStdString() + '/' + value["path"].as<std::string>();
                }
            }
        YAML::Emitter output;
        output << root;
        if (!output.good() || output.size() > 16 * 1024 * 1024)
            return storageError("ConfigLimitExceeded");
        return QByteArray(output.c_str(), static_cast<qsizetype>(output.size()));
    } catch (const YAML::Exception&) {
        return storageError("InvalidProviderConfig");
    }
}
domain::CommandResult preserveRuntimeProviderCache(JsonStore& store, const QString& coreReference,
                                                   const QString& revision) {
    if (revision.isEmpty())
        return std::monostate{};
    if (!QRegularExpression(QStringLiteral("^[a-f0-9-]{1,64}$")).match(revision).hasMatch())
        return storageError("UnsafeDataPath");
    const auto reference = coreReference + QStringLiteral("/revisions/%1").arg(revision);
    const auto listed = store.files(reference);
    if (const auto* failure = std::get_if<domain::Error>(&listed))
        return *failure;
    const auto files = std::get<QStringList>(listed);
    if (files.size() > 32)
        return storageError("ProviderLimitExceeded");
    for (const auto& file : files)
        if (!QRegularExpression(QStringLiteral("^providers/[a-f0-9]{64}[.]yaml$"))
                 .match(file.mid(reference.size() + 1))
                 .hasMatch())
            return storageError("UnsafeDataPath");
    const auto resolved = store.path(coreReference + QStringLiteral("/providers"));
    if (const auto* failure = std::get_if<domain::Error>(&resolved))
        return *failure;
    QSet<QString> retained;
    for (const auto& file : files)
        retained.insert(QFileInfo(file).fileName());
    const auto old =
        QDir(std::get<QString>(resolved))
            .entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto& file : old)
        if (!file.isFile() ||
            !QRegularExpression(QStringLiteral("^[a-f0-9]{64}[.]yaml$"))
                 .match(file.fileName())
                 .hasMatch() ||
            !std::holds_alternative<QString>(
                store.path(coreReference + "/providers/" + file.fileName())))
            return storageError("UnsafeDataPath");
    for (const auto& file : files) {
        const auto bytes = store.readBytes(file, 4 * 1024 * 1024);
        if (const auto* failure = std::get_if<domain::Error>(&bytes))
            return *failure;
        const auto saved = store.writeBytes(coreReference + '/' + file.mid(reference.size() + 1),
                                            std::get<QByteArray>(bytes));
        if (const auto* failure = std::get_if<domain::Error>(&saved))
            return *failure;
    }
    for (const auto& file : old)
        if (!retained.contains(file.fileName())) {
            const auto removed = store.remove(coreReference + "/providers/" + file.fileName());
            if (const auto* failure = std::get_if<domain::Error>(&removed))
                return *failure;
        }
    return std::monostate{};
}
domain::CommandResult collectRuntimeProviderRevisions(JsonStore& store,
                                                      const QString& coreReference,
                                                      const QStringList& retained) {
    const auto reference = coreReference + QStringLiteral("/revisions");
    const auto resolved = store.path(reference);
    if (const auto* failure = std::get_if<domain::Error>(&resolved))
        return *failure;
    const QDir parent(std::get<QString>(resolved));
    const auto entries =
        parent.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    QStringList removed;
    QStringList files;
    for (const auto& entry : entries) {
        const auto name = entry.fileName();
        const auto directory = reference + '/' + name;
        if (!QRegularExpression(QStringLiteral("^[a-f0-9-]{1,64}$")).match(name).hasMatch() ||
            !entry.isDir() || !std::holds_alternative<QString>(store.path(directory)))
            return storageError("UnsafeDataPath");
        QDirIterator tree(entry.absoluteFilePath(),
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
        while (tree.hasNext()) {
            const auto child = tree.nextFileInfo();
            const auto relative =
                QDir(entry.absoluteFilePath()).relativeFilePath(child.absoluteFilePath());
            if (!std::holds_alternative<QString>(store.path(directory + '/' + relative)) ||
                (child.isDir()
                     ? relative != QStringLiteral("providers")
                     : !child.isFile() ||
                           !QRegularExpression(QStringLiteral("^providers/[a-f0-9]{64}[.]yaml$"))
                                .match(relative)
                                .hasMatch()))
                return storageError("UnsafeDataPath");
        }
        const auto listed = store.files(directory);
        if (const auto* failure = std::get_if<domain::Error>(&listed))
            return *failure;
        for (const auto& file : std::get<QStringList>(listed)) {
            if (!QRegularExpression(QStringLiteral("^providers/[a-f0-9]{64}[.]yaml$"))
                     .match(file.mid(directory.size() + 1))
                     .hasMatch())
                return storageError("UnsafeDataPath");
            if (!retained.contains(name))
                files.append(file);
        }
        if (!retained.contains(name))
            removed.append(directory);
    }
    for (const auto& file : files) {
        const auto result = store.remove(file);
        if (std::holds_alternative<domain::Error>(result))
            return result;
    }
    for (const auto& directory : removed) {
        const auto path = std::get<QString>(store.path(directory));
        if ((QDir(path + "/providers").exists() && !QDir().rmdir(path + "/providers")) ||
            !QDir().rmdir(path))
            return storageError("DataRemoveFailed");
    }
    return std::monostate{};
}
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
