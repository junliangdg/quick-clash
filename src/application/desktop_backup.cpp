#include "application/desktop_runtime.h"
#include "infra/config/config_overlay.h"
#include "infra/config/custom_groups.h"
#include "infra/io/bounded_read.h"
#include "quickclash_build_info.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QUrl>

namespace quickclash::application {
namespace {
QString digest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString catalogDigest(const QJsonArray& entries) {
    return digest(QJsonDocument(entries).toJson(QJsonDocument::Compact));
}
QString overlayDigest(const QJsonObject& overlay) {
    return digest(QJsonDocument(overlay).toJson(QJsonDocument::Compact));
}
bool keysAllowed(const QJsonObject& object, const QStringList& keys) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!keys.contains(it.key()))
            return false;
    return true;
}
} // namespace
void DesktopRuntime::backupProfiles(bool originals) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    QJsonArray entries;
    bool hasCustomGroups = false;
    const auto rulebook = userRuleBook();
    const auto globalRules = rulebook.value("global").toArray();
    if (profiles_.isEmpty() && globalRules.isEmpty()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const auto profileRules = rulebook.value("profiles").toObject();
    bool hasUserRules = !globalRules.isEmpty();
    qsizetype total = QJsonDocument(globalRules).toJson(QJsonDocument::Compact).size();
    for (const auto& value : profiles_) {
        const auto item = value.toObject();
        const bool snapshot = item.contains(QStringLiteral("snapshotRef"));
        auto raw = store_.readBytes(item.value(QStringLiteral("sourceRef")).toString());
        auto expanded = snapshot ? store_.readBytes(item.value("snapshotRef").toString()) : raw;
        if (!std::holds_alternative<QByteArray>(raw) ||
            !std::holds_alternative<QByteArray>(expanded) ||
            digest(std::get<QByteArray>(raw)) != item.value("sourceHash") ||
            (snapshot && digest(std::get<QByteArray>(expanded)) != item.value("snapshotHash"))) {
            error(QStringLiteral("SourceIntegrityFailed"));
            return;
        }
        const auto base =
            infra::composeOverlay(std::get<QByteArray>(expanded), item.value("overlay").toObject());
        if (!std::holds_alternative<QByteArray>(base)) {
            error(QStringLiteral("InvalidBackup"));
            return;
        }
        const auto groupView = infra::composeCustomGroups(std::get<QByteArray>(base),
                                                          item.value("customGroups").toArray());
        if (!std::holds_alternative<infra::CustomGroupConfig>(groupView)) {
            error(std::get<domain::Error>(groupView).code);
            return;
        }
        const auto archivedGroups =
            infra::archiveCustomGroups(item.value("customGroups").toArray(),
                                       std::get<infra::CustomGroupConfig>(groupView).view);
        hasCustomGroups = hasCustomGroups || !archivedGroups.isEmpty();
        if (item.value("providerMode") == "runtime") {
            expanded = infra::offlineProviderConfig(std::get<QByteArray>(expanded));
            if (const auto* failure = std::get_if<domain::Error>(&expanded)) {
                error(failure->code);
                return;
            }
        }
        const auto overlay = item.value("overlay").toObject();
        auto composed = infra::composeOverlay(std::get<QByteArray>(expanded), overlay);
        if (const auto* failure = std::get_if<domain::Error>(&composed)) {
            error(failure->code);
            return;
        }
        // Freezing source names may enlarge definitions: never export an archive that our
        // own restore limits would reject.
        const auto archived =
            infra::composeCustomGroups(std::get<QByteArray>(composed), archivedGroups);
        if (const auto* failure = std::get_if<domain::Error>(&archived)) {
            error(failure->code);
            return;
        }
        const auto id = item.value("id").toString();
        const auto localRules = profileRules.value(id).toObject();
        if (!localRules.isEmpty()) {
            const auto restoredRules =
                infra::composeUserRules(std::get<infra::CustomGroupConfig>(archived).yaml,
                                        {{"profiles", QJsonObject{{id, localRules}}}}, id);
            if (std::holds_alternative<domain::Error>(restoredRules)) {
                // Offline provider snapshots do not retain RULE-SET providers. Do not
                // produce an archive that this version cannot restore successfully.
                featureState_.insert("backupResult", "Failed");
                error(QStringLiteral("UserRuleBackupUnavailable"));
                return;
            }
        }
        const auto& bytes = originals ? std::get<QByteArray>(raw) : std::get<QByteArray>(composed);
        total += bytes.size() + (originals && snapshot ? std::get<QByteArray>(expanded).size() : 0);
        total += originals ? QJsonDocument(overlay).toJson(QJsonDocument::Compact).size() : 0;
        // Atomic storage has a 16 MiB wire limit, including base64/JSON expansion.
        if (total > 10 * 1024 * 1024) {
            error(QStringLiteral("BackupLimitExceeded"));
            return;
        }
        QJsonObject entry{{"name", item.value("name")},
                          {"sha256", digest(bytes)},
                          {"yaml", QString::fromLatin1(bytes.toBase64())}};
        if (originals) {
            entry.insert("overlay", overlay);
            entry.insert("overlayHash", overlayDigest(overlay));
            if (snapshot) {
                entry.insert("snapshotYaml",
                             QString::fromLatin1(std::get<QByteArray>(expanded).toBase64()));
                entry.insert("snapshotHash", digest(std::get<QByteArray>(expanded)));
            }
        }
        entry.insert("customGroups", archivedGroups);
        entry.insert("customGroupsHash", catalogDigest(archivedGroups));
        hasUserRules = hasUserRules || !localRules.isEmpty();
        entry.insert("userRules", localRules);
        entry.insert("userRulesHash", overlayDigest(localRules));
        total += QJsonDocument(archivedGroups).toJson(QJsonDocument::Compact).size();
        total += QJsonDocument(localRules).toJson(QJsonDocument::Compact).size();
        if (total > 10 * 1024 * 1024) {
            error(QStringLiteral("BackupLimitExceeded"));
            return;
        }
        entries.append(entry);
    }
    if (!hasCustomGroups && !hasUserRules) {
        for (qsizetype index = 0; index < entries.size(); ++index) {
            auto entry = entries[index].toObject();
            entry.remove("customGroups");
            entry.remove("customGroupsHash");
            entries[index] = entry;
        }
    }
    if (!hasUserRules) {
        for (qsizetype index = 0; index < entries.size(); ++index) {
            auto entry = entries[index].toObject();
            entry.remove("userRules");
            entry.remove("userRulesHash");
            entries[index] = entry;
        }
    }
    QJsonObject archive{{"format", "QuickClashOfflineProfiles"},
                        {"schemaVersion", hasUserRules      ? 4
                                          : hasCustomGroups ? 3
                                                            : 2},
                        {"applicationVersion", QUICKCLASH_VERSION},
                        {"containsCredentials", true},
                        {"scope", originals ? "source" : "offline"},
                        {"profiles", entries}};
    if (hasUserRules) {
        archive.insert("globalRules", globalRules);
        archive.insert("globalRulesHash", catalogDigest(globalRules));
    }
    const auto bytes = QJsonDocument(archive).toJson(QJsonDocument::Compact);
    if (bytes.size() > 16 * 1024 * 1024) {
        error(QStringLiteral("BackupLimitExceeded"));
        return;
    }
    const auto path = QStringLiteral("backups/profiles_%1.json")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto saved = store_.writeBytes(path, bytes, true);
    if (const auto* failure = std::get_if<domain::Error>(&saved)) {
        error(failure->code);
        return;
    }
    lastError_.clear();
    featureState_.insert("backupResult", "Saved");
    featureState_.insert("backupUrl", QUrl::fromLocalFile(store_.root() + '/' + path).toString());
    publish();
}
void DesktopRuntime::cancelBackupRestore() {
    if (!backupRestore_)
        return;
    if (backupRestore_ && backupRestore_->active) {
        ++operationGeneration_;
        const auto stopped = stopValidation();
        if (const auto* failure = std::get_if<domain::Error>(&stopped))
            error(failure->code);
        busy_ = false;
    }
    backupRestore_.reset();
    featureState_.remove("backupPreview");
    featureState_.insert("backupResult", "Cancelled");
}
void DesktopRuntime::previewBackup(const QString& url) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    cancelBackupRestore();
    const QUrl source(url);
    if (!source.isLocalFile()) {
        error(QStringLiteral("BackupLocalFileRequired"));
        return;
    }
    QFile file(source.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        error(QStringLiteral("BackupLocalFileRequired"));
        return;
    }
    auto loaded = infra::readFileBounded(file, 48 * 1024 * 1024);
    if (!loaded.ok) {
        error(QStringLiteral("BackupLimitExceeded"));
        return;
    }
    const auto document = QJsonDocument::fromJson(loaded.bytes);
    loaded.bytes = {};
    const auto archive = document.object();
    const auto version = archive.value("schemaVersion").toInt();
    const auto entries = archive.value("profiles").toArray();
    const bool originals = version >= 2 && archive.value("scope") == "source";
    if (!document.isObject() || archive.value("format") != "QuickClashOfflineProfiles" ||
        (version != 1 && version != 2 && version != 3 && version != 4) ||
        archive.value("schemaVersion").toDouble() != version ||
        archive.value("containsCredentials") != true || !archive.value("profiles").isArray() ||
        (entries.isEmpty() && (version != 4 || archive.value("globalRules").toArray().isEmpty())) ||
        entries.size() + profiles_.size() > 100 ||
        (version >= 2 && !originals && archive.value("scope") != "offline") ||
        !keysAllowed(archive,
                     {"format", "schemaVersion", "applicationVersion", "containsCredentials",
                      "scope", "profiles", "globalRules", "globalRulesHash"}) ||
        (version == 4 && (!archive.value("globalRules").isArray() ||
                          catalogDigest(archive.value("globalRules").toArray()) !=
                              archive.value("globalRulesHash")))) {
        error(QStringLiteral("InvalidBackup"));
        return;
    }
    auto context = std::make_shared<BackupRestore>();
    context->candidates = std::make_shared<ConfigCandidates>(store_);
    context->preferences = runtimePreferences_;
    context->token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    context->catalogHash = catalogDigest(profiles_);
    if (version == 4) {
        context->hasGlobalRules = true;
        context->globalRules = archive.value("globalRules").toArray();
        const QByteArray validation("proxies: []\nproxy-groups: [{name: Validation, type: select, "
                                    "proxies: [DIRECT]}]\nrules: ['MATCH,DIRECT']\n");
        const QJsonObject book{
            {"global", context->globalRules},
            {"profiles", QJsonObject{{"validation", QJsonObject{{"proxy", "Validation"}}}}}};
        if (std::holds_alternative<domain::Error>(
                infra::composeUserRules(validation, book, QStringLiteral("validation")))) {
            error(QStringLiteral("InvalidBackup"));
            return;
        }
    }
    QJsonArray summary;
    qsizetype total = 0;
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        const auto name = entry.value("name").toString();
        const bool snapshot = entry.contains("snapshotYaml");
        const auto overlay = entry.value("overlay").toObject();
        const auto customGroups = entry.value("customGroups").toArray();
        auto entryKeys = originals
                             ? QStringList{"name",        "sha256",       "yaml",        "overlay",
                                           "overlayHash", "snapshotYaml", "snapshotHash"}
                             : QStringList{"name", "sha256", "yaml"};
        if (version >= 3)
            entryKeys.append({"customGroups", "customGroupsHash"});
        if (version == 4)
            entryKeys.append({"userRules", "userRulesHash"});
        if (!value.isObject() || name.trimmed().isEmpty() || name.size() > 80 ||
            !keysAllowed(entry, entryKeys) ||
            (version >= 3 && (!entry.value("customGroups").isArray() ||
                              catalogDigest(customGroups) != entry.value("customGroupsHash"))) ||
            (version == 4 && (!entry.value("userRules").isObject() ||
                              overlayDigest(entry.value("userRules").toObject()) !=
                                  entry.value("userRulesHash"))) ||
            (originals && (!entry.value("overlay").isObject() ||
                           overlayDigest(overlay) != entry.value("overlayHash"))) ||
            snapshot != entry.contains("snapshotHash")) {
            error(QStringLiteral("InvalidBackup"));
            return;
        }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject staged{{"id", id},
                           {"name", name},
                           {"secretRef", ""},
                           {"pendingApply", true},
                           {"overlay", overlay},
                           {"customGroups", customGroups},
                           {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
        if (version == 4 && !entry.value("userRules").toObject().isEmpty())
            staged.insert("restoredUserRules", entry.value("userRules"));
        QByteArray effective;
        for (const auto* key : {"yaml", "snapshotYaml"}) {
            if (QLatin1String(key) == "snapshotYaml" && !snapshot)
                continue;
            const auto decoded = QByteArray::fromBase64Encoding(
                entry.value(QLatin1String(key)).toString().toLatin1(),
                QByteArray::AbortOnBase64DecodingErrors);
            const bool isSnapshot = QLatin1String(key) == "snapshotYaml";
            const auto checksum = entry.value(isSnapshot ? "snapshotHash" : "sha256").toString();
            total += decoded.decoded.size();
            if (!decoded || decoded.decoded.isEmpty() ||
                decoded.decoded.size() > 16 * 1024 * 1024 || total > 32 * 1024 * 1024 ||
                digest(decoded.decoded) != checksum) {
                error(QStringLiteral("InvalidBackup"));
                return;
            }
            if (!isSnapshot) {
                const auto normalized = infra::coreSubscriptionConfig(decoded.decoded);
                const auto requests = infra::providerRequests(normalized);
                if (!std::holds_alternative<QJsonArray>(requests) ||
                    (snapshot && normalized == decoded.decoded &&
                     std::get<QJsonArray>(requests).isEmpty())) {
                    error(QStringLiteral("InvalidBackup"));
                    return;
                }
            }
            const auto reference = QStringLiteral("data/profiles/%1/%2.yaml")
                                       .arg(id, isSnapshot ? "snapshot" : "restored");
            const auto saved = store_.writeBytes(reference, decoded.decoded, true);
            if (const auto* failure = std::get_if<domain::Error>(&saved)) {
                error(failure->code);
                return;
            }
            context->candidates->references.append(reference);
            staged.insert(isSnapshot ? "snapshotRef" : "sourceRef", reference);
            staged.insert(isSnapshot ? "snapshotHash" : "sourceHash", checksum);
            effective = decoded.decoded;
        }
        total += QJsonDocument(overlay).toJson(QJsonDocument::Compact).size() +
                 QJsonDocument(customGroups).toJson(QJsonDocument::Compact).size();
        auto composed = infra::composeProfileConfig(effective, staged);
        if (std::holds_alternative<QByteArray>(composed) && staged.contains("restoredUserRules"))
            composed = infra::composeUserRules(
                std::get<QByteArray>(composed),
                {{"profiles", QJsonObject{{id, staged.value("restoredUserRules")}}}}, id);
        if (total > 32 * 1024 * 1024 || !std::holds_alternative<QByteArray>(composed) ||
            std::holds_alternative<domain::Error>(
                infra::preflightConfig(std::get<QByteArray>(composed), preferences()))) {
            error(QStringLiteral("InvalidBackup"));
            return;
        }
        context->entries.append(staged);
        summary.append(
            QJsonObject{{"name", name}, {"snapshot", snapshot}, {"overrides", !overlay.isEmpty()}});
    }
    // A restored profile's rules are staged with its new identity. Refuse an archive
    // that would make the combined rulebook exceed the composition/storage budget.
    auto combined = userRuleBook();
    auto combinedProfiles = combined.value("profiles").toObject();
    for (const auto& value : context->entries) {
        const auto item = value.toObject();
        if (item.contains("restoredUserRules"))
            combinedProfiles.insert(item.value("id").toString(), item.value("restoredUserRules"));
    }
    combined.insert("profiles", combinedProfiles);
    auto restoredGlobal = combined;
    if (context->hasGlobalRules)
        restoredGlobal.insert("global", context->globalRules);
    if (QJsonDocument(combined).toJson(QJsonDocument::Compact).size() > 256 * 1024 ||
        QJsonDocument(restoredGlobal).toJson(QJsonDocument::Compact).size() > 256 * 1024) {
        error(QStringLiteral("BackupLimitExceeded"));
        return;
    }
    backupRestore_ = context;
    lastError_.clear();
    featureState_.insert("backupResult", "Preview");
    featureState_.insert("backupPreview", QJsonObject{{"token", context->token},
                                                      {"profiles", summary},
                                                      {"scope", originals ? "source" : "offline"},
                                                      {"hasGlobalRules", context->hasGlobalRules},
                                                      {"decodedBytes", QString::number(total)},
                                                      {"containsCredentials", true}});
    publish();
}
void DesktopRuntime::restoreBackup(const QString& token, bool restoreGlobalRules) {
    auto context = backupRestore_;
    if (busy_ || store_.readOnly() || !context || context->token != token ||
        context->preferences != runtimePreferences_ ||
        context->catalogHash != catalogDigest(profiles_)) {
        error(QStringLiteral("BackupPreviewRequired"));
        return;
    }
    busy_ = true;
    context->restoreGlobalRules = restoreGlobalRules && context->hasGlobalRules;
    context->active = true;
    context->generation = ++operationGeneration_;
    featureState_.insert("backupResult", "Restoring");
    featureState_.remove("backupPreview");
    publish();
    restoreNext(context, 0);
}
void DesktopRuntime::restoreNext(const std::shared_ptr<BackupRestore>& context, qsizetype index) {
    if (context != backupRestore_ || context->generation != operationGeneration_)
        return;
    if (index == context->entries.size()) {
        if (context->catalogHash != catalogDigest(profiles_) ||
            context->preferences != runtimePreferences_) {
            cancelBackupRestore();
            error(QStringLiteral("BackupPreviewRequired"));
            return;
        }
        for (const auto& value : context->entries) {
            const auto item = value.toObject();
            if (std::holds_alternative<domain::Error>(store_.verifyBytes(
                    item.value("sourceRef").toString(), item.value("sourceHash").toString())) ||
                (item.contains("snapshotRef") &&
                 std::holds_alternative<domain::Error>(
                     store_.verifyBytes(item.value("snapshotRef").toString(),
                                        item.value("snapshotHash").toString())))) {
                cancelBackupRestore();
                error(QStringLiteral("SourceIntegrityFailed"));
                return;
            }
        }
        auto catalog = profiles_;
        for (const auto& value : context->entries)
            catalog.append(value);
        const bool restored = saveCatalog(catalog);
        if (restored) {
            context->candidates->references.clear();
            featureState_.insert("backupResult", "Restored");
            lastError_.clear();
        } else {
            featureState_.insert("backupResult", "Failed");
        }
        backupRestore_.reset();
        busy_ = false;
        if (restored && context->restoreGlobalRules) {
            auto book = userRuleBook();
            book.insert("global", context->globalRules);
            previousPreferences_ = runtimePreferences_;
            runtimePreferences_.insert("userRules", book);
            userRuleApplying_ = true;
            userRuleBackupApplying_ = true;
            userRuleResult_ = QStringLiteral("Applying");
            featureState_.insert("backupResult", "RulesApplying");
            lastError_.clear();
            if (!selectedId_.isEmpty()) {
                applyProfile(selectedId_, engine_.ready());
            } else {
                const auto saved = store_.write(QStringLiteral("config/runtime_defaults.json"),
                                                runtimePreferences_);
                if (const auto* failure = std::get_if<domain::Error>(&saved)) {
                    restorePreferences();
                    error(failure->code);
                } else {
                    previousPreferences_ = {};
                    savedPreferences_ = runtimePreferences_;
                    publish();
                }
            }
            return;
        }
        // Restoring never selects or starts imported profiles, even in an empty repository.
        publish();
        return;
    }
    const auto item = context->entries[index].toObject();
    const auto reference =
        item.value(item.contains("snapshotRef") ? "snapshotRef" : "sourceRef").toString();
    auto bytes = store_.readBytes(reference);
    if (!std::holds_alternative<QByteArray>(bytes) ||
        digest(std::get<QByteArray>(bytes)) !=
            item.value(item.contains("snapshotRef") ? "snapshotHash" : "sourceHash") ||
        std::holds_alternative<domain::Error>(store_.verifyBytes(
            item.value("sourceRef").toString(), item.value("sourceHash").toString()))) {
        cancelBackupRestore();
        error(QStringLiteral("SourceIntegrityFailed"));
        return;
    }
    auto composed = infra::composeProfileConfig(std::get<QByteArray>(bytes), item);
    if (std::holds_alternative<QByteArray>(composed) && item.contains("restoredUserRules"))
        composed = infra::composeUserRules(
            std::get<QByteArray>(composed),
            {{"profiles",
              QJsonObject{{item.value("id").toString(), item.value("restoredUserRules")}}}},
            item.value("id").toString());
    const auto checked =
        std::holds_alternative<QByteArray>(composed)
            ? infra::preflightConfig(std::get<QByteArray>(composed), preferences())
            : domain::Result<infra::PreflightConfig>(std::get<domain::Error>(composed));
    if (const auto* failure = std::get_if<domain::Error>(&checked)) {
        cancelBackupRestore();
        error(failure->code);
        return;
    }
    const auto weak = std::weak_ptr<BackupRestore>(context);
    validate(std::get<infra::PreflightConfig>(checked).semanticYaml, [this, weak,
                                                                      index](bool valid) {
        const auto current = weak.lock();
        if (!current || current != backupRestore_ || current->generation != operationGeneration_)
            return;
        if (!valid) {
            cancelBackupRestore();
            featureState_.insert("backupResult", "Failed");
            error(QStringLiteral("BackupValidationFailed"));
            return;
        }
        QTimer::singleShot(0, this, [this, weak, index] {
            if (const auto next = weak.lock())
                restoreNext(next, index + 1);
        });
    });
}
} // namespace quickclash::application
