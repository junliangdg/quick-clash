#include "platform/core_archive.h"
namespace quickclash::platform {
struct CoreArchive::State {};
CoreArchive::CoreArchive(QObject* parent) : QObject(parent), state_(std::make_unique<State>()) {}
CoreArchive::~CoreArchive() = default;
void CoreArchive::cancel() {}
bool CoreArchive::running() const {
    return false;
}
QJsonObject CoreArchive::memory() const {
    return {};
}
void CoreArchive::extract(const QString&, const QString&, const QString&,
                          std::function<void(domain::CommandResult)> completion) {
    domain::Error error;
    error.code = QStringLiteral("CorePlatformUnsupported");
    completion(error);
}
} // namespace quickclash::platform
