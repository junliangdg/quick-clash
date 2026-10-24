#include "native_proxy_client.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <wininet.h>
// clang-format on
#endif
namespace quickclash::test {
#ifdef Q_OS_WIN
namespace {
struct InternetHandle {
    HINTERNET value;
    ~InternetHandle() {
        if (value)
            InternetCloseHandle(value);
    }
};
} // namespace
#endif
bool nativeProxyRequest(const QString& host, quint16 port) {
#ifdef Q_OS_WIN
    InternetHandle session{
        InternetOpenW(L"QuickClashFixture", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0)};
    if (!session.value)
        return false;
    DWORD timeout = 5000;
    InternetSetOptionW(session.value, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(session.value, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetHandle connection{InternetConnectW(session.value,
                                               reinterpret_cast<LPCWSTR>(host.utf16()), port,
                                               nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0)};
    if (!connection.value)
        return false;
    InternetHandle request{HttpOpenRequestW(
        connection.value, L"GET", L"/system-client", nullptr, nullptr, nullptr,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
            INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_NO_UI,
        0)};
    if (!request.value)
        return false;
    // Allow this public fixture CA on this request only. Name/time validation remains active.
    // Product TLS policy and OS trust store are untouched.
    DWORD flags = 0, size = sizeof(flags);
    if (!InternetQueryOptionW(request.value, INTERNET_OPTION_SECURITY_FLAGS, &flags, &size))
        return false;
    flags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    if (!InternetSetOptionW(request.value, INTERNET_OPTION_SECURITY_FLAGS, &flags, sizeof(flags)) ||
        !HttpSendRequestW(request.value, nullptr, 0, nullptr, 0))
        return false;
    DWORD status = 0;
    size = sizeof(status);
    return HttpQueryInfoW(request.value, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status,
                          &size, nullptr) &&
           status == 200;
#else
    Q_UNUSED(host)
    Q_UNUSED(port)
    return false;
#endif
}
bool terminateOwnedFixture(quint32 pid) {
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process)
        return false;
    const bool result = TerminateProcess(process, 99);
    CloseHandle(process);
    return result;
#else
    Q_UNUSED(pid)
    return false;
#endif
}
} // namespace quickclash::test
