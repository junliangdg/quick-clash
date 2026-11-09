#include "infra/core/service_client.h"
#include "infra/core/service_protocol.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/json_store.h"
#include "platform/process_memory.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QUuid>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace quickclash::infra {
namespace {
// A slow status or feature request must not tear down the authenticated session. Losing the
// only owner connection makes the service stop the core for safety, which previously turned a
// transient control-plane timeout into the user-visible CoreExited/TUN restart loop.
constexpr int kRequestTimeoutMs = 15000;
// The core probe has a 2-second deadline. Allow transport and service scheduling slack,
// but do not let a stuck delay request occupy a slot for the full control-plane deadline.
constexpr int kDelayRequestTimeoutMs = 5000;
} // namespace

ServiceClient::ServiceClient(QObject* parent) : QObject(parent) {
    socket_.setReadBufferSize(kReadChunkSize);
    deadline_.setSingleShot(true);
    deadline_.setInterval(kRequestTimeoutMs);
    heartbeat_.setInterval(10000);
    writeTimer_.setSingleShot(true);
    writeTimer_.setTimerType(Qt::PreciseTimer);
    connect(&writeTimer_, &QTimer::timeout, this, &ServiceClient::sendPendingCommands);
    // This timer only bounds connection and handshake. Once verified, every request owns its
    // timeout so one unanswered response does not abort the session.
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (!verified_)
            fail("ServiceTimeout");
    });
    connect(&heartbeat_, &QTimer::timeout, this, [this] {
        if (heartbeatPending_ || (activity_.isValid() && activity_.elapsed() < 10000))
            return;
        heartbeatPending_ = true;
        const auto generation = closeGeneration_;
        command(QStringLiteral("heartbeat"), {}, [this, generation](domain::Result<QJsonObject>) {
            if (generation == closeGeneration_)
                heartbeatPending_ = false;
        });
    });
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        // Refresh on every handshake: the bundled service can be replaced while this GUI
        // remains open. Reusing its constructor-time digest would reject the updated service.
        expectedServiceBuild_ = ServiceProtocol::executableBuild(
            QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("quickclash_service.exe")));
#ifdef Q_OS_WIN
        ULONG pid = 0;
        if (!GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket_.socketDescriptor()),
                                         &pid)) {
            fail("ServiceIdentityRejected");
            return;
        }
        switch (platform::installedServiceRegistration(pid)) {
        case platform::ServiceRegistration::Verified:
            break;
        case platform::ServiceRegistration::NotInstalled:
            fail("ServiceInstallationRequired");
            return;
        case platform::ServiceRegistration::Stopped:
            fail("ServiceNotRunning");
            return;
        case platform::ServiceRegistration::Unavailable:
            fail("ServiceUnavailable");
            return;
        case platform::ServiceRegistration::Mismatch:
            fail("ServiceIdentityRejected");
            return;
        }
#endif
        handshakeId_ = QString::number(++nextId_);
        socket_.write(QJsonDocument(QJsonObject{{QStringLiteral("protocolVersion"), 1},
                                                {QStringLiteral("requestId"), handshakeId_},
                                                {QStringLiteral("sessionId"), QString{}},
                                                {QStringLiteral("engineEpoch"), QString{}},
                                                {QStringLiteral("action"), QStringLiteral("hello")},
                                                {QStringLiteral("payload"), QJsonObject{}}})
                          .toJson(QJsonDocument::Compact) +
                      '\n');
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        // A service that closes the pipe before completing hello is not usable by this client.
        // Treat that as a protocol/version problem so switching TUN on can offer the one
        // administrator update instead of a dead-end "unavailable" message. After a verified
        // handshake the same event means the live session was lost.
        fail(!verified_ && !handshakeId_.isEmpty() ? "ServiceProtocolMismatch"
                                                   : "ServiceDisconnected");
    });
    connect(&socket_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError error) {
                if (error == QLocalSocket::ServerNotFoundError)
                    fail("ServiceInstallationRequired");
                else if (error == QLocalSocket::ConnectionRefusedError)
                    fail("ServiceNotRunning");
                else if (error == QLocalSocket::PeerClosedError && !verified_ &&
                         !handshakeId_.isEmpty())
                    fail("ServiceProtocolMismatch");
                else
                    fail("ServiceUnavailable");
            });
    connect(&socket_, &QIODevice::readyRead, this, [this] {
        if (!appendAvailable(socket_, input_, 3 * 1024 * 1024)) {
            fail("ServiceResponseLimit");
            return;
        }
        qsizetype newline;
        while ((newline = input_.indexOf('\n')) >= 0) {
            const auto result = QJsonDocument::fromJson(input_.first(newline)).object();
            input_.remove(0, newline + 1);
            if (!ServiceProtocol::validResponse(result)) {
                fail("ServiceProtocolMismatch");
                return;
            }
            if (!verified_) {
                if (result.value(QStringLiteral("requestId")) == handshakeId_) {
                    const auto handshakeError = result.value(QStringLiteral("error")).toString();
                    if (!handshakeError.isEmpty()) {
                        const auto accepted =
                            handshakeError == QStringLiteral("ServiceSessionBusy") ||
                            handshakeError == QStringLiteral("ServiceClientUntrusted") ||
                            handshakeError == QStringLiteral("ServiceProtocolMismatch");
                        fail(accepted ? handshakeError.toLatin1().constData()
                                      : "ServiceIdentityRejected");
                        return;
                    }
                }
                auto capabilities = result.value(QStringLiteral("result")).toObject();
                const auto session = capabilities.take(QStringLiteral("sessionId")).toString();
                if (result.value(QStringLiteral("requestId")) != handshakeId_ ||
                    QUuid(session).isNull()) {
                    fail("ServiceProtocolMismatch");
                    return;
                }
                const auto error =
                    ServiceProtocol::capabilityError(capabilities, expectedServiceBuild_);
                if (!error.isEmpty()) {
                    fail(error.toLatin1().constData());
                    return;
                }
                verified_ = true;
                everVerified_ = true;
                sessionId_ = session;
                handshakeId_.clear();
                deadline_.stop();
                sendPendingCommands();
                heartbeat_.start();
                continue;
            }
            auto handler = handlers_.take(result.value(QStringLiteral("requestId")).toString());
            if (!handler)
                continue;
            const auto error = result.value(QStringLiteral("error")).toString();
            if (error.isEmpty()) {
                const auto value = result.value(QStringLiteral("result")).toObject();
                if (value.contains(QStringLiteral("epoch")))
                    epoch_ = value.value(QStringLiteral("epoch")).toString();
                handler(result.value(QStringLiteral("result")).toObject());
            } else {
                auto failure = storageError(error.toLatin1().constData());
                if (result.contains(QStringLiteral("detail")))
                    failure.redactedDetail = QString::fromUtf8(
                        QJsonDocument(result.value(QStringLiteral("detail")).toObject())
                            .toJson(QJsonDocument::Compact));
                handler(failure);
            }
        }
        if (handlers_.isEmpty())
            deadline_.stop();
    });
}
ServiceClient::~ServiceClient() {
    // QLocalSocket can emit disconnected from its destructor. Close it while the session
    // members still exist, without invoking client callbacks during destruction.
    socket_.disconnect(this);
    close();
}
bool ServiceClient::connected() const {
    return verified_ && socket_.state() == QLocalSocket::ConnectedState;
}
bool ServiceClient::hadSession() const {
    return everVerified_;
}
void ServiceClient::command(const QString& action, const QJsonObject& payload, Handler handler) {
    if (handlers_.size() >= kServiceRequestLimit) {
        handler(storageError("ServiceBusy"));
        return;
    }
    const auto id = QString::number(++nextId_);
    const QJsonObject message{
        {QStringLiteral("protocolVersion"), 1},    {QStringLiteral("requestId"), id},
        {QStringLiteral("sessionId"), sessionId_}, {QStringLiteral("engineEpoch"), epoch_},
        {QStringLiteral("action"), action},        {QStringLiteral("payload"), payload}};
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > 24 * 1024 * 1024) {
        handler(storageError("ServiceRequestLimit"));
        return;
    }
    handlers_.insert(id, std::move(handler));
    pendingWrites_.append(bytes);
    if (connected()) {
        sendPendingCommands();
    } else if (socket_.state() == QLocalSocket::UnconnectedState) {
        connectService();
    }
    if (!verified_ && !deadline_.isActive())
        deadline_.start();
}
void ServiceClient::sendPendingCommands() {
    if (!connected() || failing_ || pendingWrites_.isEmpty())
        return;
    const auto remaining =
        writeClock_.isValid() ? kServiceRequestSpacingMs - writeClock_.elapsed() : 0;
    if (remaining > 0) {
        writeTimer_.start(static_cast<int>(remaining));
        return;
    }
    // Keep request IDs in FIFO order. The bounded handlers_ ledger includes queued commands.
    auto message = QJsonDocument::fromJson(pendingWrites_.takeFirst()).object();
    message.insert(QStringLiteral("sessionId"), sessionId_);
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (socket_.write(bytes) != bytes.size()) {
        fail("ServiceUnavailable");
        return;
    }
    writeClock_.start();
    activity_.start();
    const auto id = message.value(QStringLiteral("requestId")).toString();
    const auto timeout =
        message.value(QStringLiteral("action")) == QStringLiteral("delay") ? kDelayRequestTimeoutMs
        : message.value(QStringLiteral("action")) == QStringLiteral("reload") ? 20000
                                                                              : kRequestTimeoutMs;
    QTimer::singleShot(timeout, this, [this, id] {
        auto handler = handlers_.take(id);
        if (handler)
            handler(storageError("ServiceTimeout"));
    });
    if (!pendingWrites_.isEmpty())
        writeTimer_.start(kServiceRequestSpacingMs);
}
void ServiceClient::cancelQueuedDelays() {
    for (auto it = pendingWrites_.begin(); it != pendingWrites_.end();) {
        const auto request = QJsonDocument::fromJson(*it).object();
        if (request.value(QStringLiteral("action")) == QStringLiteral("delay")) {
            handlers_.remove(request.value(QStringLiteral("requestId")).toString());
            it = pendingWrites_.erase(it);
        } else {
            ++it;
        }
    }
    if (pendingWrites_.isEmpty())
        writeTimer_.stop();
}
void ServiceClient::connectService(int attempt) {
    if (pendingWrites_.isEmpty() || verified_ || socket_.state() != QLocalSocket::UnconnectedState)
        return;
    switch (platform::installedServiceRegistration(0)) {
    case platform::ServiceRegistration::NotInstalled:
        fail("ServiceInstallationRequired");
        return;
    case platform::ServiceRegistration::Mismatch:
        // A legacy registration or moved folder needs a new explicit authorization.
        fail("ServiceProtocolMismatch");
        return;
    case platform::ServiceRegistration::Stopped:
        if (!platform::startRegisteredService()) {
            fail("ServiceNotRunning");
            return;
        }
        if (attempt >= 100) {
            fail("ServiceNotRunning");
            return;
        }
        {
            const auto generation = closeGeneration_;
            QTimer::singleShot(100, this, [this, attempt, generation] {
                if (generation == closeGeneration_)
                    connectService(attempt + 1);
            });
        }
        return;
    case platform::ServiceRegistration::Unavailable:
    case platform::ServiceRegistration::Verified:
        break;
    }
    socket_.connectToServer(QStringLiteral("QuickClash.Service.v1"));
}
void ServiceClient::fail(const char* code) {
    if (failing_)
        return;
    failing_ = true;
    ++closeGeneration_;
    deadline_.stop();
    heartbeat_.stop();
    writeTimer_.stop();
    writeClock_.invalidate();
    heartbeatPending_ = false;
    activity_.invalidate();
    pendingWrites_.clear();
    input_.clear();
    sessionId_.clear();
    epoch_.clear();
    verified_ = false;
    handshakeId_.clear();
    auto handlers = std::move(handlers_);
    handlers_.clear();
    const bool active = socket_.state() != QLocalSocket::UnconnectedState;
    socket_.abort();
    for (auto& handler : handlers)
        handler(storageError(code));
    if (active || !handlers.isEmpty())
        emit lost(QString::fromLatin1(code));
    failing_ = false;
}
void ServiceClient::close() {
    ++closeGeneration_;
    sessionId_.clear();
    epoch_.clear();
    verified_ = false;
    everVerified_ = false;
    handshakeId_.clear();
    failing_ = true;
    heartbeat_.stop();
    writeTimer_.stop();
    writeClock_.invalidate();
    heartbeatPending_ = false;
    activity_.invalidate();
    deadline_.stop();
    handlers_.clear();
    pendingWrites_.clear();
    input_.clear();
    socket_.abort();
    failing_ = false;
}
} // namespace quickclash::infra
