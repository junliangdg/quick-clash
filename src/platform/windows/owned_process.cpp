#include "platform/windows/owned_process.h"
#include "infra/storage/json_store.h"
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <userenv.h>
#include <aclapi.h>
// clang-format on
#endif

namespace quickclash::platform {
#ifdef Q_OS_WIN
namespace {
class Handle final {
  public:
    ~Handle() {
        reset();
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(HANDLE value = nullptr) {
        if (value_ && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
        value_ = value;
    }
    [[nodiscard]] HANDLE get() const {
        return value_;
    }

  private:
    HANDLE value_ = nullptr;
};
QString quote(const QString& value) {
    QString output = QStringLiteral("\"");
    int slashes = 0;
    for (const auto character : value) {
        if (character == QLatin1Char('\\')) {
            ++slashes;
        } else {
            output += QString(slashes * (character == QLatin1Char('"') ? 2 : 1), QLatin1Char('\\'));
            if (character == QLatin1Char('"')) {
                output += QLatin1Char('\\');
            }
            output += character;
            slashes = 0;
        }
    }
    return output + QString(slashes * 2, QLatin1Char('\\')) + QLatin1Char('"');
}
bool grant(const QString& path, PSID sid, DWORD access, bool revoke = false) {
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = access;
    entry.grfAccessMode = revoke ? REVOKE_ACCESS : GRANT_ACCESS;
    entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    PACL old = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto* name = reinterpret_cast<LPCWSTR>(path.utf16());
    if (GetNamedSecurityInfoW(name, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              &old, nullptr, &descriptor) != ERROR_SUCCESS) {
        return false;
    }
    PACL acl = nullptr;
    const auto merged = SetEntriesInAclW(1, &entry, old, &acl);
    LocalFree(descriptor);
    if (merged != ERROR_SUCCESS) {
        return false;
    }
    const auto result =
        SetNamedSecurityInfoW(const_cast<LPWSTR>(name), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, acl, nullptr);
    LocalFree(acl);
    return result == ERROR_SUCCESS;
}
} // namespace
struct OwnedProcess::State {
    Handle job;
    Handle process;
    Handle output;
    quint32 pid = 0;
    QString profile;
    QString grantedDirectory;
    QString grantedExecutable;
    PSID sid = nullptr;
    ~State() {
        // Kill before removing the temporary security profile.
        job.reset();
        if (sid) {
            if (!grantedDirectory.isEmpty())
                (void)grant(grantedDirectory, sid, 0, true);
            if (!grantedExecutable.isEmpty())
                (void)grant(grantedExecutable, sid, 0, true);
            FreeSid(sid);
        }
        if (!profile.isEmpty()) {
            (void)DeleteAppContainerProfile(reinterpret_cast<LPCWSTR>(profile.utf16()));
        }
    }
};
#else
struct OwnedProcess::State {};
#endif
OwnedProcess::OwnedProcess(QObject* parent) : QObject(parent), state_(std::make_unique<State>()) {
    timer_.setInterval(50);
    connect(&timer_, &QTimer::timeout, this, &OwnedProcess::poll);
}
OwnedProcess::~OwnedProcess() {
    stop();
}
bool OwnedProcess::running() const {
#ifdef Q_OS_WIN
    return state_->process.get() != nullptr;
#else
    return false;
#endif
}
quint32 OwnedProcess::pid() const {
#ifdef Q_OS_WIN
    return state_->pid;
#else
    return 0;
#endif
}
domain::CommandResult OwnedProcess::start(const QString& executable, const QStringList& arguments,
                                          const QString& directory, bool isolated,
                                          const QProcessEnvironment& environment) {
    if (running()) {
        return infra::storageError("CoreAlreadyRunning");
    }
#ifdef Q_OS_WIN
    state_ = std::make_unique<State>();
    state_->job.reset(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!state_->job.get() ||
        !SetInformationJobObject(state_->job.get(), JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
        return infra::storageError("CoreJobFailed");
    }
    SECURITY_ATTRIBUTES inheritance{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = nullptr;
    HANDLE writer = nullptr;
    if (!CreatePipe(&output, &writer, &inheritance, 0)) {
        return infra::storageError("CorePipeFailed");
    }
    state_->output.reset(output);
    Handle writeHandle;
    writeHandle.reset(writer);
    if (!SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0)) {
        return infra::storageError("CorePipeFailed");
    }
    Handle input;
    input.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritance,
                            OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE) {
        return infra::storageError("CorePipeFailed");
    }
    SIZE_T attributeBytes = 0;
    const DWORD attributeCount = isolated ? 2 : 1;
    (void)InitializeProcThreadAttributeList(nullptr, attributeCount, 0, &attributeBytes);
    QByteArray attributes(static_cast<qsizetype>(attributeBytes), Qt::Uninitialized);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, attributeCount, 0, &attributeBytes)) {
        return infra::storageError("CoreProcessFailed");
    }
    struct Cleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~Cleanup() {
            DeleteProcThreadAttributeList(value);
        }
    } cleanup{list};
    SECURITY_CAPABILITIES capabilities{};
    if (isolated) {
        state_->grantedDirectory = directory;
        state_->grantedExecutable = executable;
        state_->profile = QStringLiteral("QuickClash.Validate.") +
                          QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto* name = reinterpret_cast<LPCWSTR>(state_->profile.utf16());
        if (FAILED(CreateAppContainerProfile(name, name, L"QuickClash offline validation", nullptr,
                                             0, &state_->sid)) ||
            !grant(directory, state_->sid, GENERIC_ALL) ||
            !grant(executable, state_->sid, GENERIC_READ | GENERIC_EXECUTE)) {
            state_ = std::make_unique<State>();
            return infra::storageError("CoreValidationIsolationFailed");
        }
        capabilities.AppContainerSid = state_->sid;
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                       &capabilities, sizeof(capabilities), nullptr, nullptr)) {
            state_ = std::make_unique<State>();
            return infra::storageError("CoreValidationIsolationFailed");
        }
    }
    HANDLE inherited[] = {writer, input.get()};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr)) {
        return infra::storageError("CoreProcessFailed");
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = writer;
    startup.StartupInfo.hStdError = writer;
    startup.lpAttributeList = list;
    QString command = quote(executable);
    for (const auto& argument : arguments) {
        command += QLatin1Char(' ') + quote(argument);
    }
    PROCESS_INFORMATION process{};
    auto variables = environment.toStringList();
    variables.sort(Qt::CaseInsensitive);
    auto environmentBlock = variables.join(QChar::Null);
    environmentBlock.append(QChar::Null);
    environmentBlock.append(QChar::Null);
    if (!CreateProcessW(reinterpret_cast<LPCWSTR>(executable.utf16()),
                        reinterpret_cast<LPWSTR>(command.data()), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT |
                            CREATE_UNICODE_ENVIRONMENT,
                        environmentBlock.data(), reinterpret_cast<LPCWSTR>(directory.utf16()),
                        &startup.StartupInfo, &process)) {
        return infra::storageError("CoreProcessFailed");
    }
    state_->process.reset(process.hProcess);
    Handle thread;
    thread.reset(process.hThread);
    if (!AssignProcessToJobObject(state_->job.get(), process.hProcess) ||
        ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        (void)TerminateProcess(process.hProcess, 1);
        (void)WaitForSingleObject(process.hProcess, 2000);
        state_ = std::make_unique<State>();
        return infra::storageError("CoreJobFailed");
    }
    state_->pid = process.dwProcessId;
    outputBytes_ = 0;
    timer_.start();
    return std::monostate{};
#else
    Q_UNUSED(executable)
    Q_UNUSED(arguments)
    Q_UNUSED(directory)
    Q_UNUSED(isolated)
    Q_UNUSED(environment)
    return infra::storageError("CorePlatformUnsupported");
#endif
}
void OwnedProcess::poll() {
#ifdef Q_OS_WIN
    if (!running()) {
        return;
    }
    // Discard raw core output: it may contain credentials or server addresses.
    // Drain at most 64 KiB per event to keep control operations responsive.
    DWORD available = 0;
    char buffer[4096];
    DWORD consumed = 0;
    while (consumed < 64 * 1024 &&
           PeekNamedPipe(state_->output.get(), nullptr, 0, nullptr, &available, nullptr) &&
           available) {
        DWORD read = 0;
        if (!ReadFile(state_->output.get(), buffer, std::min<DWORD>(available, sizeof(buffer)),
                      &read, nullptr) ||
            !read) {
            break;
        }
        consumed += read;
        outputBytes_ = std::min<qsizetype>(outputBytes_ + read, 1024 * 1024 * 1024);
    }
    if (WaitForSingleObject(state_->process.get(), 0) == WAIT_OBJECT_0) {
        DWORD exit = 1;
        (void)GetExitCodeProcess(state_->process.get(), &exit);
        timer_.stop();
        state_ = std::make_unique<State>();
        emit finished(static_cast<int>(exit));
    }
#endif
}
void OwnedProcess::stop() {
    timer_.stop();
#ifdef Q_OS_WIN
    if (running()) {
        (void)TerminateJobObject(state_->job.get(), 0);
        (void)WaitForSingleObject(state_->process.get(), 2000);
    }
#endif
    state_ = std::make_unique<State>();
}
bool OwnedProcess::ownsListener(quint16 port) const {
#ifdef Q_OS_WIN
    if (!running()) {
        return false;
    }
    ULONG size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) !=
        ERROR_INSUFFICIENT_BUFFER) {
        return false;
    }
    QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (GetExtendedTcpTable(bytes.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) !=
        NO_ERROR) {
        return false;
    }
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes.constData());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        if (ntohs(static_cast<u_short>(row.dwLocalPort)) == port && row.dwOwningPid == pid() &&
            row.dwLocalAddr == htonl(INADDR_LOOPBACK)) {
            return true;
        }
    }
#else
    Q_UNUSED(port)
#endif
    return false;
}
} // namespace quickclash::platform
