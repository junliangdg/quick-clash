#include "platform/windows/service_host.h"
#include <QCoreApplication>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {
SERVICE_STATUS_HANDLE statusHandle = nullptr;
SERVICE_STATUS status{};
QCoreApplication* application = nullptr;
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
    quickclash::platform::ServiceHost host;
    if (!host.listen()) {
        report(SERVICE_STOPPED);
        application = nullptr;
        return;
    }
    report(SERVICE_RUNNING);
    app.exec();
    host.shutdown();
    application = nullptr;
    report(SERVICE_STOPPED);
}
} // namespace
int main() {
    SERVICE_TABLE_ENTRYW table[]{{const_cast<LPWSTR>(L"QuickClashService"), service},
                                 {nullptr, nullptr}};
    return StartServiceCtrlDispatcherW(table) ? 0 : 1;
}
