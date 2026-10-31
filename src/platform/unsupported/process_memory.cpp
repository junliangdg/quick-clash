#include "platform/process_memory.h"

namespace quickclash::platform {
bool startRegisteredService() {
    return false;
}
ProcessIdentity processIdentity(quint32) {
    return {};
}
QJsonObject processMemory(ProcessIdentity) {
    return {};
}
QJsonObject currentProcessMemory() {
    return {};
}
ServiceRegistration installedServiceRegistration(quint32) {
    return ServiceRegistration::Unavailable;
}
QJsonObject installedServiceMemory() {
    return {{QStringLiteral("complete"), false}};
}
} // namespace quickclash::platform
