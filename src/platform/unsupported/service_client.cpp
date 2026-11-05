#include "infra/core/service_client.h"
#include <QTimer>
#include <utility>

namespace quickclash::infra {
ServiceClient::ServiceClient(QObject* parent) : QObject(parent) {}
ServiceClient::~ServiceClient() = default;
bool ServiceClient::connected() const {
    return false;
}
bool ServiceClient::hadSession() const {
    return false;
}
void ServiceClient::command(const QString&, const QJsonObject&, Handler handler) {
    // Preserve the asynchronous callback and context lifetime without opening any IPC endpoint.
    const auto generation = closeGeneration_;
    QTimer::singleShot(0, this, [this, generation, handler = std::move(handler)] {
        if (generation != closeGeneration_)
            return;
        domain::Error error;
        error.category = domain::ErrorCategory::Platform;
        error.code = QStringLiteral("ServicePlatformUnsupported");
        handler(error);
    });
}
void ServiceClient::cancelQueuedDelays() {}
void ServiceClient::close() {
    ++closeGeneration_;
}
} // namespace quickclash::infra
