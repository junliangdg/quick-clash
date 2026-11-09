#include "platform/windows/service_installer.h"

#include "platform/windows/trusted_client_root.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUuid>
#include <cstdio>
#include <exception>
#include <memory>
#include <utility>

// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
// clang-format on

namespace quickclash::platform {
namespace {

constexpr auto kServiceName = L"QuickClashService";
constexpr auto kServiceDisplay = L"QuickClash TUN Service";
// The first authorization trusts the portable payload and subsequent user-controlled updates.
constexpr auto kDataSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
constexpr auto kFileSddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";
constexpr auto kServiceSddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;CCLCRCRPWP;;;BU)";
constexpr int kStartTimeoutMs = 20000;

struct SetupFailure {
    QString text;
};

class SetupLog {
  public:
    explicit SetupLog(QString file) : file_(std::move(file)) {
        if (!file_.isEmpty())
            QDir().mkpath(QFileInfo(file_).absolutePath());
    }
    [[nodiscard]] bool attached() const {
        return !file_.isEmpty();
    }
    void line(const QString& message) {
        const auto text = QStringLiteral("[%1] %2").arg(
            QDateTime::currentDateTime().toString(Qt::ISODate), message);
        if (!file_.isEmpty()) {
            QFile file(file_);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                file.write(text.toUtf8());
                file.write("\r\n");
                file.flush();
            }
        }
        std::fprintf(stderr, "%s\n", text.toUtf8().constData());
    }

  private:
    QString file_;
};

QString winError(DWORD code) {
    wchar_t* buffer = nullptr;
    const auto size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString result = size > 0 ? QString::fromWCharArray(buffer, static_cast<int>(size)).trimmed()
                              : QStringLiteral("error %1").arg(code);
    if (buffer)
        LocalFree(buffer);
    return result;
}

[[noreturn]] void fail(const QString& text) {
    throw SetupFailure{text};
}

void require(bool condition, const QString& text) {
    if (!condition)
        fail(text);
}

bool administrator() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID sid = nullptr;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid))
        return false;
    BOOL member = FALSE;
    CheckTokenMembership(nullptr, sid, &member);
    FreeSid(sid);
    return member != FALSE;
}

bool reparsePoint(const QString& path) {
    const auto attributes =
        GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

QString dataRoot() {
    const auto programData = qEnvironmentVariable("ProgramData");
    require(!programData.isEmpty(), QStringLiteral("ProgramData is not available."));
    return QDir(programData).absoluteFilePath(QStringLiteral("QuickClash"));
}

QString trustedRootFile() {
    return QDir(dataRoot()).filePath(QStringLiteral("trusted_client_root.json"));
}

QString serviceImage(const QString& root) {
    return QDir(root).filePath(QStringLiteral("quickclash_service.exe"));
}

QString quotedImage(const QString& image) {
    return QLatin1Char('"') + QDir::toNativeSeparators(image) + QLatin1Char('"');
}

void applySddl(const QString& path, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor,
                                                                 nullptr),
            QStringLiteral("Cannot parse the security descriptor."));
    BOOL present = FALSE;
    PACL dacl = nullptr;
    BOOL defaulted = FALSE;
    const auto described =
        GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) && present;
    DWORD status = ERROR_INVALID_SECURITY_DESCR;
    if (described) {
        status = SetNamedSecurityInfoW(
            const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16())),
            SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, dacl, nullptr);
    }
    LocalFree(descriptor);
    require(described && status == ERROR_SUCCESS,
            QStringLiteral("Cannot restrict permissions on %1: %2").arg(path, winError(status)));
}

void ensureDirectory(const QString& path, const wchar_t* sddl) {
    require(QDir().mkpath(path), QStringLiteral("Cannot create %1").arg(path));
    require(!reparsePoint(path), QStringLiteral("Refusing a reparse point: %1").arg(path));
    applySddl(path, sddl);
}

void writeTrustedRoot(const QString& root, SetupLog& log) {
    const auto trusted = makeTrustedClientRoot(root);
    require(trusted.has_value(),
            QStringLiteral("The client folder cannot be authorized: %1").arg(root));
    const auto directory = dataRoot();
    ensureDirectory(directory, kDataSddl);
    const auto target = trustedRootFile();
    const auto temporary =
        QDir(directory).filePath(QStringLiteral("trusted_client_root.%1.tmp")
                                     .arg(QUuid::createUuid().toString(QUuid::Id128)));
    QFile file(temporary);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            QStringLiteral("Cannot write %1").arg(temporary));
    const QJsonObject payload{{QStringLiteral("schemaVersion"), 1},
                              {QStringLiteral("clientRoot"), trusted->root}};
    file.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    file.close();
    try {
        applySddl(temporary, kFileSddl);
    } catch (...) {
        QFile::remove(temporary);
        throw;
    }
    require(MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(temporary).utf16()),
                        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
            QStringLiteral("Cannot replace %1: %2").arg(target, winError(GetLastError())));
    log.line(QStringLiteral("Authorized client folder: %1").arg(trusted->root));
}

class ScmHandle {
  public:
    ScmHandle() = default;
    explicit ScmHandle(SC_HANDLE handle) : handle_(handle) {}
    ScmHandle(const ScmHandle&) = delete;
    ScmHandle& operator=(const ScmHandle&) = delete;
    ~ScmHandle() {
        if (handle_)
            CloseServiceHandle(handle_);
    }
    [[nodiscard]] SC_HANDLE get() const {
        return handle_;
    }
    explicit operator bool() const {
        return handle_ != nullptr;
    }

  private:
    SC_HANDLE handle_ = nullptr;
};

ScmHandle openScm(DWORD access) {
    const auto handle = OpenSCManagerW(nullptr, nullptr, access);
    require(handle != nullptr,
            QStringLiteral("Cannot open the service manager: %1").arg(winError(GetLastError())));
    return ScmHandle(handle);
}

ScmHandle openService(SC_HANDLE scm, DWORD access, bool required) {
    const auto handle = OpenServiceW(scm, kServiceName, access);
    if (!handle && required)
        fail(
            QStringLiteral("Cannot open the QuickClash service: %1").arg(winError(GetLastError())));
    return ScmHandle(handle);
}

DWORD serviceState(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    require(QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status),
                                 sizeof(status), &bytes),
            QStringLiteral("Cannot query the service state: %1").arg(winError(GetLastError())));
    return status.dwCurrentState;
}

DWORD serviceProcessId(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status),
                              sizeof(status), &bytes))
        return 0;
    return status.dwProcessId;
}

void waitForState(SC_HANDLE service, DWORD desired, SetupLog& log) {
    const auto deadline = QDateTime::currentMSecsSinceEpoch() + kStartTimeoutMs;
    for (;;) {
        const auto state = serviceState(service);
        if (state == desired) {
            log.line(QStringLiteral("Service reached the expected state."));
            return;
        }
        if (state == SERVICE_STOPPED && desired != SERVICE_STOPPED)
            fail(QStringLiteral("The service stopped while starting."));
        if (QDateTime::currentMSecsSinceEpoch() > deadline)
            fail(QStringLiteral("Timed out waiting for the service to reach state %1.")
                     .arg(desired));
        Sleep(200);
    }
}

void startService(SC_HANDLE service, SetupLog& log) {
    if (serviceState(service) == SERVICE_RUNNING) {
        log.line(QStringLiteral("Service already running."));
        return;
    }
    if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
        fail(QStringLiteral("Cannot start the QuickClash service: %1")
                 .arg(winError(GetLastError())));
    waitForState(service, SERVICE_RUNNING, log);
}

void stopService(SC_HANDLE service, SetupLog& log) {
    const auto state = serviceState(service);
    if (state == SERVICE_STOPPED)
        return;
    // SCM reports SERVICE_STOPPED before the process has necessarily released its image and
    // DLLs. An elevated migration can wait for complete release before replacing registration.
    // Ordinary updates use SCM state only; their token cannot synchronize a SYSTEM process.
    const auto pid = serviceProcessId(service);
    HANDLE process = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    if (pid && !process)
        log.line(QStringLiteral("Warning: cannot wait for service process %1: %2")
                     .arg(pid)
                     .arg(winError(GetLastError())));
    SERVICE_STATUS status{};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        if (process)
            CloseHandle(process);
        fail(
            QStringLiteral("Cannot stop the QuickClash service: %1").arg(winError(GetLastError())));
    }
    try {
        waitForState(service, SERVICE_STOPPED, log);
    } catch (...) {
        if (process)
            CloseHandle(process);
        throw;
    }
    if (process) {
        log.line(QStringLiteral("Waiting for service process %1 to exit.").arg(pid));
        const auto exited = WaitForSingleObject(process, kStartTimeoutMs);
        CloseHandle(process);
        require(exited == WAIT_OBJECT_0,
                QStringLiteral("Timed out waiting for the service process to exit."));
    }
}

QString registrationImage(SC_HANDLE service) {
    DWORD needed = 0;
    QueryServiceConfigW(service, nullptr, 0, &needed);
    require(
        needed > 0,
        QStringLiteral("Cannot read the service configuration: %1").arg(winError(GetLastError())));
    QByteArray buffer(static_cast<int>(needed), 0);
    auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    require(
        QueryServiceConfigW(service, config, needed, &needed),
        QStringLiteral("Cannot read the service configuration: %1").arg(winError(GetLastError())));
    const auto account = config->lpServiceStartName
                             ? QString::fromWCharArray(config->lpServiceStartName)
                             : QString{};
    require(account.isEmpty() ||
                account.compare(QStringLiteral("LocalSystem"), Qt::CaseInsensitive) == 0,
            QStringLiteral("The registered service does not run as LocalSystem."));
    return config->lpBinaryPathName ? QString::fromWCharArray(config->lpBinaryPathName) : QString{};
}

void requireFixedRegistration(SC_HANDLE service, const QString& root) {
    const auto expected = quotedImage(serviceImage(root));
    const auto actual = registrationImage(service);
    require(actual.compare(expected, Qt::CaseInsensitive) == 0,
            QStringLiteral("The registered service does not point at %1").arg(expected));
}

void servicePermissions(SC_HANDLE service) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(kServiceSddl, SDDL_REVISION_1,
                                                                 &descriptor, nullptr),
            QStringLiteral("Cannot parse service permissions."));
    const auto ok = SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor);
    require(ok, QStringLiteral("Cannot set service permissions."));
}

void validate(const ServiceSetupRequest& request, SetupLog& log) {
    const auto trusted = makeTrustedClientRoot(request.clientRoot);
    require(trusted.has_value(), QStringLiteral("This portable folder cannot be authorized."));
    const QFileInfo image(serviceImage(trusted->root));
    require(image.isFile() && !reparsePoint(image.absoluteFilePath()),
            QStringLiteral("quickclash_service.exe is missing or linked."));
    require(QFileInfo(portableCorePath(trusted->root)).isFile(),
            QStringLiteral("The portable Mihomo core is missing."));
    log.line(QStringLiteral("Portable service: %1").arg(image.absoluteFilePath()));
    log.line(QStringLiteral("Shared core: %1").arg(portableCorePath(trusted->root)));
}
void removeLegacyPayload(const QString& portableRoot, SetupLog& log) {
    const auto programFiles = qEnvironmentVariable("ProgramFiles");
    if (programFiles.isEmpty())
        return;
    const QDir parent(programFiles);
    const auto entries =
        parent.entryList({QStringLiteral("QuickClash"), QStringLiteral("QuickClash_previous_*")},
                         QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        if (entry != QStringLiteral("QuickClash") &&
            (entry.size() != 52 || QUuid(entry.mid(20)).isNull()))
            continue;
        const auto path = QDir::cleanPath(parent.absoluteFilePath(entry));
        if (!path.startsWith(parent.absolutePath() + QLatin1Char('/'), Qt::CaseInsensitive) ||
            portableRoot.startsWith(path + QLatin1Char('/'), Qt::CaseInsensitive) ||
            portableRoot.compare(path, Qt::CaseInsensitive) == 0 || reparsePoint(path) ||
            !QFileInfo::exists(QDir(path).filePath(QStringLiteral("bin/quickclash_service.exe"))))
            continue;
        bool safe = true;
        int count = 0;
        QDirIterator files(path,
                           QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto file = files.next();
            const auto top = QDir(path).relativeFilePath(file).section(QLatin1Char('/'), 0, 0);
            if (++count > 10000 || reparsePoint(file) ||
                (top != QStringLiteral("bin") && top != QStringLiteral("core") &&
                 top != QStringLiteral("geo")) ||
                QFileInfo(file).fileName().compare(QStringLiteral("quickclash.exe"),
                                                   Qt::CaseInsensitive) == 0) {
                safe = false;
                break;
            }
        }
        if (!safe || !QDir(path).removeRecursively())
            log.line(QStringLiteral("Legacy payload retained for manual inspection: %1").arg(path));
        else
            log.line(QStringLiteral("Removed obsolete service payload: %1").arg(path));
    }
}

void configure(const ServiceSetupRequest& request, SetupLog& log) {
    validate(request, log);
    const auto trusted = *makeTrustedClientRoot(request.clientRoot);
    auto scm = openScm(SC_MANAGER_CONNECT | (administrator() ? SC_MANAGER_CREATE_SERVICE : 0));
    auto service = openService(scm.get(),
                               administrator() ? SERVICE_ALL_ACCESS
                                               : SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS |
                                                     SERVICE_START | SERVICE_STOP,
                               false);
    if (!administrator()) {
        require(static_cast<bool>(service),
                QStringLiteral("First authorization requires administrator rights."));
        requireFixedRegistration(service.get(), trusted.root);
        stopService(service.get(), log);
        startService(service.get(), log);
        return;
    }
    const auto command = quotedImage(serviceImage(trusted.root)).toStdWString();
    QFile previousFile(trustedRootFile());
    const bool previousExists = previousFile.exists();
    require(!previousExists || previousFile.open(QIODevice::ReadOnly),
            QStringLiteral("Cannot read existing authorization."));
    const auto previous = previousExists ? previousFile.readAll() : QByteArray{};
    previousFile.close();
    QString oldCommand;
    DWORD oldStart = SERVICE_DEMAND_START;
    bool wasRunning = false;
    bool created = false;
    if (service) {
        oldCommand = registrationImage(service.get());
        DWORD bytes = 0;
        QueryServiceConfigW(service.get(), nullptr, 0, &bytes);
        require(bytes > 0 && bytes <= 64 * 1024,
                QStringLiteral("Invalid service configuration size."));
        QByteArray buffer(static_cast<qsizetype>(bytes), Qt::Uninitialized);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        require(QueryServiceConfigW(service.get(), config, bytes, &bytes),
                QStringLiteral("Cannot read service configuration."));
        oldStart = config->dwStartType;
        wasRunning = serviceState(service.get()) != SERVICE_STOPPED;
        stopService(service.get(), log);
    }
    try {
        writeTrustedRoot(trusted.root, log);
        if (service) {
            require(ChangeServiceConfigW(service.get(), SERVICE_WIN32_OWN_PROCESS,
                                         SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                                         command.c_str(), nullptr, nullptr, nullptr, L"LocalSystem",
                                         nullptr, kServiceDisplay),
                    QStringLiteral("Cannot update portable service registration."));
            servicePermissions(service.get());
            startService(service.get(), log);
        } else {
            ScmHandle fresh(CreateServiceW(
                scm.get(), kServiceName, kServiceDisplay, SERVICE_ALL_ACCESS,
                SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr));
            require(static_cast<bool>(fresh),
                    QStringLiteral("Cannot register the portable service."));
            created = true;
            servicePermissions(fresh.get());
            startService(fresh.get(), log);
        }
    } catch (...) {
        log.line(QStringLiteral("Restoring previous registration and authorization."));
        auto rollback = openService(scm.get(), SERVICE_ALL_ACCESS, false);
        if (rollback) {
            try {
                stopService(rollback.get(), log);
            } catch (...) {
            }
            if (created)
                DeleteService(rollback.get());
            else if (!oldCommand.isEmpty())
                ChangeServiceConfigW(rollback.get(), SERVICE_NO_CHANGE, oldStart, SERVICE_NO_CHANGE,
                                     oldCommand.toStdWString().c_str(), nullptr, nullptr, nullptr,
                                     nullptr, nullptr, nullptr);
        }
        if (previousExists) {
            QFile restored(trustedRootFile());
            if (restored.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                restored.write(previous);
                restored.close();
                applySddl(trustedRootFile(), kFileSddl);
            }
        } else
            QFile::remove(trustedRootFile());
        if (rollback && wasRunning)
            StartServiceW(rollback.get(), 0, nullptr);
        throw;
    }
    log.line(QStringLiteral(
        "On-demand service registered directly in the portable folder; no payload copied."));
    removeLegacyPayload(trusted.root, log);
}

void uninstall(SetupLog& log) {
    auto scm = openScm(SC_MANAGER_CONNECT);
    if (auto service = openService(scm.get(), SERVICE_ALL_ACCESS, false)) {
        stopService(service.get(), log);
        require(DeleteService(service.get()),
                QStringLiteral("Cannot remove service registration."));
    }
    require(!QFileInfo::exists(trustedRootFile()) || QFile::remove(trustedRootFile()),
            QStringLiteral("Cannot remove portable authorization."));
    log.line(
        QStringLiteral("Service registration and authorization removed; portable files retained."));
}

QString actionName(ServiceSetupAction action) {
    switch (action) {
    case ServiceSetupAction::Install:
        return QStringLiteral("install");
    case ServiceSetupAction::Update:
        return QStringLiteral("update");
    case ServiceSetupAction::Uninstall:
        return QStringLiteral("uninstall");
    case ServiceSetupAction::Authorize:
        return QStringLiteral("authorize");
    case ServiceSetupAction::Validate:
        return QStringLiteral("validate");
    }
    return QStringLiteral("unknown");
}

bool parseAction(const QString& name, ServiceSetupAction* action) {
    if (name == QStringLiteral("install"))
        *action = ServiceSetupAction::Install;
    else if (name == QStringLiteral("update"))
        *action = ServiceSetupAction::Update;
    else if (name == QStringLiteral("uninstall"))
        *action = ServiceSetupAction::Uninstall;
    else if (name == QStringLiteral("authorize"))
        *action = ServiceSetupAction::Authorize;
    else if (name == QStringLiteral("validate"))
        *action = ServiceSetupAction::Validate;
    else
        return false;
    return true;
}

} // namespace

ServiceSetupInvocation parseServiceSetupCommandLine(const QStringList& arguments) {
    ServiceSetupInvocation invocation;
    for (int index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments.at(index);
        QString value;
        auto takeValue = [&](bool inlineValue) {
            if (inlineValue)
                return true;
            if (index + 1 >= arguments.size())
                return false;
            value = arguments.at(++index);
            return true;
        };
        if (argument == QStringLiteral("--service-setup") ||
            argument.startsWith(QStringLiteral("--service-setup="))) {
            invocation.present = true;
            if (argument.contains(QLatin1Char('='))) {
                value = argument.section(QLatin1Char('='), 1);
            } else if (!takeValue(false)) {
                invocation.valid = false;
                invocation.problem = QStringLiteral("--service-setup requires an action.");
                continue;
            }
            if (!parseAction(value, &invocation.request.action)) {
                invocation.valid = false;
                invocation.problem = QStringLiteral("Unsupported setup action: %1").arg(value);
            }
        } else if (argument == QStringLiteral("--client-root") ||
                   argument.startsWith(QStringLiteral("--client-root="))) {
            if (argument.contains(QLatin1Char('=')))
                value = argument.section(QLatin1Char('='), 1);
            else if (!takeValue(false)) {
                invocation.valid = false;
                invocation.problem = QStringLiteral("--client-root requires a folder.");
                continue;
            }
            invocation.request.clientRoot = value;
        } else if (argument == QStringLiteral("--log") ||
                   argument.startsWith(QStringLiteral("--log="))) {
            if (argument.contains(QLatin1Char('=')))
                value = argument.section(QLatin1Char('='), 1);
            else if (!takeValue(false)) {
                invocation.valid = false;
                invocation.problem = QStringLiteral("--log requires a file.");
                continue;
            }
            invocation.request.logFile = value;
        }
    }
    return invocation;
}

int runServiceSetup(const ServiceSetupRequest& request) {
    SetupLog log(request.logFile);
    try {
        log.line(QStringLiteral("QuickClash setup started: %1").arg(actionName(request.action)));
        log.line(QStringLiteral("Client folder: %1").arg(request.clientRoot));
        const auto elevated = administrator();
        log.line(QStringLiteral("Administrator: %1")
                     .arg(elevated ? QStringLiteral("yes") : QStringLiteral("no")));
        if (request.action != ServiceSetupAction::Validate &&
            request.action != ServiceSetupAction::Update && !elevated)
            fail(QStringLiteral("This action requires administrator rights."));
        switch (request.action) {
        case ServiceSetupAction::Install:
            configure(request, log);
            break;
        case ServiceSetupAction::Update:
            configure(request, log);
            break;
        case ServiceSetupAction::Uninstall:
            uninstall(log);
            break;
        case ServiceSetupAction::Authorize:
            configure(request, log);
            break;
        case ServiceSetupAction::Validate:
            validate(request, log);
            break;
        }
        log.line(QStringLiteral("QuickClash setup finished: %1").arg(actionName(request.action)));
        return 0;
    } catch (const SetupFailure& failure) {
        log.line(QStringLiteral("Setup failed: %1").arg(failure.text));
        return 1;
    } catch (const std::exception& error) {
        log.line(QStringLiteral("Setup failed: %1").arg(QString::fromUtf8(error.what())));
        return 1;
    }
}

} // namespace quickclash::platform
