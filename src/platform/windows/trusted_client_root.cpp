#include "platform/windows/trusted_client_root.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
// clang-format on

namespace quickclash::platform {
namespace {
struct HandleCloser final {
    void operator()(void* handle) const {
        if (handle && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};
using FileHandle = std::unique_ptr<void, HandleCloser>;

bool reparse(const QString& path) {
    const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

QString canonicalPath(const QString& path) {
    if (path.isEmpty() || reparse(path))
        return {};
    FileHandle handle(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!handle)
        return {};
    wchar_t buffer[32768]{};
    const auto length = GetFinalPathNameByHandleW(handle.get(), buffer, 32768,
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= 32768)
        return {};
    QString result = QString::fromWCharArray(buffer, static_cast<int>(length));
    if (result.startsWith(QStringLiteral("\\\\?\\UNC\\")))
        result = QStringLiteral("\\\\") + result.mid(8);
    else if (result.startsWith(QStringLiteral("\\\\?\\")))
        result = result.mid(4);
    return QDir::cleanPath(result);
}

bool samePath(const QString& left, const QString& right) {
    return !left.isEmpty() && left.compare(QDir::cleanPath(right), Qt::CaseInsensitive) == 0;
}

bool writableByEveryone(const QString& path) {
    constexpr DWORD kWriteMask = DELETE | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                                 FILE_WRITE_ATTRIBUTES | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE |
                                 GENERIC_ALL;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto status = GetNamedSecurityInfoW(
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor);
    if (status != ERROR_SUCCESS)
        return true;
    bool writable = false;
    for (DWORD index = 0; acl && index < acl->AceCount && !writable; ++index) {
        void* raw = nullptr;
        if (!GetAce(acl, index, &raw))
            continue;
        const auto* header = static_cast<const ACE_HEADER*>(raw);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
            continue;
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        const auto sid = reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart));
        if (!IsWellKnownSid(sid, WinWorldSid) && !IsWellKnownSid(sid, WinAnonymousSid))
            continue;
        writable = (ace->Mask & kWriteMask) != 0;
    }
    LocalFree(descriptor);
    return writable;
}

bool forbiddenRoot(const QString& root) {
    if (QDir(root).isRoot())
        return true;
    // Only the Program Files root itself is refused; the protected QuickClash installation
    // below it stays a valid client folder for the installed GUI.
    for (const auto* name : {"ProgramFiles", "ProgramFiles(x86)"}) {
        const auto value = qEnvironmentVariable(name);
        if (!value.isEmpty() && samePath(root, value))
            return true;
    }
    for (const auto* name : {"ProgramData", "PUBLIC", "SystemRoot"}) {
        const auto value = qEnvironmentVariable(name);
        if (value.isEmpty())
            continue;
        const auto normalized = QDir::cleanPath(value);
        if (samePath(root, normalized) ||
            root.startsWith(normalized + QLatin1Char('/'), Qt::CaseInsensitive))
            return true;
    }
    return false;
}
} // namespace
QString portableCorePath(const QString& clientRoot) {
    const QDir client(clientRoot);
    const auto packaged = client.filePath(QStringLiteral("../core/mihomo.exe"));
    if (QFileInfo::exists(packaged) ||
        QFileInfo::exists(client.filePath(QStringLiteral("../metadata/release_matrix.json"))))
        return QFileInfo(packaged).absoluteFilePath();
#ifdef QUICKCLASH_DEVELOPMENT_CORE
    return QStringLiteral(QUICKCLASH_DEVELOPMENT_CORE);
#else
    return client.filePath(QStringLiteral("core/mihomo.exe"));
#endif
}

std::optional<TrustedClientRoot> makeTrustedClientRoot(const QString& root) {
    if (root.isEmpty() || !QDir::isAbsolutePath(root))
        return std::nullopt;
    const QFileInfo info(root);
    if (!info.isDir() || info.isSymbolicLink() || info.isJunction() || reparse(root))
        return std::nullopt;
    const auto canonicalRoot = canonicalPath(root);
    // Canonicalizing must not move the authorized directory elsewhere.
    if (!samePath(canonicalRoot, QDir::cleanPath(root)))
        return std::nullopt;
    // The service runs as LocalSystem, so a folder any local account can write must never
    // become the authorized client image source.
    if (forbiddenRoot(canonicalRoot) || writableByEveryone(canonicalRoot))
        return std::nullopt;
    const auto executable = QDir(canonicalRoot).filePath(QStringLiteral("quickclash.exe"));
    const QFileInfo executableInfo(executable);
    if (!executableInfo.isFile() || executableInfo.isSymbolicLink() ||
        executableInfo.isJunction() || reparse(executable))
        return std::nullopt;
    const auto canonicalExecutable = canonicalPath(executable);
    if (!samePath(canonicalExecutable, executable))
        return std::nullopt;
    return TrustedClientRoot{canonicalRoot, canonicalExecutable};
}

std::optional<TrustedClientRoot> readTrustedClientRoot(const QString& configFile,
                                                       const QString& fallbackRoot) {
    const QFileInfo info(configFile);
    if (!info.exists())
        return makeTrustedClientRoot(fallbackRoot);
    if (!info.isFile() || info.isSymbolicLink() || info.isJunction() || reparse(configFile) ||
        info.size() > 64 * 1024)
        return std::nullopt;
    QFile file(configFile);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return std::nullopt;
    const auto object = document.object();
    if (object.size() != 2 || object.value(QStringLiteral("schemaVersion")).toInt(-1) != 1 ||
        !object.value(QStringLiteral("clientRoot")).isString())
        return std::nullopt;
    return makeTrustedClientRoot(object.value(QStringLiteral("clientRoot")).toString());
}

std::optional<QString> trustedClientImage(const QString& imagePath,
                                          const TrustedClientRoot& trusted) {
    if (imagePath.isEmpty() || !QDir::isAbsolutePath(imagePath))
        return std::nullopt;
    const auto canonical = canonicalPath(imagePath);
    if (!samePath(canonical, trusted.executable))
        return std::nullopt;
    return canonical;
}

} // namespace quickclash::platform
