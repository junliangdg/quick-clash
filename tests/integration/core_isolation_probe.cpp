#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <aclapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <userenv.h>
#include <utility>
#include <vector>
#include <windows.h>
#include <winsock2.h>

namespace {
class Handle final {
  public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const {
        return value_;
    }

  private:
    HANDLE value_;
};
class Profile final {
  public:
    explicit Profile(std::wstring name) : name_(std::move(name)) {
        const auto result =
            CreateAppContainerProfile(name_.c_str(), name_.c_str(),
                                      L"QuickClash offline validation probe", nullptr, 0, &sid_);
        if (FAILED(result)) {
            throw std::runtime_error("CreateAppContainerProfile failed");
        }
    }
    ~Profile() {
        if (sid_) {
            FreeSid(sid_);
        }
        if (!deleted_) {
            (void)DeleteAppContainerProfile(name_.c_str());
        }
    }
    Profile(const Profile&) = delete;
    Profile& operator=(const Profile&) = delete;
    [[nodiscard]] PSID sid() const {
        return sid_;
    }
    void remove() {
        if (FAILED(DeleteAppContainerProfile(name_.c_str()))) {
            throw std::runtime_error("AppContainer profile cleanup failed");
        }
        deleted_ = true;
    }

  private:
    std::wstring name_;
    PSID sid_ = nullptr;
    bool deleted_ = false;
};
void grant(const std::filesystem::path& path, PSID sid) {
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = GENERIC_ALL;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_USER;
    entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    PACL oldAcl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                              nullptr, &oldAcl, nullptr, &descriptor) != ERROR_SUCCESS) {
        throw std::runtime_error("sandbox ACL read failed");
    }
    PACL acl = nullptr;
    const auto merge = SetEntriesInAclW(1, &entry, oldAcl, &acl);
    LocalFree(descriptor);
    if (merge != ERROR_SUCCESS) {
        throw std::runtime_error("sandbox ACL creation failed");
    }
    const auto result =
        SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
    LocalFree(acl);
    if (result != ERROR_SUCCESS) {
        throw std::runtime_error("sandbox ACL grant failed");
    }
}
DWORD run(const std::filesystem::path& binary, std::wstring arguments,
          const std::filesystem::path& directory, PSID sid, const std::filesystem::path& log) {
    SECURITY_ATTRIBUTES inheritance{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const Handle output(CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inheritance,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    const Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &inheritance, OPEN_EXISTING, 0, nullptr));
    if (output.get() == INVALID_HANDLE_VALUE || input.get() == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("probe output creation failed");
    }
    SIZE_T bytes = 0;
    (void)InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 2, 0, &bytes)) {
        throw std::runtime_error("attribute initialization failed");
    }
    struct AttributeCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeCleanup() {
            DeleteProcThreadAttributeList(value);
        }
    } cleanup{attributes};
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = sid;
    HANDLE inherited[] = {output.get(), input.get()};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                   &capabilities, sizeof(capabilities), nullptr, nullptr) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr)) {
        throw std::runtime_error("AppContainer attributes failed");
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = output.get();
    startup.StartupInfo.hStdError = output.get();
    startup.lpAttributeList = attributes;
    const Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.get() || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                               &limits, sizeof(limits))) {
        throw std::runtime_error("probe Job failed");
    }
    auto command = L"\"" + binary.wstring() + L"\" " + arguments;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                        directory.c_str(), &startup.StartupInfo, &process)) {
        throw std::runtime_error("AppContainer process creation failed: " +
                                 std::to_string(GetLastError()));
    }
    const Handle child(process.hProcess);
    const Handle thread(process.hThread);
    if (!AssignProcessToJobObject(job.get(), child.get())) {
        TerminateProcess(child.get(), 20);
        (void)WaitForSingleObject(child.get(), 5000);
        throw std::runtime_error("probe Job assignment failed");
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1) ||
        WaitForSingleObject(child.get(), 10000) != WAIT_OBJECT_0) {
        throw std::runtime_error("isolated process failed or exceeded 10s deadline");
    }
    DWORD exit = 0;
    if (!GetExitCodeProcess(child.get(), &exit)) {
        throw std::runtime_error("exit query failed");
    }
    return exit;
}
int networkCheck(unsigned short port) {
    const auto socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) {
        return 30;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    u_long nonblocking = 1;
    if (ioctlsocket(socketHandle, FIONBIO, &nonblocking) != 0) {
        closesocket(socketHandle);
        return 30;
    }
    const auto result =
        connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    auto error = WSAGetLastError();
    bool connected = result == 0;
    bool denied = result == SOCKET_ERROR && error == WSAEACCES;
    if (result == SOCKET_ERROR && error == WSAEWOULDBLOCK) {
        fd_set writable;
        fd_set exceptional;
        FD_ZERO(&writable);
        FD_ZERO(&exceptional);
        FD_SET(socketHandle, &writable);
        FD_SET(socketHandle, &exceptional);
        timeval timeout{1, 0};
        const auto selected = select(0, nullptr, &writable, &exceptional, &timeout);
        int size = sizeof(error);
        if (selected > 0 && getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                                       reinterpret_cast<char*>(&error), &size) == 0) {
            connected = error == 0;
            denied = error == WSAEACCES;
        } else if (selected == 0) {
            error = WSAETIMEDOUT;
            denied = true;
        }
    }
    closesocket(socketHandle);
    std::cout << "loopback connect result=" << result << " error=" << error << '\n';
    return connected ? 33 : (denied ? 0 : 31);
}
} // namespace
int wmain(int argc, wchar_t* argv[]) {
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0) {
        return 32;
    }
    struct SocketCleanup {
        ~SocketCleanup() {
            WSACleanup();
        }
    } socketCleanup;
    if (argc == 3 && std::wstring(argv[1]) == L"--network-check") {
        const Handle token([] {
            HANDLE value = nullptr;
            return OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &value) ? value : nullptr;
        }());
        DWORD appContainer = 0;
        DWORD returned = 0;
        if (!token.get() ||
            !GetTokenInformation(token.get(), TokenIsAppContainer, &appContainer,
                                 sizeof(appContainer), &returned) ||
            appContainer != 1) {
            return 34;
        }
        std::cout << "TokenIsAppContainer=1; zero capabilities supplied by parent\n";
        return networkCheck(static_cast<unsigned short>(std::stoul(argv[2])));
    }
    if (argc != 5) {
        std::cerr << "usage: core_isolation_probe core valid_config sandbox_directory "
                     "evidence_directory\n";
        return 2;
    }
    try {
        const auto directory = std::filesystem::absolute(argv[3]);
        const auto evidence = std::filesystem::absolute(argv[4]);
        // The caller owns this fresh temporary directory; never alter source/binary ACLs.
        if (!std::filesystem::create_directory(directory)) {
            throw std::runtime_error("sandbox directory must be new");
        }
        Profile profile(L"quickclash.probe." + std::to_wstring(GetCurrentProcessId()) + L"." +
                        std::to_wstring(GetTickCount64()));
        wchar_t executable[32768]{};
        if (GetModuleFileNameW(nullptr, executable, 32768) == 0) {
            return 3;
        }
        const auto core = directory / L"mihomo.exe";
        const auto helper = directory / L"network_probe.exe";
        std::filesystem::copy_file(argv[1], core);
        std::filesystem::copy_file(executable, helper);
        std::filesystem::copy_file(argv[2], directory / L"valid.yaml");
        std::ofstream(directory / L"invalid.yaml") << "proxies: [\n";
        grant(directory, profile.sid());
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            grant(entry.path(), profile.sid());
        }
        const auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) {
            throw std::runtime_error("listener creation failed");
        }
        struct ListenerCleanup {
            SOCKET value;
            ~ListenerCleanup() {
                closesocket(value);
            }
        } listenerCleanup{listener};
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int length = sizeof(address);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener, 4) != 0 ||
            getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            throw std::runtime_error("loopback fixture failed");
        }
        if (networkCheck(ntohs(address.sin_port)) != 33) {
            throw std::runtime_error("unrestricted loopback control failed");
        }
        const auto denied =
            run(helper, L"--network-check " + std::to_wstring(ntohs(address.sin_port)), directory,
                profile.sid(), evidence / L"network_denial.log");
        const auto valid = run(core, L"-t -d . -f valid.yaml", directory, profile.sid(),
                               evidence / L"isolated_valid.log");
        const auto invalid = run(core, L"-t -d . -f invalid.yaml", directory, profile.sid(),
                                 evidence / L"isolated_invalid.log");
        const auto files = static_cast<int>(std::distance(
            std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}));
        profile.remove();
        if (denied != 0 || valid != 0 || invalid != 1 || files != 4) {
            throw std::runtime_error("isolated checks failed: network=" + std::to_string(denied) +
                                     " valid=" + std::to_string(valid) + " invalid=" +
                                     std::to_string(invalid) + " files=" + std::to_string(files));
        }
        std::cout << "PASS: zero-capability AppContainer; loopback denied within 1s; "
                     "Mihomo valid=0 invalid=1; no new files; profile removed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
