#include "infra/core/service_client.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/json_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace quickclash::infra {
ServiceClient::ServiceClient(QObject* parent) : QObject(parent) {
    socket_.setReadBufferSize(kReadChunkSize);
    deadline_.setSingleShot(true);
    deadline_.setInterval(15000);
    heartbeat_.setInterval(10000);
    connect(&deadline_, &QTimer::timeout, this, [this] { fail("ServiceTimeout"); });
    connect(&heartbeat_, &QTimer::timeout, this, [this] {
        command(QStringLiteral("heartbeat"), {}, [](domain::Result<QJsonObject>) {});
    });
    connect(&socket_, &QLocalSocket::connected, this, [this] {
#ifdef Q_OS_WIN
        ULONG pid = 0;
        if (!GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket_.socketDescriptor()),
                                         &pid)) {
            fail("ServiceIdentityRejected");
            return;
        }
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) {
            fail("ServiceIdentityRejected");
            return;
        }
        wchar_t path[32768]{};
        DWORD length = 32768;
        HANDLE token = nullptr;
        const bool queried = QueryFullProcessImageNameW(process, 0, path, &length) &&
                             OpenProcessToken(process, TOKEN_QUERY, &token);
        QByteArray data(256, Qt::Uninitialized);
        DWORD size = 0;
        const bool system =
            queried &&
            GetTokenInformation(token, TokenUser, data.data(), static_cast<DWORD>(data.size()),
                                &size) &&
            IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, WinLocalSystemSid);
        if (token)
            CloseHandle(token);
        CloseHandle(process);
        const auto expected = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("quickclash_service.exe"));
        if (!system || QDir::cleanPath(QString::fromWCharArray(path, static_cast<int>(length)))
                               .compare(expected, Qt::CaseInsensitive) != 0) {
            fail("ServiceIdentityRejected");
            return;
        }
#endif
        for (const auto& bytes : pendingWrites_)
            socket_.write(bytes);
        pendingWrites_.clear();
        heartbeat_.start();
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] { fail("ServiceDisconnected"); });
    connect(&socket_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) { fail("ServiceUnavailable"); });
    connect(&socket_, &QIODevice::readyRead, this, [this] {
        if (!appendAvailable(socket_, input_, 3 * 1024 * 1024)) {
            fail("ServiceResponseLimit");
            return;
        }
        qsizetype newline;
        while ((newline = input_.indexOf('\n')) >= 0) {
            const auto result = QJsonDocument::fromJson(input_.first(newline)).object();
            input_.remove(0, newline + 1);
            auto handler = handlers_.take(result.value(QStringLiteral("requestId")).toString());
            if (!handler)
                continue;
            const auto error = result.value(QStringLiteral("error")).toString();
            if (error.isEmpty()) {
                const auto value = result.value(QStringLiteral("result")).toObject();
                if (value.contains(QStringLiteral("epoch")))
                    epoch_ = value.value(QStringLiteral("epoch")).toString();
                handler(result.value(QStringLiteral("result")).toObject());
            } else
                handler(storageError(error.toLatin1().constData()));
        }
        if (handlers_.isEmpty())
            deadline_.stop();
    });
}
bool ServiceClient::connected() const {
    return socket_.state() == QLocalSocket::ConnectedState;
}
void ServiceClient::command(const QString& action, const QJsonObject& payload, Handler handler) {
    if (handlers_.size() >= 4) {
        handler(storageError("ServiceBusy"));
        return;
    }
    const auto id = QString::number(++nextId_);
    const QJsonObject message{{QStringLiteral("protocolVersion"), 1},
                              {QStringLiteral("requestId"), id},
                              {QStringLiteral("sessionId"), epoch_},
                              {QStringLiteral("action"), action},
                              {QStringLiteral("payload"), payload}};
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > 24 * 1024 * 1024) {
        handler(storageError("ServiceRequestLimit"));
        return;
    }
    handlers_.insert(id, std::move(handler));
    if (connected())
        socket_.write(bytes);
    else {
        pendingWrites_.append(bytes);
        if (socket_.state() == QLocalSocket::UnconnectedState)
            socket_.connectToServer(QStringLiteral("QuickClash.Service.v1"));
    }
    if (!deadline_.isActive())
        deadline_.start();
}
void ServiceClient::fail(const char* code) {
    if (failing_)
        return;
    failing_ = true;
    deadline_.stop();
    heartbeat_.stop();
    pendingWrites_.clear();
    input_.clear();
    epoch_.clear();
    auto handlers = std::move(handlers_);
    handlers_.clear();
    const bool active = socket_.state() != QLocalSocket::UnconnectedState;
    socket_.abort();
    for (auto& handler : handlers)
        handler(storageError(code));
    if (active || !handlers.isEmpty())
        emit lost();
    failing_ = false;
}
void ServiceClient::close() {
    epoch_.clear();
    failing_ = true;
    heartbeat_.stop();
    deadline_.stop();
    handlers_.clear();
    pendingWrites_.clear();
    input_.clear();
    socket_.abort();
    failing_ = false;
}
} // namespace quickclash::infra
