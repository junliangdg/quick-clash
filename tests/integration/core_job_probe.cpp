#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <windows.h>

namespace {
class Handle {
  public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept {
        return value_;
    }

  private:
    HANDLE value_;
};
} // namespace
int wmain(int argc, wchar_t* argv[]) {
    if (argc != 5) {
        std::cerr << "usage: core_job_probe core config directory pid_file\n";
        return 2;
    }
    const Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.get() || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                               &limits, sizeof(limits))) {
        return 3;
    }
    std::wstring command =
        L"\"" + std::wstring(argv[1]) + L"\" -f \"" + argv[2] + L"\" -d \"" + argv[3] + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, argv[3], &startup,
                        &process)) {
        return 4;
    }
    const Handle child(process.hProcess);
    const Handle thread(process.hThread);
    if (!AssignProcessToJobObject(job.get(), child.get())) {
        TerminateProcess(child.get(), 5);
        WaitForSingleObject(child.get(), 5000);
        return 5;
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        return 6;
    }
    std::ofstream pidFile{std::filesystem::path(argv[4])};
    if (!pidFile) {
        return 8;
    }
    pidFile << process.dwProcessId;
    pidFile.close();
    const auto wait = WaitForSingleObject(child.get(), 60000);
    return wait == WAIT_OBJECT_0 ? 0 : 7;
}
