#include "platform/process_memory.h"

namespace quickclash::platform {
ProcessIdentity processIdentity(quint32) {
    return {};
}
QJsonObject processMemory(ProcessIdentity) {
    return {};
}
QJsonObject currentProcessMemory() {
    return {};
}
bool installedServiceMatches(quint32) {
    return false;
}
QJsonObject installedServiceMemory() {
    return {{QStringLiteral("complete"), false}};
}
} // namespace quickclash::platform
