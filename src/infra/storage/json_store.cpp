#include "infra/storage/json_store.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/geo_resource_writer.h"
#include "infra/storage/storage_maintenance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <algorithm>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <aclapi.h>
#include <io.h>
#include <sddl.h>
// clang-format on
#endif

namespace quickclash::infra {
namespace {
constexpr qsizetype kMetadataLimit = 2 * 1024 * 1024;
bool linked(const QFileInfo& info) {
    return info.isSymbolicLink() || info.isJunction();
}
} // namespace
domain::Error storageError(const char* code) {
    domain::Error error;
    error.code = QString::fromLatin1(code);
    error.userMessageKey = error.code;
    error.category = domain::ErrorCategory::Storage;
    error.stage = QStringLiteral("storage");
    return error;
}
domain::CommandResult restrictToCurrentUser(const QString& path, bool directory) {
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return storageError("DataPermissionsFailed");
    }
    DWORD size = 0;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(static_cast<qsizetype>(size), Qt::Uninitialized);
    const bool queried = GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    CloseHandle(token);
    if (!queried) {
        return storageError("DataPermissionsFailed");
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.constData());
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid)) {
        return storageError("DataPermissionsFailed");
    }
    const auto inherit = directory ? QStringLiteral("OICI") : QString{};
    const auto sddl = QStringLiteral("D:P(A;%1;FA;;;SY)(A;%1;FA;;;%2)")
                          .arg(inherit, QString::fromWCharArray(sid));
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr)) {
        return storageError("DataPermissionsFailed");
    }
    const bool applied = SetFileSecurityW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor);
    if (!applied) {
        return storageError("DataPermissionsFailed");
    }
#else
    auto permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (directory) {
        permissions |= QFileDevice::ExeOwner;
    }
    if (!QFile::setPermissions(path, permissions)) {
        return storageError("DataPermissionsFailed");
    }
#endif
    return std::monostate{};
}
JsonStore::JsonStore(QString root) : root_(QDir::cleanPath(QFileInfo(root).absoluteFilePath())) {}
domain::CommandResult JsonStore::open() {
    if (opened_) {
        return readOnly_ ? domain::CommandResult{storageError("RepositoryReadOnly")}
                         : domain::CommandResult{std::monostate{}};
    }
    // Refuse linked ancestors before creating any directories.
    for (auto ancestor = QFileInfo(root_); !ancestor.isRoot();
         ancestor = QFileInfo(ancestor.absolutePath())) {
        if (linked(ancestor)) {
            return storageError("UnsafeDataPath");
        }
    }
    if (!QDir().mkpath(root_)) {
        return storageError("DataDirectoryUnavailable");
    }
    const auto secured = restrictToCurrentUser(root_, true);
    if (std::holds_alternative<domain::Error>(secured)) {
        return std::get<domain::Error>(secured);
    }
    lock_ = std::make_unique<QLockFile>(QDir(root_).filePath(QStringLiteral("writer.lock")));
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock(0)) {
        return storageError("RepositoryBusy");
    }
    opened_ = true;
    const auto pending = path(QStringLiteral("recovery_pending.json"));
    if (!std::holds_alternative<QString>(pending)) {
        freeze();
        return std::monostate{};
    }
    if (QFileInfo::exists(std::get<QString>(pending))) {
        const auto loaded = readBytes(QStringLiteral("recovery_pending.json"), 4096);
        const auto* bytes = std::get_if<QByteArray>(&loaded);
        const auto marker = bytes ? QJsonDocument::fromJson(*bytes).object() : QJsonObject{};
        const auto archive = marker.value("archive").toString();
        const auto id = archive.mid(9);
        const auto resolvedArchive = path(archive);
        const bool complete = marker.value("schemaVersion") == 1 &&
                              marker.value("complete") == true && archive.startsWith("recovery/") &&
                              !QUuid(id).isNull() &&
                              QUuid(id).toString(QUuid::WithoutBraces) == id &&
                              std::holds_alternative<QString>(resolvedArchive) &&
                              QFileInfo(std::get<QString>(resolvedArchive)).isDir();
        // A crash during multi-directory archival must never expose a partial catalog as normal.
        if (!complete || !QFile::remove(std::get<QString>(pending)))
            freeze();
    }
    return std::monostate{};
}
domain::Result<QString> JsonStore::path(const QString& reference) const {
    if (reference.isEmpty() || reference.contains(QLatin1Char('\\')) ||
        reference.contains(QLatin1Char(':')) || QDir::isAbsolutePath(reference)) {
        return storageError("UnsafeDataPath");
    }
    const auto parts = reference.split(QLatin1Char('/'));
    auto current = root_;
    if (linked(QFileInfo(current))) {
        return storageError("UnsafeDataPath");
    }
    for (const auto& part : parts) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..") ||
            part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))) {
            return storageError("UnsafeDataPath");
        }
        for (const auto character : part) {
            if (!(character.isLetterOrNumber() && character.unicode() < 128) &&
                character != QLatin1Char('_') && character != QLatin1Char('-') &&
                character != QLatin1Char('.')) {
                return storageError("UnsafeDataPath");
            }
        }
        const auto base = part.section(QLatin1Char('.'), 0, 0).toUpper();
        if (base == QStringLiteral("CON") || base == QStringLiteral("PRN") ||
            base == QStringLiteral("AUX") || base == QStringLiteral("NUL") ||
            (base.size() == 4 &&
             (base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT"))) &&
             base.back() >= QLatin1Char('0') && base.back() <= QLatin1Char('9'))) {
            return storageError("UnsafeDataPath");
        }
        current = QDir(current).filePath(part);
        if (linked(QFileInfo(current))) {
            return storageError("UnsafeDataPath");
        }
    }
    return current;
}

domain::Result<QString> JsonStore::writeSupport(const QJsonObject& report) {
    if (!opened_ || !lock_ || !lock_->isLocked())
        return storageError("RepositoryReadOnly");
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (bytes.size() > 256 * 1024)
        return storageError("MetadataLimitExceeded");
    const auto inventory = inspectStorage(*this);
    if (const auto* failure = std::get_if<domain::Error>(&inventory))
        return *failure;
    const auto& usage = std::get<StorageInventory>(inventory);
    QList<StorageFile> exports;
    for (const auto& file : usage.files)
        if (file.reference.startsWith(QStringLiteral("logs/support_")))
            exports.append(file);
    if (usage.logs + bytes.size() > kLogBudget)
        return storageError("StorageBudgetExceeded");
    const auto ref = QStringLiteral("logs/support_%1.json")
                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto resolved = path(ref);
    if (const auto* failure = std::get_if<domain::Error>(&resolved))
        return *failure;
    const auto parent = QFileInfo(std::get<QString>(resolved)).absolutePath();
    if (!QDir().mkpath(parent))
        return storageError("DataWriteFailed");
    const auto secured = restrictToCurrentUser(parent, true);
    if (const auto* failure = std::get_if<domain::Error>(&secured))
        return *failure;
    resolved = path(ref);
    if (const auto* failure = std::get_if<domain::Error>(&resolved))
        return *failure;
    QSaveFile output(std::get<QString>(resolved));
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        return storageError("DataWriteFailed");
    if (output.write(bytes) != bytes.size() || !output.flush())
        return storageError("DataWriteFailed");
#ifdef Q_OS_WIN
    if (!FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(output.handle()))))
        return storageError("DataSyncFailed");
#endif
    if (!output.commit())
        return storageError("DataCommitFailed");
    std::sort(exports.begin(), exports.end(),
              [](const auto& a, const auto& b) { return a.modified < b.modified; });
    while (exports.size() >= 32) {
        const auto old = path(exports.takeFirst().reference);
        if (!std::holds_alternative<QString>(old) || !QFile::remove(std::get<QString>(old)))
            return storageError("DataRemoveFailed");
    }
    return ref;
}

domain::Result<QString> JsonStore::archiveForRecovery(const QString& fingerprint) {
    if (!opened_ || !readOnly_ || !lock_ || !lock_->isLocked())
        return storageError("RepositoryRecoveryRequired");
    const auto pending = path(QStringLiteral("recovery_pending.json"));
    if (!std::holds_alternative<QString>(pending) || QFileInfo::exists(std::get<QString>(pending)))
        return storageError("RecoveryArchiveIncomplete");
    const auto inventory = inspectStorage(*this, true);
    if (const auto* failure = std::get_if<domain::Error>(&inventory))
        return *failure;
    if (std::get<StorageInventory>(inventory).fingerprint != fingerprint)
        return storageError("StoragePreviewStale");
    const auto reference =
        QStringLiteral("recovery/%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto destination = path(reference);
    if (const auto* failure = std::get_if<domain::Error>(&destination))
        return *failure;
    const auto target = std::get<QString>(destination);
    if (!QDir().mkpath(target))
        return storageError("DataWriteFailed");
    const auto secured = restrictToCurrentUser(target, true);
    if (const auto* failure = std::get_if<domain::Error>(&secured))
        return *failure;
    const auto mark = [&](bool complete) {
        QSaveFile marker(std::get<QString>(pending));
        marker.setDirectWriteFallback(false);
        const auto bytes =
            QJsonDocument(
                QJsonObject{{"schemaVersion", 1}, {"archive", reference}, {"complete", complete}})
                .toJson(QJsonDocument::Compact);
        if (!marker.open(QIODevice::WriteOnly) || marker.write(bytes) != bytes.size() ||
            !marker.flush())
            return false;
#ifdef Q_OS_WIN
        if (!FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(marker.handle()))))
            return false;
#endif
        return marker.commit();
    };
    if (!mark(false))
        return storageError("DataWriteFailed");
    const auto entries = QDir(root_).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    QStringList moved;
    for (const auto& entry : entries) {
        if (entry == QStringLiteral("writer.lock") || entry == QStringLiteral("recovery") ||
            entry == QStringLiteral("recovery_pending.json"))
            continue;
        // Validate again immediately before each move. No recursive removal is used.
        const auto source = path(entry);
        const auto archive = path(reference + '/' + entry);
        const bool valid =
            std::holds_alternative<QString>(source) && std::holds_alternative<QString>(archive);
        if (!valid || !QDir().rename(std::get<QString>(source), std::get<QString>(archive))) {
            bool restored = true;
            for (auto it = moved.crbegin(); it != moved.crend(); ++it)
                restored = QDir().rename(target + '/' + *it, root_ + '/' + *it) && restored;
            if (restored)
                restored = QFile::remove(std::get<QString>(pending));
            return storageError(restored ? "RecoveryArchiveFailed" : "RecoveryArchiveIncomplete");
        }
        moved.append(entry);
    }
    if (!mark(true))
        return storageError("RecoveryArchiveIncomplete");
    return reference;
}
domain::Result<QByteArray> JsonStore::readBytes(const QString& reference, qsizetype limit) const {
    const auto resolved = path(reference);
    if (const auto* error = std::get_if<domain::Error>(&resolved)) {
        return *error;
    }
    QFile file(std::get<QString>(resolved));
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit || file.size() < 0) {
        return storageError("DataReadFailed");
    }
    auto loaded = readFileBounded(file, limit);
    if (!loaded.ok) {
        return storageError("DataReadFailed");
    }
    return std::move(loaded.bytes);
}
domain::CommandResult JsonStore::verifyBytes(const QString& reference,
                                             const QString& sha256) const {
    const auto resolved = path(reference);
    if (const auto* failure = std::get_if<domain::Error>(&resolved))
        return *failure;
    QFile input(std::get<QString>(resolved));
    if (!input.open(QIODevice::ReadOnly) || input.size() < 0 || input.size() > 16 * 1024 * 1024)
        return storageError("DataReadFailed");
    const auto expected = input.size();
    qint64 total = 0;
    QCryptographicHash checksum(QCryptographicHash::Sha256);
    const auto status = consumeAvailable(input, total, 16 * 1024 * 1024, [&](QByteArrayView chunk) {
        checksum.addData(chunk);
        return true;
    });
    if (status != ReadStatus::Complete || total != expected || input.size() != expected ||
        input.error() != QFileDevice::NoError ||
        QString::fromLatin1(checksum.result().toHex()) != sha256)
        return storageError("SourceIntegrityFailed");
    return std::monostate{};
}
domain::Result<std::optional<QJsonObject>> JsonStore::read(const QString& reference) {
    const auto resolved = path(reference);
    if (const auto* error = std::get_if<domain::Error>(&resolved)) {
        return *error;
    }
    if (!QFileInfo::exists(std::get<QString>(resolved))) {
        return std::optional<QJsonObject>{};
    }
    const auto bytes = readBytes(reference, kMetadataLimit);
    if (const auto* error = std::get_if<domain::Error>(&bytes)) {
        freeze();
        return *error;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(std::get<QByteArray>(bytes), &parseError);
    const auto version = document.object().value(QStringLiteral("schemaVersion"));
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !version.isDouble() || version.toDouble() != 1) {
        freeze();
        return storageError(version.isDouble() && version.toDouble() > 1
                                ? "SchemaTooNew"
                                : "MetadataRecoveryRequired");
    }
    return std::optional<QJsonObject>{document.object()};
}
domain::CommandResult JsonStore::writeBytes(const QString& reference, const QByteArray& bytes,
                                            bool immutable) {
    if (bytes.size() > 16 * 1024 * 1024)
        return storageError("DataLimitExceeded");
    const auto resolved = prepareWrite(reference, immutable);
    if (const auto* error = std::get_if<domain::Error>(&resolved))
        return *error;
    QSaveFile file(std::get<QString>(resolved));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return storageError("DataWriteFailed");
    return commitWrite(file);
}
domain::Result<QString> JsonStore::prepareWrite(const QString& reference, bool immutable) {
    if (!opened_ || readOnly_) {
        return storageError("RepositoryReadOnly");
    }
    const auto resolved = path(reference);
    if (const auto* error = std::get_if<domain::Error>(&resolved)) {
        return *error;
    }
    const auto target = std::get<QString>(resolved);
    if (immutable && QFileInfo::exists(target)) {
        return storageError("ArtifactAlreadyExists");
    }
    const auto parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent)) {
        return storageError("DataWriteFailed");
    }
    const auto secured = restrictToCurrentUser(parent, true);
    if (const auto* error = std::get_if<domain::Error>(&secured)) {
        return *error;
    }
    // Recheck after directory creation; all writers share this path/ACL boundary.
    return path(reference);
}
domain::CommandResult JsonStore::commitWrite(QSaveFile& file) const {
    if (!opened_ || readOnly_)
        return storageError("RepositoryReadOnly");
    if (!file.flush()) {
        return storageError("DataWriteFailed");
    }
#ifdef Q_OS_WIN
    if (!FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())))) {
        return storageError("DataSyncFailed");
    }
#endif
    if (!file.commit()) {
        return storageError("DataCommitFailed");
    }
    return std::monostate{};
}
domain::Result<std::unique_ptr<GeoResourceWriter>> JsonStore::beginGeoWrite() {
    const auto resolved = prepareWrite(QStringLiteral("cache/geo/download"), false);
    if (const auto* error = std::get_if<domain::Error>(&resolved))
        return *error;
    // The private constructor prevents bypassing the store's path and ACL checks.
    auto writer = std::unique_ptr<GeoResourceWriter>(
        new GeoResourceWriter(*this, std::get<QString>(resolved) + QStringLiteral(".XXXXXX")));
    const auto opened = writer->open();
    if (const auto* error = std::get_if<domain::Error>(&opened))
        return *error;
    return writer;
}
domain::Result<std::unique_ptr<QTemporaryFile>> JsonStore::beginConfigTemporary() {
    const auto resolved = prepareWrite(QStringLiteral("cache/config_stage/item"), false);
    if (const auto* failure = std::get_if<domain::Error>(&resolved))
        return *failure;
    auto file =
        std::make_unique<QTemporaryFile>(std::get<QString>(resolved) + QStringLiteral(".XXXXXX"));
    if (!file->open())
        return storageError("DataWriteFailed");
    const auto secured = restrictToCurrentUser(file->fileName(), false);
    if (const auto* failure = std::get_if<domain::Error>(&secured))
        return *failure;
    return file;
}
domain::CommandResult JsonStore::write(const QString& reference, QJsonObject value) {
    // Never replace unknown or corrupt metadata with defaults.
    const auto previous = read(reference);
    if (const auto* error = std::get_if<domain::Error>(&previous)) {
        return *error;
    }
    value.insert(QStringLiteral("schemaVersion"), 1);
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (bytes.size() > kMetadataLimit) {
        return storageError("MetadataLimitExceeded");
    }
    return writeBytes(reference, bytes);
}
domain::CommandResult JsonStore::remove(const QString& reference) {
    if (!opened_ || readOnly_) {
        return storageError("RepositoryReadOnly");
    }
    const auto resolved = path(reference);
    if (const auto* error = std::get_if<domain::Error>(&resolved)) {
        return *error;
    }
    const auto target = std::get<QString>(resolved);
    if (QFileInfo::exists(target) && !QFile::remove(target)) {
        return storageError("DataRemoveFailed");
    }
    return std::monostate{};
}
domain::Result<QStringList> JsonStore::files(const QString& directory) const {
    const auto resolved = path(directory);
    if (const auto* failure = std::get_if<domain::Error>(&resolved))
        return *failure;
    QStringList files;
    QDirIterator iterator(std::get<QString>(resolved),
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto entry = iterator.nextFileInfo();
        if (linked(entry))
            return storageError("UnsafeDataPath");
        if (!entry.isFile())
            continue;
        const auto relative = QDir(root_).relativeFilePath(entry.absoluteFilePath());
        if (std::holds_alternative<domain::Error>(path(relative)))
            return storageError("UnsafeDataPath");
        files.append(relative);
        if (files.size() > 10000)
            return storageError("RepositoryFileLimitExceeded");
    }
    return files;
}
} // namespace quickclash::infra
