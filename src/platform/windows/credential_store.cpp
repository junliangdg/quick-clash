#include "platform/windows/credential_store.h"
#include "platform/desktop_services.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <wincrypt.h>
// clang-format on
#endif

namespace quickclash::platform {
std::unique_ptr<application::ICredentialStore> makeCredentialStore(infra::JsonStore& store) {
    return std::make_unique<CredentialStore>(store);
}
domain::Result<QString> CredentialStore::referencePath(const QString& reference) const {
    const QUuid id(reference);
    if (id.isNull() || reference != id.toString(QUuid::WithoutBraces)) {
        return infra::storageError("InvalidSecretReference");
    }
    return QStringLiteral("data/credentials/%1.blob").arg(reference);
}
domain::Result<QByteArray> CredentialStore::read(const QString& reference) const {
    const auto path = referencePath(reference);
    if (const auto* error = std::get_if<domain::Error>(&path)) {
        return *error;
    }
    const auto bytes = store_.readBytes(std::get<QString>(path), 128 * 1024);
    if (const auto* error = std::get_if<domain::Error>(&bytes)) {
        return *error;
    }
#ifdef Q_OS_WIN
    const auto& encrypted = std::get<QByteArray>(bytes);
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &output)) {
        return infra::storageError("CredentialDecryptFailed");
    }
    const QByteArray secret(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return secret;
#else
    return infra::storageError("CredentialStoreUnsupported");
#endif
}
domain::CommandResult CredentialStore::write(const QString& reference, const QByteArray& secret) {
    const auto path = referencePath(reference);
    if (const auto* error = std::get_if<domain::Error>(&path)) {
        return *error;
    }
    if (secret.isEmpty() || secret.size() > 64 * 1024) {
        return infra::storageError("CredentialLimitExceeded");
    }
#ifdef Q_OS_WIN
    DATA_BLOB input{static_cast<DWORD>(secret.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(secret.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"QuickClash", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return infra::storageError("CredentialEncryptFailed");
    }
    const QByteArray encrypted(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return store_.writeBytes(std::get<QString>(path), encrypted);
#else
    return infra::storageError("CredentialStoreUnsupported");
#endif
}
domain::CommandResult CredentialStore::remove(const QString& reference) {
    const auto path = referencePath(reference);
    if (const auto* error = std::get_if<domain::Error>(&path)) {
        return *error;
    }
    return store_.remove(std::get<QString>(path));
}
} // namespace quickclash::platform
