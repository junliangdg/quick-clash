#pragma once
#include <QJsonObject>

namespace quickclash::platform {
struct ProcessIdentity {
    quint32 pid = 0;
    quint64 created = 0;
};
enum class ServiceRegistration { Unavailable, NotInstalled, Stopped, Mismatch, Verified };
[[nodiscard]] ProcessIdentity processIdentity(quint32 pid);
[[nodiscard]] QJsonObject processMemory(ProcessIdentity identity);
[[nodiscard]] QJsonObject currentProcessMemory();
// Verifies a pipe peer against the administrator-owned SCM registration without opening
// a LocalSystem process/token, which Windows may deny to an ordinary user. The fixed
// service image next to the current GUI must match the authorized SCM path.
// A zero pid inspects the registration itself, which is enough to tell a missing service
// from a stopped one before opening the pipe. A non-zero pid must also own the service.
[[nodiscard]] ServiceRegistration installedServiceRegistration(quint32 pid);
// Starts only a registration already verified against this portable directory; no UAC.
[[nodiscard]] bool startRegisteredService();
// SCM determines the only permitted service PID; image and LocalSystem identity are checked.
[[nodiscard]] QJsonObject installedServiceMemory();
} // namespace quickclash::platform
