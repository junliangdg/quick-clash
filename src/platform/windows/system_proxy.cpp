#include "platform/windows/system_proxy.h"
#include <QDateTime>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <wininet.h>
// clang-format on
#endif

namespace quickclash::platform {
namespace {
const QString kLease = QStringLiteral("data/leases/windows_lan.json");
bool valid(const QJsonObject& value) {
    const auto flags = value.value(QStringLiteral("flags"));
    return flags.isDouble() && flags.toInt(-1) >= 0 && flags.toInt() <= 15 &&
           value.value(QStringLiteral("server")).isString() &&
           value.value(QStringLiteral("bypass")).isString() &&
           value.value(QStringLiteral("pac")).isString() &&
           value.value(QStringLiteral("server")).toString().size() <= 32768 &&
           value.value(QStringLiteral("bypass")).toString().size() <= 32768 &&
           value.value(QStringLiteral("pac")).toString().size() <= 32768;
}
bool validLease(const QJsonObject& lease) {
    return lease.value(QStringLiteral("scope")) == QStringLiteral("windows_current_user_lan") &&
           !QUuid(lease.value(QStringLiteral("leaseId")).toString()).isNull() &&
           valid(lease.value(QStringLiteral("before")).toObject()) &&
           valid(lease.value(QStringLiteral("applied")).toObject()) &&
           (!lease.contains(QStringLiteral("previousApplied")) ||
            valid(lease.value(QStringLiteral("previousApplied")).toObject()));
}
} // namespace
domain::Result<QJsonObject> WindowsProxySettings::read() {
#ifdef Q_OS_WIN
    INTERNET_PER_CONN_OPTIONW options[4]{};
    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
    INTERNET_PER_CONN_OPTION_LISTW list{};
    list.dwSize = sizeof(list);
    list.dwOptionCount = 4;
    list.pOptions = options;
    DWORD size = sizeof(list);
    if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size)) {
        return infra::storageError("SystemProxyReadFailed");
    }
    QJsonObject result{{QStringLiteral("flags"), static_cast<int>(options[0].Value.dwValue)}};
    const char* keys[] = {"server", "bypass", "pac"};
    for (int i = 1; i < 4; ++i) {
        result.insert(QLatin1String(keys[i - 1]),
                      options[i].Value.pszValue ? QString::fromWCharArray(options[i].Value.pszValue)
                                                : QString{});
        if (options[i].Value.pszValue)
            GlobalFree(options[i].Value.pszValue);
    }
    if (!valid(result))
        return infra::storageError("SystemProxySnapshotInvalid");
    return result;
#else
    return infra::storageError("SystemProxyUnsupported");
#endif
}
domain::CommandResult WindowsProxySettings::write(const QJsonObject& snapshot) {
    if (!valid(snapshot))
        return infra::storageError("SystemProxySnapshotInvalid");
#ifdef Q_OS_WIN
    const auto server = snapshot.value(QStringLiteral("server")).toString();
    const auto bypass = snapshot.value(QStringLiteral("bypass")).toString();
    const auto pac = snapshot.value(QStringLiteral("pac")).toString();
    INTERNET_PER_CONN_OPTIONW options[4]{};
    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[0].Value.dwValue = static_cast<DWORD>(snapshot.value(QStringLiteral("flags")).toInt());
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[1].Value.pszValue = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(server.utf16()));
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    options[2].Value.pszValue = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(bypass.utf16()));
    options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
    options[3].Value.pszValue = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(pac.utf16()));
    INTERNET_PER_CONN_OPTION_LISTW list{};
    list.dwSize = sizeof(list);
    list.dwOptionCount = 4;
    list.pOptions = options;
    if (!InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, sizeof(list))) {
        return infra::storageError("SystemProxyWriteFailed");
    }
    const bool notified =
        InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0) &&
        InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
    const auto observed = read();
    if (!notified || !std::holds_alternative<QJsonObject>(observed) ||
        std::get<QJsonObject>(observed) != snapshot) {
        return infra::storageError("SystemProxyReadbackFailed");
    }
    return std::monostate{};
#else
    return infra::storageError("SystemProxyUnsupported");
#endif
}
ProxyLease::ProxyLease(infra::JsonStore& store, ProxySettings& settings)
    : store_(store), settings_(settings) {}
ProxyLease::ProxyLease(infra::JsonStore& store) : store_(store), settings_(defaultSettings_) {}
domain::CommandResult ProxyLease::acquire(quint16 port, const QString& owner) {
    if (port < 1024 || owner.isEmpty())
        return infra::storageError("InvalidProxyTarget");
    const auto loaded = store_.read(kLease);
    if (const auto* error = std::get_if<domain::Error>(&loaded))
        return *error;
    auto lease = std::get<std::optional<QJsonObject>>(loaded).value_or(QJsonObject{});
    if (!lease.isEmpty() && !validLease(lease))
        return infra::storageError("ProxyLeaseRecoveryRequired");
    const auto observed = settings_.read();
    if (const auto* error = std::get_if<domain::Error>(&observed))
        return *error;
    const auto current = std::get<QJsonObject>(observed);
    const auto target = QStringLiteral("http=127.0.0.1:%1;https=127.0.0.1:%1").arg(port);
    const QJsonObject applied{
        {QStringLiteral("flags"), 3},
        {QStringLiteral("server"), target},
        {QStringLiteral("bypass"), QStringLiteral("localhost;127.0.0.1;[::1];<local>")},
        {QStringLiteral("pac"), QString{}}};
    if (!lease.isEmpty() && lease.value(QStringLiteral("status")) != QStringLiteral("Released")) {
        const bool suspended =
            lease.value(QStringLiteral("status")) == QStringLiteral("Suspended") &&
            current == lease.value(QStringLiteral("before")).toObject();
        if (!suspended && current != lease.value(QStringLiteral("applied")).toObject()) {
            active_ = false;
            conflict_ = true;
            return infra::storageError("SystemProxyConflict");
        }
        if (current == applied) {
            active_ = true;
            conflict_ = false;
            return std::monostate{};
        }
        lease.insert(QStringLiteral("previousApplied"), current);
    } else {
        lease = {
            {QStringLiteral("leaseId"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("scope"), QStringLiteral("windows_current_user_lan")},
            {QStringLiteral("before"), current},
            {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    }
    lease.insert(QStringLiteral("ownerSession"), owner);
    lease.insert(QStringLiteral("applied"), applied);
    lease.insert(QStringLiteral("status"), QStringLiteral("Prepared"));
    auto result = store_.write(kLease, lease);
    if (std::holds_alternative<domain::Error>(result))
        return result;
    // Minimize the read/write race; the OS has no compare-and-swap for these settings.
    const auto recheck = settings_.read();
    if (!std::holds_alternative<QJsonObject>(recheck) ||
        std::get<QJsonObject>(recheck) != current) {
        conflict_ = true;
        return infra::storageError("SystemProxyConflict");
    }
    result = settings_.write(applied);
    if (std::holds_alternative<domain::Error>(result)) {
        // A failed notification/readback can follow a successful OS write.
        // The persisted intent permits release only if the value is still ours.
        const auto restored = release();
        Q_UNUSED(restored)
        return result;
    }
    active_ = true;
    conflict_ = false;
    lease.insert(QStringLiteral("status"), QStringLiteral("Applied"));
    lease.remove(QStringLiteral("previousApplied"));
    return store_.write(kLease, lease);
}
domain::CommandResult ProxyLease::suspend() {
    const auto loaded = store_.read(kLease);
    if (const auto* error = std::get_if<domain::Error>(&loaded))
        return *error;
    const auto document = std::get<std::optional<QJsonObject>>(loaded);
    if (!document || !active_)
        return std::monostate{};
    auto lease = *document;
    if (!validLease(lease))
        return infra::storageError("ProxyLeaseRecoveryRequired");
    const auto current = settings_.read();
    if (!std::holds_alternative<QJsonObject>(current) ||
        std::get<QJsonObject>(current) != lease.value(QStringLiteral("applied")).toObject()) {
        active_ = false;
        conflict_ = true;
        return infra::storageError("SystemProxyConflict");
    }
    lease.insert(QStringLiteral("status"), QStringLiteral("Suspending"));
    auto result = store_.write(kLease, lease);
    if (std::holds_alternative<domain::Error>(result))
        return result;
    const auto recheck = settings_.read();
    if (!std::holds_alternative<QJsonObject>(recheck) ||
        std::get<QJsonObject>(recheck) != std::get<QJsonObject>(current)) {
        active_ = false;
        conflict_ = true;
        return infra::storageError("SystemProxyConflict");
    }
    result = settings_.write(lease.value(QStringLiteral("before")).toObject());
    if (std::holds_alternative<domain::Error>(result))
        return result;
    active_ = false;
    lease.insert(QStringLiteral("status"), QStringLiteral("Suspended"));
    return store_.write(kLease, lease);
}
domain::CommandResult ProxyLease::reapply(quint16 port, const QString& owner) {
    if (!conflict_ || active_)
        return infra::storageError("SystemProxyConflictRequired");
    const auto loaded = store_.read(kLease);
    if (!std::holds_alternative<std::optional<QJsonObject>>(loaded))
        return std::get<domain::Error>(loaded);
    auto lease = std::get<std::optional<QJsonObject>>(loaded).value_or(QJsonObject{});
    if (!validLease(lease))
        return infra::storageError("ProxyLeaseRecoveryRequired");
    // Explicit user action abandons the old claim. Acquire snapshots the current
    // external value as the new before image and rechecks before writing.
    lease.insert(QStringLiteral("status"), QStringLiteral("Released"));
    const auto saved = store_.write(kLease, lease);
    if (std::holds_alternative<domain::Error>(saved))
        return saved;
    return acquire(port, owner);
}
domain::CommandResult ProxyLease::release() {
    active_ = false;
    const auto loaded = store_.read(kLease);
    if (const auto* error = std::get_if<domain::Error>(&loaded))
        return *error;
    const auto document = std::get<std::optional<QJsonObject>>(loaded);
    if (!document)
        return std::monostate{};
    auto lease = *document;
    if (!validLease(lease))
        return infra::storageError("ProxyLeaseRecoveryRequired");
    if (lease.value(QStringLiteral("status")) == QStringLiteral("Released"))
        return std::monostate{};
    const auto observed = settings_.read();
    if (const auto* error = std::get_if<domain::Error>(&observed))
        return *error;
    const auto current = std::get<QJsonObject>(observed);
    const auto before = lease.value(QStringLiteral("before")).toObject();
    if (current != before) {
        if (current != lease.value(QStringLiteral("applied")).toObject() &&
            !(lease.value(QStringLiteral("status")) == QStringLiteral("Prepared") &&
              current == lease.value(QStringLiteral("previousApplied")).toObject())) {
            conflict_ = true;
            return infra::storageError("SystemProxyConflict");
        }
        lease.insert(QStringLiteral("status"), QStringLiteral("Releasing"));
        // Restoration is already authorized by the durable acquisition intent.
        // A full/read-only disk must not leave an owned proxy pointing at a stopped core.
        const auto intent = store_.write(kLease, lease);
        Q_UNUSED(intent)
        const auto recheck = settings_.read();
        if (!std::holds_alternative<QJsonObject>(recheck) ||
            std::get<QJsonObject>(recheck) != current) {
            conflict_ = true;
            return infra::storageError("SystemProxyConflict");
        }
        const auto result = settings_.write(before);
        if (std::holds_alternative<domain::Error>(result))
            return result;
    }
    lease.insert(QStringLiteral("status"), QStringLiteral("Released"));
    conflict_ = false;
    return store_.write(kLease, lease);
}
domain::Result<bool> ProxyLease::safeToArchive() {
    if (active_ || conflict_)
        return false;
    const auto loaded = store_.read(kLease);
    if (const auto* failure = std::get_if<domain::Error>(&loaded))
        return *failure;
    const auto lease = std::get<std::optional<QJsonObject>>(loaded);
    if (!lease)
        return true;
    if (!validLease(*lease))
        return infra::storageError("ProxyLeaseRecoveryRequired");
    if (lease->value(QStringLiteral("status")) == QStringLiteral("Released"))
        return true;
    const auto observed = settings_.read();
    if (const auto* failure = std::get_if<domain::Error>(&observed))
        return *failure;
    return std::get<QJsonObject>(observed) == lease->value(QStringLiteral("before")).toObject();
}
domain::Result<bool> ProxyLease::matches() {
    if (!active_)
        return false;
    const auto loaded = store_.read(kLease);
    if (const auto* error = std::get_if<domain::Error>(&loaded))
        return *error;
    const auto lease = std::get<std::optional<QJsonObject>>(loaded);
    const auto observed = settings_.read();
    if (const auto* error = std::get_if<domain::Error>(&observed))
        return *error;
    if (!lease || !validLease(*lease))
        return infra::storageError("ProxyLeaseRecoveryRequired");
    if (std::get<QJsonObject>(observed) != lease->value(QStringLiteral("applied")).toObject()) {
        active_ = false;
        conflict_ = true;
        return false;
    }
    return true;
}
} // namespace quickclash::platform
