#include "application/desktop_runtime.h"
#include "infra/config/config_overlay.h"
#include "infra/io/bounded_read.h"
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
    if (busy_ || store_.readOnly() || profiles_.isEmpty()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    QJsonArray entries;
    qsizetype total = 0;
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
        entries.append(entry);
    }
    const QJsonObject archive{{"format", "QuickClashOfflineProfiles"},
                              {"schemaVersion", 2},
                              {"applicationVersion", "0.1.0"},
                              {"containsCredentials", true},
                              {"scope", originals ? "source" : "offline"},
                              {"profiles", entries}};
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
        validationCompletion_ = {};
        validationDeadline_.stop();
        validator_->stop();
        staging_.reset();
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
    const bool originals = version == 2 && archive.value("scope") == "source";
    if (!document.isObject() || archive.value("format") != "QuickClashOfflineProfiles" ||
        (version != 1 && version != 2) || archive.value("schemaVersion").toDouble() != version ||
        archive.value("containsCredentials") != true || entries.isEmpty() ||
        entries.size() + profiles_.size() > 100 ||
        (version == 2 && !originals && archive.value("scope") != "offline") ||
        !keysAllowed(archive, {"format", "schemaVersion", "applicationVersion",
                               "containsCredentials", "scope", "profiles"})) {
        error(QStringLiteral("InvalidBackup"));
        return;
    }
    auto context = std::make_shared<BackupRestore>();
    context->candidates = std::make_shared<ConfigCandidates>(store_);
    context->preferences = runtimePreferences_;
    context->token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    context->catalogHash = catalogDigest(profiles_);
    QJsonArray summary;
    qsizetype total = 0;
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        const auto name = entry.value("name").toString();
        const bool snapshot = entry.contains("snapshotYaml");
        const auto overlay = entry.value("overlay").toObject();
        if (!value.isObject() || name.trimmed().isEmpty() || name.size() > 80 ||
            !keysAllowed(entry, originals
                                    ? QStringList{"name", "sha256", "yaml", "overlay",
                                                  "overlayHash", "snapshotYaml", "snapshotHash"}
                                    : QStringList{"name", "sha256", "yaml"}) ||
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
                           {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
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
                const auto requests = infra::providerRequests(decoded.decoded);
                if (!std::holds_alternative<QJsonArray>(requests) ||
                    (snapshot && std::get<QJsonArray>(requests).isEmpty())) {
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
        total += QJsonDocument(overlay).toJson(QJsonDocument::Compact).size();
        const auto composed = infra::composeOverlay(effective, overlay);
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
    backupRestore_ = context;
    lastError_.clear();
    featureState_.insert("backupResult", "Preview");
    featureState_.insert("backupPreview", QJsonObject{{"token", context->token},
                                                      {"profiles", summary},
                                                      {"scope", originals ? "source" : "offline"},
                                                      {"decodedBytes", QString::number(total)},
                                                      {"containsCredentials", true}});
    publish();
}
void DesktopRuntime::restoreBackup(const QString& token) {
    auto context = backupRestore_;
    if (busy_ || store_.readOnly() || !context || context->token != token ||
        context->preferences != runtimePreferences_ ||
        context->catalogHash != catalogDigest(profiles_)) {
        error(QStringLiteral("BackupPreviewRequired"));
        return;
    }
    busy_ = true;
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
        if (saveCatalog(catalog)) {
            context->candidates->references.clear();
            featureState_.insert("backupResult", "Restored");
            lastError_.clear();
        } else {
            featureState_.insert("backupResult", "Failed");
        }
        backupRestore_.reset();
        busy_ = false;
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
    const auto composed =
        infra::composeOverlay(std::get<QByteArray>(bytes), item.value("overlay").toObject());
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
