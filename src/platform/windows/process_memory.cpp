#include "platform/process_memory.h"
#include <QCoreApplication>
#include <QDir>
#include <memory>
#include <type_traits>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace quickclash::platform {
#ifdef Q_OS_WIN
namespace {
struct CloseProcess {
    void operator()(void* handle) const {
        CloseHandle(handle);
    }
};
struct CloseService {
    void operator()(SC_HANDLE handle) const {
        CloseServiceHandle(handle);
    }
};
using ProcessHandle = std::unique_ptr<void, CloseProcess>;
using ServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, CloseService>;
quint64 creation(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return 0;
    return (quint64{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}
QJsonObject sample(HANDLE process) {
    DWORD exitCode = 0;
    PROCESS_MEMORY_COUNTERS_EX2 counters{};
    counters.cb = sizeof(counters);
    if (!GetExitCodeProcess(process, &exitCode) || exitCode != STILL_ACTIVE)
        return {};
    if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        PROCESS_MEMORY_COUNTERS_EX older{};
        older.cb = sizeof(older);
        if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&older),
                                  sizeof(older)))
            return {};
        counters.WorkingSetSize = older.WorkingSetSize;
        counters.PrivateUsage = older.PrivateUsage;
    }
    const auto pid = GetProcessId(process);
    const auto created = creation(process);
    if (!pid || !created)
        return {};
    QJsonObject result{{QStringLiteral("pid"), QString::number(pid)},
                       {QStringLiteral("created"), QString::number(created)},
                       {QStringLiteral("workingSet"), QString::number(counters.WorkingSetSize)},
                       {QStringLiteral("privateCommit"), QString::number(counters.PrivateUsage)}};
    // Older Windows builds may accept EX2 but leave this field unset.
    if (counters.PrivateWorkingSetSize)
        result.insert(QStringLiteral("privateWorkingSet"),
                      QString::number(counters.PrivateWorkingSetSize));
    return result;
}
} // namespace
#endif
ProcessIdentity processIdentity(quint32 pid) {
#ifdef Q_OS_WIN
    ProcessHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (process)
        return {pid, creation(process.get())};
#else
    Q_UNUSED(pid)
#endif
    return {};
}
QJsonObject processMemory(ProcessIdentity identity) {
#ifdef Q_OS_WIN
    if (!identity.pid || !identity.created)
        return {};
    ProcessHandle process(
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, identity.pid));
    if (process && creation(process.get()) == identity.created)
        return sample(process.get());
#else
    Q_UNUSED(identity)
#endif
    return {};
}
QJsonObject currentProcessMemory() {
#ifdef Q_OS_WIN
    return sample(GetCurrentProcess());
#else
    return {};
#endif
}
ServiceRegistration installedServiceRegistration(quint32 pid) {
#ifdef Q_OS_WIN
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    ServiceHandle service(manager ? OpenServiceW(manager.get(), L"QuickClashService",
                                                 SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS)
                                  : nullptr);
    if (!service)
        return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? ServiceRegistration::NotInstalled
                                                              : ServiceRegistration::Mismatch;
    SERVICE_STATUS_PROCESS before{}, after{};
    DWORD size = 0;
    if (!QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<BYTE*>(&before), sizeof(before), &size) ||
        before.dwServiceType != SERVICE_WIN32_OWN_PROCESS)
        return ServiceRegistration::Mismatch;
    DWORD needed = 0;
    QueryServiceConfigW(service.get(), nullptr, 0, &needed);
    if (!needed || needed > 64 * 1024)
        return ServiceRegistration::Mismatch;
    QByteArray buffer(static_cast<qsizetype>(needed), Qt::Uninitialized);
    auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    if (!QueryServiceConfigW(service.get(), config, needed, &needed) ||
        QString::fromWCharArray(config->lpServiceStartName)
                .compare(QStringLiteral("LocalSystem"), Qt::CaseInsensitive) != 0)
        return ServiceRegistration::Mismatch;
    const auto directory = QCoreApplication::applicationDirPath();
    const auto expected = QDir::toNativeSeparators(
        QDir(directory).filePath(QStringLiteral("quickclash_service.exe")));
    // The reviewed installer uses one quoted executable with no arguments.
    if (QString::fromWCharArray(config->lpBinaryPathName)
            .compare('"' + expected + '"', Qt::CaseInsensitive) != 0)
        return ServiceRegistration::Mismatch;
    if (before.dwCurrentState != SERVICE_RUNNING)
        return ServiceRegistration::Stopped;
    if (pid && before.dwProcessId != pid)
        return ServiceRegistration::Mismatch;
    if (!pid)
        return ServiceRegistration::Verified;
    if (!QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<BYTE*>(&after), sizeof(after), &size) ||
        after.dwCurrentState != SERVICE_RUNNING || after.dwProcessId != pid)
        return ServiceRegistration::Mismatch;
    return ServiceRegistration::Verified;
#else
    Q_UNUSED(pid)
    return ServiceRegistration::Unavailable;
#endif
}
QJsonObject installedServiceMemory() {
    QJsonObject result{{QStringLiteral("complete"), false}};
#ifdef Q_OS_WIN
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager)
        return result;
    ServiceHandle service(OpenServiceW(manager.get(), L"QuickClashService", SERVICE_QUERY_STATUS));
    if (!service) {
        result.insert(QStringLiteral("complete"), GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST);
        return result;
    }
    const auto status = [&service](SERVICE_STATUS_PROCESS& value) {
        DWORD size = 0;
        return QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
                                    reinterpret_cast<BYTE*>(&value), sizeof(value), &size);
    };
    SERVICE_STATUS_PROCESS before{};
    if (!status(before))
        return result;
    if (before.dwCurrentState == SERVICE_STOPPED) {
        result.insert(QStringLiteral("complete"), true);
        return result;
    }
    if (before.dwCurrentState != SERVICE_RUNNING || !before.dwProcessId)
        return result;
    ProcessHandle process(
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, before.dwProcessId));
    if (!process)
        return result;
    wchar_t path[32768]{};
    DWORD length = 32768;
    HANDLE tokenValue = nullptr;
    if (!QueryFullProcessImageNameW(process.get(), 0, path, &length) ||
        !OpenProcessToken(process.get(), TOKEN_QUERY, &tokenValue))
        return result;
    ProcessHandle token(tokenValue);
    QByteArray data(256, Qt::Uninitialized);
    DWORD size = 0;
    const bool system =
        GetTokenInformation(token.get(), TokenUser, data.data(), static_cast<DWORD>(data.size()),
                            &size) &&
        IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, WinLocalSystemSid);
    const auto expected = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("quickclash_service.exe"));
    if (!system || QDir::cleanPath(QString::fromWCharArray(path, static_cast<int>(length)))
                           .compare(expected, Qt::CaseInsensitive) != 0)
        return result;
    const auto memory = sample(process.get());
    SERVICE_STATUS_PROCESS after{};
    if (!status(after) || after.dwCurrentState != SERVICE_RUNNING ||
        after.dwProcessId != before.dwProcessId || memory.isEmpty())
        return result;
    result.insert(QStringLiteral("service"), memory);
    result.insert(QStringLiteral("complete"), true);
#endif
    return result;
}
bool startRegisteredService() {
    const auto registration = installedServiceRegistration(0);
    if (registration == ServiceRegistration::Verified)
        return true;
    if (registration != ServiceRegistration::Stopped)
        return false;
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    ServiceHandle service(manager ? OpenServiceW(manager.get(), L"QuickClashService",
                                                 SERVICE_START | SERVICE_QUERY_STATUS)
                                  : nullptr);
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    if (!service ||
        !QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes))
        return false;
    // A previous ordinary stop may still be completing. The client retries until STOPPED,
    // then starts the same authorized registration, without treating a transition as UAC work.
    if (status.dwCurrentState == SERVICE_START_PENDING ||
        status.dwCurrentState == SERVICE_STOP_PENDING || status.dwCurrentState == SERVICE_RUNNING)
        return true;
    return StartServiceW(service.get(), 0, nullptr) ||
           GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
}
} // namespace quickclash::platform
