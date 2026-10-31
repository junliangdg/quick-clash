#include "platform/windows/service_host.h"
#include "infra/config/config_preflight.h"
#include "infra/config/geo_resources.h"
#include "infra/core/listener_ports.h"
#include "infra/core/provider_command.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/runtime_provider_cache.h"
#include "platform/windows/trusted_client_root.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QUrl>
#include <yaml-cpp/yaml.h>
// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <psapi.h>
// clang-format on

namespace quickclash::platform {
namespace {
domain::Error failure(const char* code) {
    return infra::storageError(code);
}
QString installation() {
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}
QString serviceData() {
    wchar_t path[32768]{};
    const auto length = GetEnvironmentVariableW(L"ProgramData", path, 32768);
    return length && length < 32768
               ? QString::fromWCharArray(path) + QStringLiteral("/QuickClash/service")
               : QString{};
}
QString trustedClientRootFile() {
    const auto data = serviceData();
    return data.isEmpty()
               ? QString{}
               : QFileInfo(data).absolutePath() + QStringLiteral("/trusted_client_root.json");
}
bool fixedFields(const QJsonObject& object, const QStringList& keys) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!keys.contains(it.key()))
            return false;
    return true;
}
bool protectedPath(const QString& path) {
    // Every ancestor must be a real directory/file and protected from ordinary-user writes.
    for (auto info = QFileInfo(path); !info.isRoot(); info = QFileInfo(info.absolutePath())) {
        if (!info.exists() || info.isSymbolicLink() || info.isJunction())
            return false;
        PACL acl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const auto name = QDir::toNativeSeparators(info.absoluteFilePath());
        if (GetNamedSecurityInfoW(reinterpret_cast<LPCWSTR>(name.utf16()), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr,
                                  &descriptor) != ERROR_SUCCESS ||
            !acl)
            return false;
        bool safe = true;
        for (DWORD i = 0; i < acl->AceCount; ++i) {
            void* ace = nullptr;
            if (!GetAce(acl, i, &ace)) {
                safe = false;
                break;
            }
            const auto* header = static_cast<ACE_HEADER*>(ace);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || (header->AceFlags & INHERIT_ONLY_ACE))
                continue;
            const auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
            const auto sid = const_cast<DWORD*>(&allowed->SidStart);
            const auto normalized = QDir::fromNativeSeparators(info.absoluteFilePath());
            const bool installed =
                normalized.startsWith(
                    QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles")) +
                        QStringLiteral("/QuickClash"),
                    Qt::CaseInsensitive) ||
                normalized.startsWith(
                    QDir::fromNativeSeparators(qEnvironmentVariable("ProgramData")) +
                        QStringLiteral("/QuickClash"),
                    Qt::CaseInsensitive);
            // Shared ancestors may allow creation of unrelated children, but must not
            // allow replacing this installation or changing its ACL.
            const DWORD writes =
                DELETE | FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL |
                (installed
                     ? FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES
                     : 0);
            if ((allowed->Mask & writes) && !IsWellKnownSid(sid, WinLocalSystemSid) &&
                !IsWellKnownSid(sid, WinBuiltinAdministratorsSid) &&
                !IsWellKnownSid(sid, WinCreatorOwnerSid)) {
                // TrustedInstaller owns Windows-protected ancestors; allow its service SID.
                LPWSTR text = nullptr;
                if (!ConvertSidToStringSidW(sid, &text) ||
                    QString::fromWCharArray(text) !=
                        QStringLiteral(
                            "S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464"))
                    safe = false;
                if (text)
                    LocalFree(text);
            }
        }
        LocalFree(descriptor);
        if (!safe)
            return false;
    }
    return true;
}
} // namespace
ServiceHost::ServiceHost(QObject* parent)
    : QObject(parent), store_(serviceData()),
      buildId_(infra::ServiceProtocol::executableBuild(QCoreApplication::applicationFilePath())) {
    corePath_ = portableCorePath(installation());
    directory_ = store_.root() + QStringLiteral("/session");
    configPath_ = directory_ + QStringLiteral("/config.yaml");
    heartbeat_.setInterval(1000);
    connect(&heartbeat_, &QTimer::timeout, this, [this] {
        // A long scheduler gap includes suspend. Give the client a fresh handshake window.
        if (tick_.isValid() && tick_.restart() > 15000)
            lastHeartbeat_.restart();
        if (owner_ && lastHeartbeat_.isValid() && lastHeartbeat_.elapsed() > 60000) {
            stop("HeartbeatTimeout");
            owner_->disconnectFromServer();
        }
    });
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            auto* socket = server_.nextPendingConnection();
            if (buffers_.size() >= 4) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            buffers_.insert(socket, {});
            QString rejection;
            QString image;
            if (!authenticated(socket, &rejection, &image)) {
                rejections_.insert(socket, rejection.isEmpty()
                                               ? QStringLiteral("ServiceClientUntrusted")
                                               : rejection);
            } else {
                images_.insert(socket, image);
            }
            socket->setReadBufferSize(infra::kServiceHandshakeMaxBytes + 1);
            // Neither silent rejected peers nor authenticated peers that only say hello may
            // reserve a connection indefinitely. The owner uses its separate heartbeat lease.
            QTimer::singleShot(infra::kServiceHandshakeTimeoutMs, socket, [this, socket] {
                if (owner_ != socket)
                    socket->abort();
            });
            connect(socket, &QIODevice::readyRead, this, [this, socket] { receive(socket); });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                if (owner_ == socket) {
                    stop("ClientDisconnect");
                    owner_.clear();
                }
                buffers_.remove(socket);
                identities_.remove(socket);
                images_.remove(socket);
                rejections_.remove(socket);
                sessions_.remove(socket);
                if (!owner_)
                    protocol_.clear();
                socket->deleteLater();
            });
        }
    });
    connect(&core_, &OwnedProcess::finished, this, [this](int code) {
        coreExitKnown_ = true;
        coreExitCode_ = code;
        coreExitedAt_ = QDateTime::currentDateTimeUtc();
        // A service-initiated stop (timeout, probe failure, explicit stop) already recorded the
        // cause. Only an unsolicited core exit is a core exit.
        if (stopReason_.isEmpty() || stopReason_ == QStringLiteral("CoreExited"))
            stopReason_ = QStringLiteral("CoreExited");
        ready_ = false;
    });
}
bool ServiceHost::listen() {
    // First UAC explicitly trusts the portable executable tree. Only authorization and
    // private session state require a protected system directory.
    if (serviceData().isEmpty() || !protectedPath(QFileInfo(serviceData()).absolutePath()))
        return false;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    QByteArray data(256, Qt::Uninitialized);
    DWORD size = 0;
    const bool queried =
        GetTokenInformation(token, TokenUser, data.data(), static_cast<DWORD>(data.size()), &size);
    CloseHandle(token);
    if (!queried ||
        !IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, WinLocalSystemSid))
        return false;
    if (std::holds_alternative<domain::Error>(store_.open()))
        return false;
    if (!loadTrustedClientRoot())
        return false;
    const auto identity = store_.read(QStringLiteral("tun_identity.json"));
    const auto journal = store_.read(QStringLiteral("tun_session.json"));
    if (std::holds_alternative<domain::Error>(identity) ||
        std::holds_alternative<domain::Error>(journal))
        return false;
    if (const auto saved = std::get<std::optional<QJsonObject>>(identity)) {
        tunInstallationId_ = saved->value(QStringLiteral("installationId")).toString();
        tunName_ = saved->value(QStringLiteral("name")).toString();
        reusableTun_ = saved->value(QStringLiteral("adapter")).toObject();
        if (!QRegularExpression(QStringLiteral("^[a-f0-9]{32}$"))
                 .match(tunInstallationId_)
                 .hasMatch() ||
            !QRegularExpression(QStringLiteral("^QuickClash-[a-f0-9]{8}(-[1-9][0-9]?)?$"))
                 .match(tunName_)
                 .hasMatch())
            return false;
    }
    if (const auto saved = std::get<std::optional<QJsonObject>>(journal)) {
        tunRecord_ = *saved;
        if (tunRecord_.value(QStringLiteral("name")).toString().isEmpty() ||
            tunRecord_.value(QStringLiteral("epoch")).toString().isEmpty())
            return false;
    }
    // SCM process death closes the Job. Never reuse its configuration after restart.
    // Keep IPC available for diagnosis/retry while refusing a second uncertain session.
    (void)tunClean();
    const auto removed = store_.remove(QStringLiteral("session/config.yaml"));
    configClean_ = !std::holds_alternative<domain::Error>(removed);
    if (!configClean_)
        return false;
    server_.setSocketOptions(QLocalServer::UserAccessOption | QLocalServer::GroupAccessOption |
                             QLocalServer::OtherAccessOption);
    if (!server_.listen(QStringLiteral("QuickClash.Service.v1")))
        return false;
    tick_.start();
    heartbeat_.start();
    return true;
}
bool ServiceHost::loadTrustedClientRoot() {
    const auto file = trustedClientRootFile();
    if (file.isEmpty())
        return false;
    if (QFileInfo::exists(file) && !protectedPath(file))
        return false;
    const auto loaded = readTrustedClientRoot(file, installation());
    if (!loaded)
        return false;
    if (QDir::cleanPath(installation()).compare(loaded->root, Qt::CaseInsensitive) != 0)
        return false;
    trustedClient_ = *loaded;
    return true;
}
bool ServiceHost::authenticated(QLocalSocket* socket, QString* failure, QString* image) {
    const auto reject = [failure](const char* code) {
        if (failure)
            *failure = QString::fromLatin1(code);
        return false;
    };
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket->socketDescriptor()), &pid))
        return reject("ServiceIdentityRejected");
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return reject("ServiceIdentityRejected");
    wchar_t path[32768]{};
    DWORD length = 32768;
    HANDLE token = nullptr;
    const bool imageRead = QueryFullProcessImageNameW(process, 0, path, &length);
    const bool tokenOpen = imageRead && OpenProcessToken(process, TOKEN_QUERY, &token);
    DWORD session = 0, size = 0;
    const bool sessionRead =
        tokenOpen && GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &size);
    QByteArray user(1024, Qt::Uninitialized);
    const bool userRead =
        sessionRead &&
        GetTokenInformation(token, TokenUser, user.data(), static_cast<DWORD>(user.size()), &size);
    LPWSTR sid = nullptr;
    const bool sidRead =
        userRead &&
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid);
    FILETIME created{}, exited{}, kernel{}, used{};
    const bool timeRead = GetProcessTimes(process, &created, &exited, &kernel, &used);
    const auto sidText = sid ? QString::fromWCharArray(sid) : QString{};
    if (sid)
        LocalFree(sid);
    if (token)
        CloseHandle(token);
    CloseHandle(process);
    const auto trusted =
        imageRead ? trustedClientImage(QString::fromWCharArray(path, static_cast<int>(length)),
                                       trustedClient_)
                  : std::nullopt;
    if (!trusted)
        return reject("ServiceClientUntrusted");
    if (!sessionRead || !sidRead || sidText.isEmpty() || !timeRead || session == 0 ||
        session != WTSGetActiveConsoleSessionId())
        return reject("ServiceIdentityRejected");
    const auto identity = (*trusted + QStringLiteral("\n") + sidText + QStringLiteral(":") +
                           QString::number(session) + QStringLiteral(":") + QString::number(pid) +
                           QStringLiteral(":") + QString::number(created.dwHighDateTime) +
                           QStringLiteral(":") + QString::number(created.dwLowDateTime))
                              .toUtf8();
    if (identities_.contains(socket) && identities_.value(socket) != identity)
        return reject("ServiceIdentityRejected");
    identities_.insert(socket, identity);
    if (image)
        *image = *trusted;
    return true;
}
void ServiceHost::receive(QLocalSocket* socket) {
    if (rejections_.contains(socket)) {
        auto& rejectionBuffer = buffers_[socket];
        if (!infra::appendAvailable(*socket, rejectionBuffer, infra::kServiceHandshakeMaxBytes)) {
            socket->abort();
            return;
        }
        const auto newline = rejectionBuffer.indexOf('\n');
        if (newline < 0)
            return;
        const auto request = QJsonDocument::fromJson(rejectionBuffer.first(newline)).object();
        rejectionBuffer.remove(0, newline + 1);
        if (!infra::ServiceProtocol::validEnvelope(request)) {
            socket->abort();
            return;
        }
        answer(socket, request.value(QStringLiteral("requestId")).toString(),
               failure(rejections_.value(socket).toLatin1().constData()));
        rejections_.remove(socket);
        socket->disconnectFromServer();
        return;
    }
    QString image;
    if (!authenticated(socket, nullptr, &image) || images_.value(socket) != image) {
        socket->abort();
        return;
    }
    auto& buffer = buffers_[socket];
    const auto limit =
        sessions_.contains(socket) ? 24 * 1024 * 1024 : infra::kServiceHandshakeMaxBytes;
    if (!infra::appendAvailable(*socket, buffer, limit)) {
        socket->abort();
        return;
    }
    qsizetype newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const auto request = QJsonDocument::fromJson(buffer.first(newline)).object();
        buffer.remove(0, newline + 1);
        const auto id = request.value(QStringLiteral("requestId")).toString();
        if (!infra::ServiceProtocol::validEnvelope(request)) {
            answer(socket, id, failure("ServiceProtocolMismatch"));
            continue;
        }
        if (owner_ && owner_ != socket) {
            answer(socket, id, failure("ServiceSessionBusy"));
            continue;
        }
        const auto action = request.value(QStringLiteral("action")).toString();
        if (action == QStringLiteral("hello") && !sessions_.contains(socket) &&
            request.value(QStringLiteral("payload")).toObject().isEmpty() &&
            request.value(QStringLiteral("sessionId")).toString().isEmpty()) {
            const auto session = QUuid::createUuid().toString(QUuid::WithoutBraces);
            sessions_.insert(socket, session);
            socket->setReadBufferSize(infra::kReadChunkSize);
            auto capabilities = infra::ServiceProtocol::capabilities(buildId_);
            capabilities.insert(QStringLiteral("sessionId"), session);
            answer(socket, id, capabilities);
            continue;
        }
        if (!sessions_.contains(socket)) {
            answer(socket, id, failure("ServiceHandshakeRequired"));
            continue;
        }
        if (!owner_) {
            owner_ = socket;
            protocol_.clear();
            requestWindow_.start();
            requestCount_ = 0;
        }
        lastHeartbeat_.restart();
        if (requestWindow_.elapsed() >= 1000) {
            requestWindow_.restart();
            requestCount_ = 0;
        }
        if (++requestCount_ > 24) {
            socket->abort();
            return;
        }
        const auto decision = protocol_.accept(request);
        if (decision.disposition == infra::ServiceProtocol::Disposition::Disconnect) {
            socket->abort();
            return;
        }
        if (decision.disposition == infra::ServiceProtocol::Disposition::Cached) {
            socket->write(QJsonDocument(decision.response).toJson(QJsonDocument::Compact) + '\n');
            continue;
        }
        if (decision.disposition == infra::ServiceProtocol::Disposition::Pending)
            continue;
        if (decision.disposition == infra::ServiceProtocol::Disposition::Reject) {
            answer(socket, id, failure(decision.error.toLatin1().constData()));
            continue;
        }
        if (request.value(QStringLiteral("sessionId")).toString() != sessions_.value(socket)) {
            answer(socket, id, failure("ServiceStaleSession"));
            continue;
        }
        if (action != QStringLiteral("status") && action != QStringLiteral("heartbeat") &&
            action != QStringLiteral("resources") &&
            request.value(QStringLiteral("engineEpoch")).toString() != epoch_) {
            answer(socket, id, failure("ServiceStaleSession"));
            continue;
        }
        dispatch(socket, id, request.value(QStringLiteral("action")).toString(),
                 request.value(QStringLiteral("payload")).toObject());
    }
}
void ServiceHost::answer(QLocalSocket* socket, const QString& id,
                         domain::Result<QJsonObject> result) {
    if (!socket)
        return;
    QJsonObject response{{QStringLiteral("protocolVersion"), 1}, {QStringLiteral("requestId"), id}};
    if (const auto* error = std::get_if<domain::Error>(&result)) {
        response.insert(QStringLiteral("error"), error->code);
        if (!error->redactedDetail.isEmpty())
            response.insert(QStringLiteral("detail"),
                            QJsonDocument::fromJson(error->redactedDetail.toUtf8()).object());
    } else
        response.insert(QStringLiteral("result"), std::get<QJsonObject>(result));
    if (owner_ == socket) {
        protocol_.complete(id, response);
    }
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
}
void ServiceHost::api(QLocalSocket* socket, const QString& id, const QByteArray& method,
                      const QByteArray& path, const QJsonObject& body) {
    QPointer<QLocalSocket> peer(socket);
    const auto epoch = epoch_;
    infra::pipeHttp(this, pipe_, method, path, body,
                    [this, peer, id, epoch](domain::Result<QJsonObject> result) {
                        if (epoch != epoch_)
                            result = failure("ServiceStaleSession");
                        if (peer && peer == owner_)
                            answer(peer, id, std::move(result));
                    });
}
void ServiceHost::dispatch(QLocalSocket* socket, const QString& id, const QString& action,
                           const QJsonObject& payload) {
    if (action == QStringLiteral("resources") && payload.isEmpty()) {
        const auto service = currentProcessMemory();
        const auto core = core_.memory();
        const auto validator = validator_.memory();
        QJsonObject result{{QStringLiteral("service"), service},
                           {QStringLiteral("core"), core},
                           {QStringLiteral("validator"), validator},
                           {QStringLiteral("validatorExpected"), validator_.running()},
                           {QStringLiteral("complete"),
                            !service.isEmpty() && (!core_.running() || !core.isEmpty()) &&
                                (!validator_.running() || !validator.isEmpty())}};
        answer(socket, id, result);
        return;
    }
    if (action == QStringLiteral("heartbeat") && payload.isEmpty()) {
        answer(socket, id, QJsonObject{});
        return;
    }
    if (action == QStringLiteral("start")) {
        start(socket, id, payload);
        return;
    }
    if (action == QStringLiteral("stop") && payload.isEmpty()) {
        stop("Explicit");
        confirmStopped(socket, id, 0);
        return;
    }
    if (action == QStringLiteral("status") && payload.isEmpty()) {
        auto result =
            QJsonObject{{QStringLiteral("ready"), ready_},
                        {QStringLiteral("busy"), busy_},
                        {QStringLiteral("coreRunning"), core_.running()},
                        {QStringLiteral("pid"), static_cast<int>(core_.pid())},
                        {QStringLiteral("geoResources"), geoResources_},
                        {QStringLiteral("epoch"), epoch_},
                        {QStringLiteral("tunDevice"), tunName_},
                        {QStringLiteral("tunIdentity"), tunRecord_},
                        {QStringLiteral("clean"), !core_.running() && !validator_.running() &&
                                                      !busy_ && configClean_ && tunClean()}};
        auto tunState = QStringLiteral("Off");
        if (ready_ && tunDesired_) {
            const auto observed = observeTun();
            tunState = observed == TunObservation::On        ? QStringLiteral("On")
                       : observed == TunObservation::Missing ? QStringLiteral("Missing")
                                                             : QStringLiteral("Unknown");
        }
        result.insert(QStringLiteral("tun"), tunState == QStringLiteral("On"));
        result.insert(QStringLiteral("tunState"), tunState);
        if (!stopReason_.isEmpty())
            result.insert(QStringLiteral("stopReason"), stopReason_);
        if (!stopDetail_.isEmpty())
            result.insert(QStringLiteral("stopDetail"), stopDetail_.left(1024));
        if (coreExitKnown_) {
            result.insert(QStringLiteral("coreExitCode"), coreExitCode_);
            result.insert(QStringLiteral("coreExitedAt"), coreExitedAt_.toString(Qt::ISODate));
            const auto tail = QString::fromUtf8(core_.outputTail());
            if (!tail.isEmpty()) {
                const auto safe = domain::redactedLogEntry(0, {}, QStringLiteral("Core"),
                                                           QStringLiteral("error"), tail);
                result.insert(QStringLiteral("coreStderr"), safe.value(QStringLiteral("message")));
            }
        }
        answer(socket, id, result);
        return;
    }
    if (action == QStringLiteral("tun_preflight") && payload.isEmpty()) {
        if (!tunClean()) {
            answer(socket, id, failure("ServiceCleanupUncertain"));
            return;
        }
        const auto network = inspectTunNetwork();
        if (const auto* error = std::get_if<domain::Error>(&network))
            answer(socket, id, *error);
        else if (conflictingTunRoute(std::get<TunNetwork>(network)))
            answer(socket, id, failure("ServiceTunRouteConflict"));
        else
            answer(socket, id, QJsonObject{});
        return;
    }
    if (action == QStringLiteral("log_demand") &&
        fixedFields(payload, {QStringLiteral("enabled")})) {
        if (!payload.value(QStringLiteral("enabled")).isBool()) {
            answer(socket, id, failure("ServiceFieldsRejected"));
            return;
        }
        if (!payload.value(QStringLiteral("enabled")).toBool()) {
            delete logStream_.data();
            logBuffer_.clear();
            answer(socket, id, QJsonObject{});
            return;
        }
        if (!ready_ || busy_) {
            answer(socket, id, failure("ServiceCoreNotReady"));
            return;
        }
        if (!logStream_) {
            logStream_ = infra::pipeJsonStream(
                this, pipe_, "/logs?level=info", [this](const QJsonObject& entry) {
                    const auto safe =
                        domain::redactedLogEntry(++logSequence_, {}, QStringLiteral("Core"),
                                                 entry.value(QStringLiteral("type")).toString(),
                                                 entry.value(QStringLiteral("payload")).toString());
                    logBuffer_.append(
                        {{QStringLiteral("type"), safe.value(QStringLiteral("level"))},
                         {QStringLiteral("payload"), safe.value(QStringLiteral("message"))},
                         {QStringLiteral("truncated"), safe.value(QStringLiteral("truncated"))}});
                });
        }
        answer(socket, id, QJsonObject{});
        return;
    }
    if (!ready_ || busy_) {
        answer(socket, id, failure("ServiceCoreNotReady"));
        return;
    }
    if (action == QStringLiteral("logs") && payload.isEmpty()) {
        if (!logStream_ && !logBuffer_.entries().isEmpty()) {
            logBuffer_.append(QJsonObject{
                {QStringLiteral("source"), QStringLiteral("Service")},
                {QStringLiteral("type"), QStringLiteral("warning")},
                {QStringLiteral("payload"), QStringLiteral("Service log stream disconnected")}});
        }
        answer(socket, id,
               QJsonObject{
                   {QStringLiteral("entries"), logBuffer_.entries()},
                   {QStringLiteral("dropped"),
                    logBuffer_.status().value(QStringLiteral("droppedLogs")).toString().toInt()}});
        logBuffer_.clear();
        return;
    }
    if (action == QStringLiteral("select") &&
        fixedFields(payload, {QStringLiteral("group"), QStringLiteral("member")})) {
        const auto group = payload.value(QStringLiteral("group")).toString(),
                   member = payload.value(QStringLiteral("member")).toString();
        const auto value = groups_.value(group).toObject();
        if (value.value(QStringLiteral("type")) != QStringLiteral("Selector") ||
            !value.value(QStringLiteral("all")).toArray().contains(member)) {
            answer(socket, id, failure("ServiceSelectionRejected"));
            return;
        }
        api(socket, id, "PUT", "/proxies/" + QUrl::toPercentEncoding(group),
            {{QStringLiteral("name"), member}});
        return;
    }
    if (action == QStringLiteral("delay") &&
        fixedFields(payload, {QStringLiteral("name"), QStringLiteral("url")})) {
        const auto name = payload.value(QStringLiteral("name")).toString();
        const QUrl url(payload.value(QStringLiteral("url")).toString());
        if (!groups_.contains(name) || !url.isValid() || url.scheme() != QStringLiteral("https") ||
            !url.userInfo().isEmpty() || url.hasFragment() || url.toString().size() > 8192) {
            answer(socket, id, failure("ServiceDelayRejected"));
            return;
        }
        api(socket, id, "GET",
            "/proxies/" + QUrl::toPercentEncoding(name) +
                "/delay?timeout=2500&url=" + QUrl::toPercentEncoding(url.toString()));
        return;
    }
    if (action == QStringLiteral("close_connection") &&
        fixedFields(payload, {QStringLiteral("id")})) {
        const auto connection = payload.value(QStringLiteral("id")).toString();
        if (QUuid(connection).isNull()) {
            answer(socket, id, failure("ServiceConnectionRejected"));
            return;
        }
        api(socket, id, "DELETE", "/connections/" + QUrl::toPercentEncoding(connection));
        return;
    }
    if (action == QStringLiteral("refresh_provider") &&
        fixedFields(payload, {QStringLiteral("kind"), QStringLiteral("name")})) {
        const auto path = infra::providerRefreshPath(payload, providers_);
        if (providerRefreshPending_ || !path) {
            answer(socket, id, failure("ServiceProviderRejected"));
            return;
        }
        providerRefreshPending_ = true;
        QPointer<QLocalSocket> peer(socket);
        const auto epoch = epoch_;
        infra::pipeHttp(this, pipe_, "PUT", *path, {},
                        [this, peer, id, epoch](domain::Result<QJsonObject> result) {
                            if (epoch != epoch_)
                                return;
                            providerRefreshPending_ = false;
                            if (peer && peer == owner_)
                                answer(peer, id, std::move(result));
                        });
        return;
    }
    if (!payload.isEmpty()) {
        answer(socket, id, failure("ServiceFieldsRejected"));
        return;
    }
    if (action == QStringLiteral("groups")) {
        QPointer<QLocalSocket> peer(socket);
        const auto epoch = epoch_;
        infra::pipeHttp(this, pipe_, "GET", "/proxies", {},
                        [this, peer, id, epoch](domain::Result<QJsonObject> result) {
                            if (epoch != epoch_)
                                return;
                            if (const auto* value = std::get_if<QJsonObject>(&result))
                                groups_ = value->value(QStringLiteral("proxies")).toObject();
                            if (peer && owner_ == peer)
                                answer(peer, id, std::move(result));
                        });
        return;
    }
    const QHash<QString, QByteArray> reads{
        {QStringLiteral("rules"), "/rules"},
        {QStringLiteral("connections"), "/connections"},
        {QStringLiteral("proxy_providers"), "/providers/proxies"},
        {QStringLiteral("rule_providers"), "/providers/rules"},
        {QStringLiteral("traffic"), "/traffic"},
        {QStringLiteral("logs"), "/logs?level=info"}};
    if (reads.contains(action)) {
        api(socket, id, "GET", reads.value(action));
        return;
    }
    if (action == QStringLiteral("flush_dns")) {
        api(socket, id, "POST", "/cache/dns/flush");
        return;
    }
    if (action == QStringLiteral("close_connections")) {
        api(socket, id, "DELETE", "/connections");
        return;
    }
    answer(socket, id, failure("ServiceActionRejected"));
}
void ServiceHost::start(QLocalSocket* socket, const QString& id, const QJsonObject& payload) {
    if (!tunClean()) {
        answer(socket, id, failure("ServiceCleanupUncertain"));
        return;
    }
    if (busy_ || core_.running() || !configClean_ ||
        !fixedFields(payload,
                     {QStringLiteral("yaml"), QStringLiteral("mixedPort"), QStringLiteral("mode"),
                      QStringLiteral("dnsMode"), QStringLiteral("tun"), QStringLiteral("geo")})) {
        answer(socket, id, failure("ServiceStartRejected"));
        return;
    }
    const auto encoded = payload.value(QStringLiteral("yaml")).toString();
    if (!payload.value(QStringLiteral("tun")).isBool()) {
        answer(socket, id, failure("ServiceFieldsRejected"));
        return;
    }
    const bool tun = payload.value(QStringLiteral("tun")).toBool();
    if (encoded.size() > 23 * 1024 * 1024) {
        answer(socket, id, failure("ServiceConfigLimit"));
        return;
    }
    const auto decoded =
        QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    domain::RuntimePreferences pref;
    const auto portValue = payload.value(QStringLiteral("mixedPort"));
    const auto port = portValue.toInt();
    if (!decoded || !portValue.isDouble() || portValue.toDouble() != port || port < 1024 ||
        port > 65535) {
        answer(socket, id, failure("ServiceConfigRejected"));
        return;
    }
    pref.mixedPort = static_cast<quint16>(port);
    pref.controllerPort = port == 9090 ? 9091 : 9090;
    const auto mode = payload.value(QStringLiteral("mode")).toString();
    if (mode != QStringLiteral("rule") && mode != QStringLiteral("global") &&
        mode != QStringLiteral("direct")) {
        answer(socket, id, failure("ServiceConfigRejected"));
        return;
    }
    pref.mode = mode == QStringLiteral("global")   ? domain::ProxyMode::Global
                : mode == QStringLiteral("direct") ? domain::ProxyMode::Direct
                                                   : domain::ProxyMode::Rule;
    pref.dnsMode = payload.value(QStringLiteral("dnsMode")).toString(QStringLiteral("fake-ip"));
    const auto checked = infra::preflightConfig(decoded.decoded, pref);
    if (const auto* error = std::get_if<domain::Error>(&checked)) {
        answer(socket, id, *error);
        return;
    }
    QFile binary(corePath_);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!binary.open(QIODevice::ReadOnly) || !hash.addData(&binary) ||
        hash.result().toHex() !=
            "1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a") {
        answer(socket, id, failure("CoreIntegrityFailed"));
        return;
    }
    const auto semantic = std::get<infra::PreflightConfig>(checked).semanticYaml;
    QTcpServer listener;
    if (!listener.listen(QHostAddress::LocalHost, pref.mixedPort)) {
        const auto issue =
            infra::listenerPortIssue(listener, pref.mixedPort, QStringLiteral("mixed"));
        auto error = infra::storageError(
            issue.value(QStringLiteral("code")).toString().toLatin1().constData());
        error.redactedDetail =
            QString::fromUtf8(QJsonDocument(issue).toJson(QJsonDocument::Compact));
        answer(socket, id, error);
        return;
    }
    listener.close();
    if (tun) {
        const auto prepared = prepareTun();
        if (const auto* error = std::get_if<domain::Error>(&prepared)) {
            answer(socket, id, *error);
            return;
        }
    }
    pipe_ = QStringLiteral("QuickClash.Core.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray configBytes;
    try {
        auto root = YAML::Load(semantic.toStdString());
        providers_.clear();
        for (const auto* kind : {"proxy-providers", "rule-providers"})
            for (const auto& entry : root[kind])
                providers_.insert((std::string(kind) == "proxy-providers"
                                       ? QStringLiteral("proxies:")
                                       : QStringLiteral("rules:")) +
                                  QString::fromStdString(entry.first.as<std::string>()));
        root.remove("external-controller");
        root["external-controller-pipe"] =
            QDir::toNativeSeparators(QStringLiteral("//./pipe/") + pipe_).toStdString();
        root["secret"] = QUuid::createUuid().toString().toStdString();
        root["tun"]["enable"] = tun;
        root["tun"]["device"] = tunName_.toStdString();
        root["tun"]["stack"] = "mixed";
        root["tun"]["auto-route"] = true;
        root["tun"]["auto-detect-interface"] = true;
        root["tun"]["dns-hijack"].push_back("any:53");
        if (tun)
            root["dns"]["enable"] = true;
        if (tun && !root["dns"]["nameserver"])
            root["dns"]["nameserver"].push_back("https://1.1.1.1/dns-query");
        YAML::Emitter emitter;
        emitter << root;
        if (!emitter.good()) {
            answer(socket, id, failure("ServiceConfigRejected"));
            return;
        }
        configBytes = QByteArray(emitter.c_str());
    } catch (const YAML::Exception&) {
        answer(socket, id, failure("ServiceConfigRejected"));
        return;
    }
    const auto saved = store_.writeBytes(QStringLiteral("session/config.yaml"), configBytes, true);
    if (const auto* error = std::get_if<domain::Error>(&saved)) {
        answer(socket, id, *error);
        return;
    }
    configClean_ = false;
    const auto geo = payload.value(QStringLiteral("geo")).toObject();
    if ((!geo.isEmpty() && (geo.size() != 2 || !geo.value(QStringLiteral("snapshot")).isObject() ||
                            !geo.value(QStringLiteral("storageRoot")).isString())) ||
        (payload.contains(QStringLiteral("geo")) &&
         !payload.value(QStringLiteral("geo")).isObject())) {
        answer(socket, id, failure("ServiceFieldsRejected"));
        return;
    }
    const auto snapshot = geo.value(QStringLiteral("snapshot")).toObject();
    const auto storageRoot = geo.value(QStringLiteral("storageRoot")).toString();
    if (!snapshot.isEmpty() &&
        (!QDir::isAbsolutePath(storageRoot) || storageRoot.startsWith(QLatin1Char('/')) ||
         storageRoot.startsWith(QLatin1Char('\\')))) {
        answer(socket, id, failure("GeoResourceIntegrityFailed"));
        return;
    }
    // Open user snapshots under the authenticated pipe peer token, then copy/hash the same
    // bounded handle as SYSTEM into the private core home. This cannot read a SYSTEM-only
    // file on behalf of an ordinary client, even if a path is swapped during the request.
    const auto openSource = [socket, snapshot](QFile& source) {
        if (snapshot.isEmpty())
            return source.open(QIODevice::ReadOnly);
        if (!ImpersonateNamedPipeClient(reinterpret_cast<HANDLE>(socket->socketDescriptor())))
            return false;
        const auto opened = source.open(QIODevice::ReadOnly);
        if (!RevertToSelf())
            TerminateProcess(GetCurrentProcess(), ERROR_CANNOT_IMPERSONATE);
        return opened;
    };
    const auto resources =
        infra::prepareGeoResources(configBytes, directory_, snapshot, storageRoot, openSource);
    if (const auto* error = std::get_if<domain::Error>(&resources)) {
        answer(socket, id, *error);
        return;
    }
    geoResources_ = infra::geoResourceManifest(configBytes, snapshot);
    const auto providerCache =
        infra::prepareRuntimeProviderCache(configBytes, store_, QStringLiteral("session"));
    if (const auto* error = std::get_if<domain::Error>(&providerCache)) {
        answer(socket, id, *error);
        return;
    }
    epoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    busy_ = true;
    tunDesired_ = tun;
    mixedPort_ = pref.mixedPort;
    QPointer<QLocalSocket> peer(socket);
    const auto epoch = epoch_;
    validationConnection_ =
        connect(&validator_, &OwnedProcess::finished, this, [this, peer, id, epoch](int exit) {
            disconnect(validationConnection_);
            if (!peer || owner_ != peer || !busy_ || epoch != epoch_)
                return;
            if (exit != 0) {
                busy_ = false;
                answer(peer, id, failure("ServiceValidationFailed"));
                return;
            }
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("LISTEN_NAMEDPIPE_SDDL"),
                               QStringLiteral("D:P(A;;GA;;;SY)"));
            if (tunDesired_) {
                const auto inspected = inspectTunNetwork();
                const auto* network = std::get_if<TunNetwork>(&inspected);
                if (!network || conflictingTunRoute(*network) ||
                    availableTunName(tunName_, tunInstallationId_, *network, reusableTun_) !=
                        tunName_) {
                    busy_ = false;
                    answer(peer, id,
                           failure(!network ? "ServiceNetworkInspectionFailed"
                                   : conflictingTunRoute(*network) ? "ServiceTunRouteConflict"
                                                                   : "ServiceTunNameChanged"));
                    return;
                }
                tunBefore_ = *network;
                // Record intent before creation. A crash before GUID observation stays uncertain.
                tunRecord_ = {{QStringLiteral("name"), tunName_},
                              {QStringLiteral("epoch"), epoch_},
                              {QStringLiteral("clientImage"), images_.value(peer.data())},
                              {QStringLiteral("startedAt"),
                               QString::number(QDateTime::currentMSecsSinceEpoch())}};
                const auto saved = store_.write(QStringLiteral("tun_session.json"), tunRecord_);
                if (const auto* error = std::get_if<domain::Error>(&saved)) {
                    tunJournalFailed_ = true;
                    busy_ = false;
                    answer(peer, id, *error);
                    return;
                }
            }
            coreExitKnown_ = false;
            coreExitCode_ = 0;
            coreExitedAt_ = {};
            stopReason_.clear();
            stopDetail_.clear();
            probeDetail_.clear();
            const auto launched = core_.start(
                corePath_, {QStringLiteral("-f"), configPath_, QStringLiteral("-d"), directory_},
                directory_, false, environment);
            if (const auto* error = std::get_if<domain::Error>(&launched)) {
                busy_ = false;
                answer(peer, id, *error);
                return;
            }
            if (tunDesired_) {
                tunRecord_.insert(QStringLiteral("corePid"), static_cast<int>(core_.pid()));
                if (std::holds_alternative<domain::Error>(
                        store_.write(QStringLiteral("tun_session.json"), tunRecord_))) {
                    tunJournalFailed_ = true;
                    stop("JournalFailed");
                    answer(peer, id, failure("ServiceCleanupUncertain"));
                    return;
                }
            }
            probe(peer, id, 0);
        });
    const auto validating = validator_.start(
        corePath_,
        {QStringLiteral("-t"), QStringLiteral("-f"), configPath_, QStringLiteral("-d"), directory_},
        directory_, true);
    if (const auto* error = std::get_if<domain::Error>(&validating)) {
        disconnect(validationConnection_);
        busy_ = false;
        answer(socket, id, *error);
    }
    QTimer::singleShot(12000, this, [this, peer, id, epoch] {
        if (peer == owner_ && epoch == epoch_ && busy_ && protocol_.pending(id)) {
            stop("StartTimeout");
            answer(peer, id, failure("ServiceStartTimeout"));
        }
    });
}
void ServiceHost::probe(QLocalSocket* socket, const QString& id, int attempt) {
    if (owner_ != socket || !busy_)
        return;
    QPointer<QLocalSocket> peer(socket);
    const auto epoch = epoch_;
    infra::pipeHttp(
        this, pipe_, "GET", "/configs", {},
        [this, peer, id, attempt, epoch](domain::Result<QJsonObject> result) {
            if (!peer || owner_ != peer || !busy_ || epoch != epoch_)
                return;
            const auto* value = std::get_if<QJsonObject>(&result);
            const auto tun = value ? value->value(QStringLiteral("tun")).toObject() : QJsonObject{};
            const bool portMatches =
                value && value->value(QStringLiteral("mixed-port")).toInt() == mixedPort_;
            const bool enableMatches =
                value && tun.value(QStringLiteral("enable")).toBool() == tunDesired_;
            const bool listenerOwned = core_.ownsListener(mixedPort_);
            const bool deviceMatches =
                !tunDesired_ ||
                (value && tun.value(QStringLiteral("device")).toString() == tunName_);
            const bool observed = !tunDesired_ || tunObserved();
            if (portMatches && enableMatches && listenerOwned && deviceMatches && observed) {
                busy_ = false;
                ready_ = true;
                probeDetail_.clear();
                answer(peer, id,
                       QJsonObject{{QStringLiteral("version"), QStringLiteral("v1.19.33")},
                                   {QStringLiteral("pid"), static_cast<int>(core_.pid())},
                                   {QStringLiteral("epoch"), epoch_},
                                   {QStringLiteral("tunDevice"), tunName_},
                                   {QStringLiteral("tun"), tunDesired_}});
                return;
            }
            QString detail;
            if (!value)
                detail = QStringLiteral("configs=%1").arg(std::get<domain::Error>(result).code);
            else
                detail = QStringLiteral("port=%1/%2 enable=%3/%4 device=%5/%6 listener=%7")
                             .arg(value->value(QStringLiteral("mixed-port")).toInt())
                             .arg(mixedPort_)
                             .arg(tun.value(QStringLiteral("enable")).toBool())
                             .arg(tunDesired_)
                             .arg(tun.value(QStringLiteral("device")).toString(), tunName_)
                             .arg(listenerOwned);
            if (tunDesired_ && value && deviceMatches && !observed) {
                const auto inspected = inspectTunNetwork();
                const auto* network = std::get_if<TunNetwork>(&inspected);
                QString adapter = QStringLiteral("absent");
                if (!network)
                    adapter = QStringLiteral("inspect-failed");
                else
                    for (const auto& item : network->adapters)
                        if (item.name == tunName_) {
                            adapter = QStringLiteral("up=%1 routes=%2 dns=%3")
                                          .arg(item.up)
                                          .arg(item.defaultRoutes.join(QLatin1Char(',')),
                                               item.dnsServers.join(QLatin1Char(',')));
                            break;
                        }
                detail += QStringLiteral(" adapter(%1): %2").arg(tunName_, adapter);
            }
            probeDetail_ = detail.left(1024);
            if (attempt >= 24 || !core_.running()) {
                stopDetail_ = probeDetail_;
                stop("ProbeFailed");
                answer(peer, id, failure("ServiceTunReadbackFailed"));
                return;
            }
            QTimer::singleShot(250, this, [this, peer, id, attempt, epoch] {
                if (peer && epoch == epoch_)
                    probe(peer, id, attempt + 1);
            });
        });
}
domain::CommandResult ServiceHost::prepareTun() {
    const auto inspected = inspectTunNetwork();
    if (const auto* error = std::get_if<domain::Error>(&inspected))
        return *error;
    const auto& network = std::get<TunNetwork>(inspected);
    if (conflictingTunRoute(network))
        return failure("ServiceTunRouteConflict");
    if (tunInstallationId_.isEmpty())
        tunInstallationId_ = QUuid::createUuid().toString(QUuid::Id128);
    const auto name = availableTunName(tunName_, tunInstallationId_, network, reusableTun_);
    if (name.isEmpty())
        return failure("ServiceTunNameExhausted");
    const auto saved = store_.write(
        QStringLiteral("tun_identity.json"),
        {{QStringLiteral("installationId"), tunInstallationId_},
         {QStringLiteral("name"), name},
         {QStringLiteral("adapter"), name == tunName_ ? reusableTun_ : QJsonObject{}}});
    if (const auto* error = std::get_if<domain::Error>(&saved))
        return *error;
    if (name != tunName_)
        reusableTun_ = {};
    tunName_ = name;
    tunBefore_ = network;
    return std::monostate{};
}
ServiceHost::TunObservation ServiceHost::observeTun() {
    if (!core_.running() || tunJournalFailed_)
        return TunObservation::Missing;
    const auto inspected = inspectTunNetwork();
    const auto* network = std::get_if<TunNetwork>(&inspected);
    if (!network)
        return TunObservation::Unknown;
    const auto observed = observedTunRecord(tunName_, tunBefore_, *network, reusableTun_);
    if (observed.isEmpty())
        return TunObservation::Missing;
    if (!tunRecord_.value(QStringLiteral("guid")).toString().isEmpty())
        return tunRecord_.value(QStringLiteral("guid")) == observed.value(QStringLiteral("guid")) &&
                       tunRecord_.value(QStringLiteral("luid")) ==
                           observed.value(QStringLiteral("luid"))
                   ? TunObservation::On
                   : TunObservation::Missing;
    for (auto it = observed.begin(); it != observed.end(); ++it)
        tunRecord_.insert(it.key(), it.value());
    const auto saved = store_.write(QStringLiteral("tun_session.json"), tunRecord_);
    if (std::holds_alternative<domain::Error>(saved)) {
        tunJournalFailed_ = true;
        return TunObservation::Missing;
    }
    const auto identitySaved = store_.write(QStringLiteral("tun_identity.json"),
                                            {{QStringLiteral("installationId"), tunInstallationId_},
                                             {QStringLiteral("name"), tunName_},
                                             {QStringLiteral("adapter"), observed}});
    if (std::holds_alternative<domain::Error>(identitySaved)) {
        tunJournalFailed_ = true;
        return TunObservation::Missing;
    }
    reusableTun_ = observed;
    return TunObservation::On;
}
bool ServiceHost::tunObserved() {
    return observeTun() == TunObservation::On;
}
bool ServiceHost::tunClean() {
    if (tunJournalFailed_)
        return false;
    if (tunRecord_.isEmpty())
        return true;
    if (core_.running() || validator_.running() || busy_)
        return false;
    const auto inspected = inspectTunNetwork();
    const auto* network = std::get_if<TunNetwork>(&inspected);
    if (!network || !tunRecordClean(tunRecord_, *network))
        return false;
    if (std::holds_alternative<domain::Error>(store_.remove(QStringLiteral("tun_session.json"))))
        return false;
    tunRecord_ = {};
    return true;
}
void ServiceHost::stop(const char* reason) {
    // A real core exit keeps its own reason even when the client asks for the follow-up stop.
    if (reason && !coreExitKnown_)
        stopReason_ = QString::fromLatin1(reason);
    epoch_.clear();
    delete logStream_.data();
    logBuffer_.clear();
    disconnect(validationConnection_);
    busy_ = false;
    ready_ = false;
    tunDesired_ = false;
    validator_.stop();
    core_.stop();
    groups_ = {};
    providers_.clear();
    providerRefreshPending_ = false;
    const auto removed = store_.remove(QStringLiteral("session/config.yaml"));
    configClean_ = !std::holds_alternative<domain::Error>(removed);
}
void ServiceHost::confirmStopped(QLocalSocket* socket, const QString& id, int attempt) {
    if (!socket || owner_ != socket)
        return;
    if (!core_.running() && !validator_.running() && configClean_ && tunClean()) {
        answer(
            socket, id,
            QJsonObject{{QStringLiteral("stopped"), true}, {QStringLiteral("epoch"), QString{}}});
        return;
    }
    if (attempt >= 20) {
        answer(socket, id, failure("ServiceCleanupUncertain"));
        return;
    }
    QPointer<QLocalSocket> peer(socket);
    QTimer::singleShot(100, this, [this, peer, id, attempt] {
        if (peer)
            confirmStopped(peer, id, attempt + 1);
    });
}
void ServiceHost::shutdown() {
    stop("Shutdown");
    server_.close();
}
} // namespace quickclash::platform
