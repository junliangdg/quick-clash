#pragma once
#include <QJsonObject>

namespace quickclash::platform {
struct ProcessIdentity {
    quint32 pid = 0;
    quint64 created = 0;
};
[[nodiscard]] ProcessIdentity processIdentity(quint32 pid);
[[nodiscard]] QJsonObject processMemory(ProcessIdentity identity);
[[nodiscard]] QJsonObject currentProcessMemory();
// Verifies a pipe peer against the administrator-owned SCM registration without opening
// a LocalSystem process/token, which Windows may deny to an ordinary user.
[[nodiscard]] bool installedServiceMatches(quint32 pid);
// SCM determines the only permitted service PID; image and LocalSystem identity are checked.
[[nodiscard]] QJsonObject installedServiceMemory();
} // namespace quickclash::platform
