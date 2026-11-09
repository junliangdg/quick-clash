#include "platform/windows/owned_process.h"
#include "domain/log_buffer.h"
#include "infra/storage/json_store.h"
#include "platform/desktop_services.h"
#include <QTimer>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on
#include <QWinEventNotifier>
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
} // namespace
struct OwnedProcess::State {
    Handle job;
    Handle process;
    Handle output;
    Handle outputEvent;
    OVERLAPPED read{};
    char buffer[4096]{};
    bool readPending = false;
    bool outputClosed = false;
    bool assignedToJob = false;
    std::unique_ptr<QWinEventNotifier> processNotifier;
    std::unique_ptr<QWinEventNotifier> outputNotifier;
    quint32 pid = 0;
    ProcessIdentity identity;
    ~State() {
        processNotifier.reset();
        outputNotifier.reset();
        // The Job owns the child and is also closed if the parent exits.
        job.reset();
        if (readPending) {
            (void)CancelIoEx(output.get(), &read);
            DWORD ignored = 0;
            // OVERLAPPED and its buffer must outlive cancellation completion.
            (void)GetOverlappedResult(output.get(), &read, &ignored, TRUE);
        }
    }
};
#else
struct OwnedProcess::State {};
#endif
OwnedProcess::OwnedProcess(QObject* parent)
    : IDesktopProcess(parent), state_(std::make_unique<State>()) {}
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
QJsonObject OwnedProcess::memory() const {
#ifdef Q_OS_WIN
    return processMemory(state_->identity);
#else
    return {};
#endif
}
std::unique_ptr<application::IDesktopProcess> makeDesktopProcess() {
    return std::make_unique<OwnedProcess>();
}
domain::CommandResult OwnedProcess::start(const QString& executable, const QStringList& arguments,
                                          const QString& directory, bool validation) {
    return start(executable, arguments, directory, validation,
                 QProcessEnvironment::systemEnvironment());
}
domain::CommandResult OwnedProcess::start(const QString& executable, const QStringList& arguments,
                                          const QString& directory, bool validation,
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
    const auto pipeName = QStringLiteral("\\\\.\\pipe\\QuickClash.Output.") +
                          QUuid::createUuid().toString(QUuid::WithoutBraces);
    state_->output.reset(CreateNamedPipeW(
        reinterpret_cast<LPCWSTR>(pipeName.utf16()),
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096,
        0, nullptr));
    if (state_->output.get() == INVALID_HANDLE_VALUE)
        return infra::storageError("CorePipeFailed");
    Handle writeHandle;
    writeHandle.reset(CreateFileW(reinterpret_cast<LPCWSTR>(pipeName.utf16()), GENERIC_WRITE, 0,
                                  &inheritance, OPEN_EXISTING, 0, nullptr));
    state_->outputEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (writeHandle.get() == INVALID_HANDLE_VALUE || !state_->outputEvent.get()) {
        return infra::storageError("CorePipeFailed");
    }
    state_->read.hEvent = state_->outputEvent.get();
    if (!ConnectNamedPipe(state_->output.get(), &state_->read) &&
        GetLastError() != ERROR_PIPE_CONNECTED)
        return infra::storageError("CorePipeFailed");
    Handle input;
    input.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritance,
                            OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE) {
        return infra::storageError("CorePipeFailed");
    }
    SIZE_T attributeBytes = 0;
    const DWORD attributeCount = 1;
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
    auto variablesEnvironment = environment;
    if (validation) {
        // Explicit command-line/config values own validation. Environment
        // overrides must not replace the candidate, enable controllers or run hooks.
        for (const auto& name : variablesEnvironment.keys())
            if (name.startsWith(QStringLiteral("CLASH_"), Qt::CaseInsensitive))
                variablesEnvironment.remove(name);
        // Keep Mihomo's own home-directory path checks even when the caller has
        // configured broader access for an unrelated core process.
        variablesEnvironment.remove(QStringLiteral("SAFE_PATHS"));
        variablesEnvironment.remove(QStringLiteral("SKIP_SAFE_PATH_CHECK"));
        // Provider validation uses a private pipe owned by this user. Normal
        // service launches keep their explicitly supplied System-only ACL.
        variablesEnvironment.insert(QStringLiteral("LISTEN_NAMEDPIPE_SDDL"),
                                    QStringLiteral("D:P(A;;GA;;;SY)(A;;GA;;;OW)"));
    }
    HANDLE inherited[] = {writeHandle.get(), input.get()};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr)) {
        return infra::storageError("CoreProcessFailed");
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = writeHandle.get();
    startup.StartupInfo.hStdError = writeHandle.get();
    startup.lpAttributeList = list;
    QString command = quote(executable);
    for (const auto& argument : arguments) {
        command += QLatin1Char(' ') + quote(argument);
    }
    PROCESS_INFORMATION process{};
    auto variables = variablesEnvironment.toStringList();
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
    state_->pid = process.dwProcessId;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(process.hProcess, &created, &exited, &kernel, &user))
        state_->identity = {process.dwProcessId,
                            (quint64{created.dwHighDateTime} << 32) | created.dwLowDateTime};
    Handle thread;
    thread.reset(process.hThread);
    state_->assignedToJob = AssignProcessToJobObject(state_->job.get(), process.hProcess);
    if (!state_->assignedToJob || ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        const auto stopped = stop();
        if (std::holds_alternative<domain::Error>(stopped))
            return stopped;
        return infra::storageError("CoreJobFailed");
    }
    state_->pid = process.dwProcessId;
    outputBytes_ = 0;
    outputTail_.clear();
    exitCode_ = 0;
    exitedAt_ = {};
    state_->processNotifier = std::make_unique<QWinEventNotifier>(state_->process.get());
    connect(state_->processNotifier.get(), &QWinEventNotifier::activated, this, [this] {
        state_->processNotifier->setEnabled(false);
        QTimer::singleShot(0, this, &OwnedProcess::poll);
    });
    state_->outputNotifier = std::make_unique<QWinEventNotifier>(state_->outputEvent.get());
    connect(state_->outputNotifier.get(), &QWinEventNotifier::activated, this,
            [this] { drainOutput(); });
    drainOutput();
    return std::monostate{};
#else
    Q_UNUSED(executable)
    Q_UNUSED(arguments)
    Q_UNUSED(directory)
    Q_UNUSED(validation)
    Q_UNUSED(environment)
    return infra::storageError("CorePlatformUnsupported");
#endif
}
void OwnedProcess::poll() {
#ifdef Q_OS_WIN
    if (!running()) {
        return;
    }
    drainOutput();
    if (WaitForSingleObject(state_->process.get(), 0) == WAIT_OBJECT_0) {
        // Capture the final bytes that may have arrived between the first drain and exit.
        drainOutput();
        DWORD exit = 1;
        (void)GetExitCodeProcess(state_->process.get(), &exit);
        exitCode_ = static_cast<int>(exit);
        exitedAt_ = QDateTime::currentDateTimeUtc();
        state_ = std::make_unique<State>();
        emit finished(exitCode_);
    }
#endif
}
void OwnedProcess::drainOutput() {
#ifdef Q_OS_WIN
    if (!running() || state_->outputClosed || !state_->outputNotifier)
        return;
    state_->outputNotifier->setEnabled(false);
    // Only completion events wake the worker. Yield after 64 KiB under a noisy child.
    DWORD consumed = 0;
    while (consumed < 64 * 1024) {
        DWORD read = 0;
        if (state_->readPending) {
            if (!GetOverlappedResult(state_->output.get(), &state_->read, &read, FALSE)) {
                if (GetLastError() == ERROR_IO_INCOMPLETE) {
                    state_->outputNotifier->setEnabled(true);
                    return;
                }
                state_->readPending = false;
                state_->outputClosed = true;
                return;
            }
            state_->readPending = false;
        } else {
            ResetEvent(state_->outputEvent.get());
            if (!ReadFile(state_->output.get(), state_->buffer, sizeof(state_->buffer), &read,
                          &state_->read)) {
                if (GetLastError() == ERROR_IO_PENDING) {
                    state_->readPending = true;
                    state_->outputNotifier->setEnabled(true);
                } else
                    state_->outputClosed = true;
                return;
            }
        }
        if (!read) {
            state_->outputClosed = true;
            return;
        }
        consumed += read;
        outputTail_.append(state_->buffer, static_cast<qsizetype>(read));
        if (outputTail_.size() > 4096)
            outputTail_ = outputTail_.right(4096);
        outputBytes_ = std::min<qsizetype>(outputBytes_ + read, 1024 * 1024 * 1024);
    }
    const auto pid = state_->pid;
    QTimer::singleShot(0, this, [this, pid] {
        if (running() && state_->pid == pid)
            drainOutput();
    });
#endif
}
domain::CommandResult OwnedProcess::stop() {
#ifdef Q_OS_WIN
    if (running()) {
        const auto observed = WaitForSingleObject(state_->process.get(), 0);
        if (observed != WAIT_OBJECT_0) {
            const bool terminated = state_->assignedToJob
                                        ? TerminateJobObject(state_->job.get(), 0)
                                        : TerminateProcess(state_->process.get(), 1);
            if (!terminated)
                return infra::storageError("CoreStopFailed");
            const auto waited = WaitForSingleObject(state_->process.get(), 2000);
            if (waited != WAIT_OBJECT_0)
                return infra::storageError(waited == WAIT_TIMEOUT ? "CoreStopTimeout"
                                                                  : "CoreStopFailed");
        }
        drainOutput();
        DWORD code = 0;
        if (GetExitCodeProcess(state_->process.get(), &code))
            exitCode_ = static_cast<int>(code);
        exitedAt_ = QDateTime::currentDateTimeUtc();
    }
#endif
    state_ = std::make_unique<State>();
    return std::monostate{};
}
QJsonObject OwnedProcess::exitDiagnostics() const {
    if (!exitedAt_.isValid())
        return {};
    const auto safe =
        domain::redactedLogEntry(0, {}, QStringLiteral("Core"), QStringLiteral("error"),
                                 QString::fromUtf8(outputTail_), true);
    return {{QStringLiteral("coreExitCode"), static_cast<qint64>(static_cast<quint32>(exitCode_))},
            {QStringLiteral("coreExitedAt"), exitedAt_.toString(Qt::ISODateWithMs)},
            {QStringLiteral("coreStderr"), safe.value(QStringLiteral("message"))}};
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
    QByteArray bytes;
    DWORD result = ERROR_INSUFFICIENT_BUFFER;
    // The table can grow between sizing and reading as other applications open sockets.
    for (int attempt = 0; attempt < 3 && result == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
        if (!size || size > 16 * 1024 * 1024)
            return false;
        bytes.resize(static_cast<qsizetype>(size));
        result = GetExtendedTcpTable(bytes.data(), &size, FALSE, AF_INET,
                                     TCP_TABLE_OWNER_PID_LISTENER, 0);
    }
    if (result != NO_ERROR) {
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
