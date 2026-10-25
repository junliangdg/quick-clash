#include "infra/storage/json_store.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
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
domain::Result<QByteArray> JsonStore::readBytes(const QString& reference, qsizetype limit) const {
    const auto resolved = path(reference);
    if (const auto* error = std::get_if<domain::Error>(&resolved)) {
        return *error;
    }
    QFile file(std::get<QString>(resolved));
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit || file.size() < 0) {
        return storageError("DataReadFailed");
    }
    // QIODevice::read(maxSize) can retain maxSize capacity after a short read. Reading a
    // 500 KiB profile with the 16 MiB policy limit must not retain a 16 MiB buffer.
    const auto expected = file.size();
    const auto bytes = file.read(expected + 1);
    if (bytes.size() > limit || file.error() != QFileDevice::NoError) {
        return storageError("DataReadFailed");
    }
    return bytes;
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
    QSaveFile file(target);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) {
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
