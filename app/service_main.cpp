#include "platform/windows/service_host.h"
#include <QCoreApplication>
#include <QDebug>
#include <memory>
#include <type_traits>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {
SERVICE_STATUS_HANDLE statusHandle = nullptr;
SERVICE_STATUS status{};
QCoreApplication* application = nullptr;
struct CloseService {
    void operator()(SC_HANDLE handle) const {
        CloseServiceHandle(handle);
    }
};
using ServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, CloseService>;
void migrateStartup() {
    // An already authorized automatic service can migrate itself without asking the GUI
    // for another UAC prompt. Change only the startup policy of this running SCM instance.
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    ServiceHandle service(manager ? OpenServiceW(manager.get(), L"QuickClashService",
                                                 SERVICE_QUERY_STATUS | SERVICE_CHANGE_CONFIG)
                                  : nullptr);
    SERVICE_STATUS_PROCESS current{};
    DWORD bytes = 0;
    if (!(service &&
          QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO,
                               reinterpret_cast<LPBYTE>(&current), sizeof(current), &bytes) &&
          current.dwProcessId == GetCurrentProcessId() &&
          ChangeServiceConfigW(service.get(), SERVICE_NO_CHANGE, SERVICE_DEMAND_START,
                               SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr)))
        qWarning() << "Cannot migrate the service to on-demand startup:" << GetLastError();
}
void report(DWORD state) {
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted =
        state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode = NO_ERROR;
    SetServiceStatus(statusHandle, &status);
}
void WINAPI control(DWORD code) {
    if ((code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) && application) {
        report(SERVICE_STOP_PENDING);
        QMetaObject::invokeMethod(application, &QCoreApplication::quit, Qt::QueuedConnection);
    }
}
void WINAPI service(DWORD, LPWSTR*) {
    statusHandle = RegisterServiceCtrlHandlerW(L"QuickClashService", control);
    if (!statusHandle)
        return;
    report(SERVICE_START_PENDING);
    int count = 1;
    char name[] = "quickclash_service";
    char* arguments[] = {name, nullptr};
    QCoreApplication app(count, arguments);
    application = &app;
    {
        quickclash::platform::ServiceHost host;
        if (!host.listen()) {
            report(SERVICE_STOPPED);
            application = nullptr;
            return;
        }
        QObject::connect(&host, &quickclash::platform::ServiceHost::exitRequested, &app, [&app] {
            report(SERVICE_STOP_PENDING);
            app.quit();
        });
        migrateStartup();
        report(SERVICE_RUNNING);
        app.exec();
        host.shutdown();
    }
    application = nullptr;
    report(SERVICE_STOPPED);
}
} // namespace
int main() {
    SERVICE_TABLE_ENTRYW table[]{{const_cast<LPWSTR>(L"QuickClashService"), service},
                                 {nullptr, nullptr}};
    return StartServiceCtrlDispatcherW(table) ? 0 : 1;
}
