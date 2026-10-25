#include "application/desktop_runtime.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QUrl>

namespace quickclash::application {
namespace {
QString digest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
} // namespace
void DesktopRuntime::backupProfiles() {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    QJsonArray entries;
    qsizetype total = 0;
    for (const auto& value : profiles_) {
        const auto item = value.toObject();
        const auto reference =
            item.value(item.contains(QStringLiteral("snapshotRef")) ? QStringLiteral("snapshotRef")
                                                                    : QStringLiteral("sourceRef"))
                .toString();
        const auto result = store_.readBytes(reference);
        if (const auto* failure = std::get_if<domain::Error>(&result)) {
            error(failure->code);
            return;
        }
        const auto bytes = std::get<QByteArray>(result);
        total += bytes.size();
        if (total > 32 * 1024 * 1024) {
            error(QStringLiteral("BackupLimitExceeded"));
            return;
        }
        entries.append(
            QJsonObject{{QStringLiteral("name"), item.value(QStringLiteral("name"))},
                        {QStringLiteral("sha256"), digest(bytes)},
                        {QStringLiteral("yaml"), QString::fromLatin1(bytes.toBase64())}});
    }
    const QJsonObject archive{
        {QStringLiteral("format"), QStringLiteral("QuickClashOfflineProfiles")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("applicationVersion"), QStringLiteral("0.1.0")},
        {QStringLiteral("containsCredentials"), true},
        {QStringLiteral("profiles"), entries}};
    const auto path =
        QStringLiteral("backups/profiles_%1.json")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz")));
    const auto result =
        store_.writeBytes(path, QJsonDocument(archive).toJson(QJsonDocument::Compact), true);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
        return;
    }
    featureState_.insert(QStringLiteral("backupResult"), QStringLiteral("Saved"));
    featureState_.insert(QStringLiteral("backupUrl"),
                         QUrl::fromLocalFile(store_.root() + '/' + path).toString());
    publish();
}
void DesktopRuntime::restoreBackup(const QString& url) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const QUrl source(url);
    if (!source.isLocalFile()) {
        error(QStringLiteral("BackupLocalFileRequired"));
        return;
    }
    QFile file(source.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 48 * 1024 * 1024) {
        error(QStringLiteral("BackupLimitExceeded"));
        return;
    }
    const auto bytes = file.read(48 * 1024 * 1024 + 1);
    const auto document = QJsonDocument::fromJson(bytes);
    const auto archive = document.object();
    const auto entries = archive.value(QStringLiteral("profiles")).toArray();
    if (!document.isObject() ||
        archive.value(QStringLiteral("format")) != QStringLiteral("QuickClashOfflineProfiles") ||
        archive.value(QStringLiteral("schemaVersion")).toInt() != 1 || entries.isEmpty() ||
        entries.size() + profiles_.size() > 100) {
        error(QStringLiteral("InvalidBackup"));
        return;
    }
    qsizetype total = 0;
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        const auto decoded = QByteArray::fromBase64Encoding(
            entry.value(QStringLiteral("yaml")).toString().toLatin1(),
            QByteArray::AbortOnBase64DecodingErrors);
        total += decoded.decoded.size();
        if (!decoded || decoded.decoded.size() > 16 * 1024 * 1024 || total > 32 * 1024 * 1024 ||
            digest(decoded.decoded) != entry.value(QStringLiteral("sha256")) ||
            entry.value(QStringLiteral("name")).toString().trimmed().isEmpty() ||
            entry.value(QStringLiteral("name")).toString().size() > 80 ||
            std::holds_alternative<domain::Error>(
                infra::preflightConfig(decoded.decoded, preferences()))) {
            error(QStringLiteral("InvalidBackup"));
            return;
        }
    }
    busy_ = true;
    featureState_.insert(QStringLiteral("backupResult"), QStringLiteral("Restoring"));
    publish();
    restoreNext(entries, 0, {}, ++operationGeneration_);
}
void DesktopRuntime::restoreNext(const QJsonArray& entries, qsizetype index, QJsonArray staged,
                                 quint64 generation) {
    if (generation != operationGeneration_)
        return;
    if (index == entries.size()) {
        auto catalog = profiles_;
        for (const auto& item : staged)
            catalog.append(item);
        busy_ = false;
        if (saveCatalog(catalog)) {
            featureState_.insert(QStringLiteral("backupResult"), QStringLiteral("Restored"));
            if (selectedId_.isEmpty())
                selectProfile(staged.first().toObject().value(QStringLiteral("id")).toString());
            publish();
        }
        return;
    }
    const auto entry = entries[index].toObject();
    const auto bytes =
        QByteArray::fromBase64(entry.value(QStringLiteral("yaml")).toString().toLatin1());
    const auto semantic =
        std::get<infra::PreflightConfig>(infra::preflightConfig(bytes, preferences())).semanticYaml;
    validate(semantic, [this, entries, index, staged = std::move(staged), bytes,
                        generation](bool valid) mutable {
        if (generation != operationGeneration_)
            return;
        if (!valid) {
            busy_ = false;
            error(QStringLiteral("BackupValidationFailed"));
            return;
        }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto reference = QStringLiteral("data/profiles/%1/restored.yaml").arg(id);
        const auto saved = store_.writeBytes(reference, bytes, true);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            busy_ = false;
            error(failure->code);
            return;
        }
        staged.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), entries[index].toObject().value(QStringLiteral("name"))},
            {QStringLiteral("sourceRef"), reference},
            {QStringLiteral("sourceHash"), digest(bytes)},
            {QStringLiteral("secretRef"), QString{}},
            {QStringLiteral("pendingApply"), true},
            {QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
        restoreNext(entries, index + 1, std::move(staged), generation);
    });
}
} // namespace quickclash::application
