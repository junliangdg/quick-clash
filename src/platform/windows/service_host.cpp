#include "platform/windows/service_host.h"
#include "infra/config/config_preflight.h"
#include "infra/config/geo_resources.h"
#include "infra/core/provider_command.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/runtime_provider_cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QStandardPaths>
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
ServiceHost::ServiceHost(QObject* parent) : QObject(parent), store_(serviceData()) {
    corePath_ = QDir(installation()).absoluteFilePath(QStringLiteral("../core/mihomo.exe"));
    directory_ = store_.root() + QStringLiteral("/session");
    configPath_ = directory_ + QStringLiteral("/config.yaml");
    heartbeat_.setInterval(1000);
    connect(&heartbeat_, &QTimer::timeout, this, [this] {
        // A long scheduler gap includes suspend. Give the client a fresh handshake window.
        if (tick_.isValid() && tick_.restart() > 15000)
            lastHeartbeat_.restart();
        if (owner_ && lastHeartbeat_.isValid() && lastHeartbeat_.elapsed() > 60000) {
            stop();
            owner_->disconnectFromServer();
        }
    });
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            auto* socket = server_.nextPendingConnection();
            if (buffers_.size() >= 4 || !authenticated(socket)) {
                identities_.remove(socket);
                socket->abort();
                socket->deleteLater();
                continue;
            }
            buffers_.insert(socket, {});
            socket->setReadBufferSize(infra::kReadChunkSize);
            connect(socket, &QIODevice::readyRead, this, [this, socket] { receive(socket); });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                if (owner_ == socket) {
                    stop();
                    owner_.clear();
                }
                buffers_.remove(socket);
                identities_.remove(socket);
                sessions_.remove(socket);
                if (!owner_)
                    protocol_.clear();
                socket->deleteLater();
            });
        }
    });
    connect(&core_, &OwnedProcess::finished, this, [this](int) { ready_ = false; });
}
bool ServiceHost::listen() {
    // Installation is administrator-owned. Never run a service from a user checkout.
    const auto programFiles = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles"));
    if (programFiles.isEmpty() ||
        !installation().startsWith(QDir::cleanPath(programFiles) + '/', Qt::CaseInsensitive) ||
        serviceData().isEmpty() || !protectedPath(QCoreApplication::applicationFilePath()) ||
        !protectedPath(corePath_) || !protectedPath(QFileInfo(serviceData()).absolutePath()))
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
    // SCM process death closes the Job. Never reuse its configuration after restart.
    // Refuse to listen if kernel-owned routes or protected-file cleanup remain uncertain.
    if (!tunClean())
        return false;
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
bool ServiceHost::authenticated(QLocalSocket* socket) {
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket->socketDescriptor()), &pid))
        return false;
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    wchar_t path[32768]{};
    DWORD length = 32768;
    HANDLE token = nullptr;
    const bool queried = QueryFullProcessImageNameW(process, 0, path, &length) &&
                         OpenProcessToken(process, TOKEN_QUERY, &token);
    DWORD session = 0, size = 0;
    const bool sessionRead =
        queried && GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &size);
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
    const auto identity = sidRead && timeRead ? QStringLiteral("%1:%2:%3:%4:%5")
                                                    .arg(QString::fromWCharArray(sid))
                                                    .arg(session)
                                                    .arg(pid)
                                                    .arg(created.dwHighDateTime)
                                                    .arg(created.dwLowDateTime)
                                                    .toUtf8()
                                              : QByteArray{};
    if (sid)
        LocalFree(sid);
    if (token)
        CloseHandle(token);
    CloseHandle(process);
    if (!sessionRead || identity.isEmpty() || session == 0 ||
        session != WTSGetActiveConsoleSessionId() ||
        (identities_.contains(socket) && identities_.value(socket) != identity))
        return false;
    const auto expected = QDir(installation()).filePath(QStringLiteral("quickclash.exe"));
    if (QDir::cleanPath(QString::fromWCharArray(path, static_cast<int>(length)))
                .compare(expected, Qt::CaseInsensitive) != 0 ||
        !protectedPath(expected))
        return false;
    identities_.insert(socket, identity);
    return true;
}
void ServiceHost::receive(QLocalSocket* socket) {
    if (!authenticated(socket)) {
        socket->abort();
        return;
    }
    auto& buffer = buffers_[socket];
    if (!infra::appendAvailable(*socket, buffer, 24 * 1024 * 1024)) {
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
            auto capabilities = infra::ServiceProtocol::capabilities();
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
    if (const auto* error = std::get_if<domain::Error>(&result))
        response.insert(QStringLiteral("error"), error->code);
    else
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
        stop();
        confirmStopped(socket, id, 0);
        return;
    }
    if (action == QStringLiteral("status") && payload.isEmpty()) {
        answer(socket, id,
               QJsonObject{{QStringLiteral("ready"), ready_},
                           {QStringLiteral("tun"), ready_ && tunDesired_ && tunObserved()},
                           {QStringLiteral("pid"), static_cast<int>(core_.pid())},
                           {QStringLiteral("epoch"), epoch_},
                           {QStringLiteral("clean"), !core_.running() && !validator_.running() &&
                                                         !busy_ && configClean_ && tunClean()}});
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
    if (busy_ || core_.running() || !configClean_ || !tunClean() ||
        !fixedFields(payload,
                     {QStringLiteral("yaml"), QStringLiteral("mixedPort"), QStringLiteral("mode"),
                      QStringLiteral("dnsMode"), QStringLiteral("tun")})) {
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
        root["tun"]["device"] = "QuickClash";
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
    const auto resources = infra::prepareGeoResources(configBytes, directory_);
    if (const auto* error = std::get_if<domain::Error>(&resources)) {
        answer(socket, id, *error);
        return;
    }
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
            const auto launched = core_.start(
                corePath_, {QStringLiteral("-f"), configPath_, QStringLiteral("-d"), directory_},
                directory_, false, environment);
            if (const auto* error = std::get_if<domain::Error>(&launched)) {
                busy_ = false;
                answer(peer, id, *error);
                return;
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
            stop();
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
            if (value && value->value(QStringLiteral("mixed-port")).toInt() == mixedPort_ &&
                value->value(QStringLiteral("tun"))
                        .toObject()
                        .value(QStringLiteral("enable"))
                        .toBool() == tunDesired_ &&
                core_.ownsListener(mixedPort_) && (!tunDesired_ || tunObserved())) {
                busy_ = false;
                ready_ = true;
                answer(peer, id,
                       QJsonObject{{QStringLiteral("version"), QStringLiteral("v1.19.33")},
                                   {QStringLiteral("pid"), static_cast<int>(core_.pid())},
                                   {QStringLiteral("epoch"), epoch_},
                                   {QStringLiteral("tun"), tunDesired_}});
                return;
            }
            if (attempt >= 12 || !core_.running()) {
                stop();
                answer(peer, id, failure("ServiceTunReadbackFailed"));
                return;
            }
            QTimer::singleShot(250, this, [this, peer, id, attempt, epoch] {
                if (peer && epoch == epoch_)
                    probe(peer, id, attempt + 1);
            });
        });
}
bool ServiceHost::tunObserved() const {
    ULONG size = 0;
    (void)GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size);
    if (!size || size > 1024 * 1024)
        return false;
    QByteArray buffer(static_cast<qsizetype>(size), Qt::Uninitialized);
    auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter, &size) !=
        NO_ERROR)
        return false;
    NET_LUID luid{};
    for (; adapter; adapter = adapter->Next)
        if (QString::fromWCharArray(adapter->FriendlyName) == QStringLiteral("QuickClash") &&
            adapter->OperStatus == IfOperStatusUp) {
            luid = adapter->Luid;
            break;
        }
    if (!luid.Value)
        return false;
    PMIB_IPFORWARD_TABLE2 routes = nullptr;
    if (GetIpForwardTable2(AF_UNSPEC, &routes) != NO_ERROR)
        return false;
    bool found = false;
    for (ULONG i = 0; i < routes->NumEntries; ++i)
        found |= routes->Table[i].InterfaceLuid.Value == luid.Value &&
                 routes->Table[i].DestinationPrefix.PrefixLength <= 1;
    FreeMibTable(routes);
    return found;
}
void ServiceHost::stop() {
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
bool ServiceHost::tunClean() const {
    ULONG size = 0;
    (void)GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &size);
    if (!size || size > 1024 * 1024)
        return false;
    QByteArray data(static_cast<qsizetype>(size), Qt::Uninitialized);
    auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data());
    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapter, &size) != NO_ERROR)
        return false;
    NET_LUID luid{};
    for (; adapter; adapter = adapter->Next) {
        if (QString::fromWCharArray(adapter->FriendlyName) == QStringLiteral("QuickClash")) {
            if (adapter->OperStatus == IfOperStatusUp)
                return false;
            luid = adapter->Luid;
        }
    }
    if (!luid.Value)
        return true;
    PMIB_IPFORWARD_TABLE2 routes = nullptr;
    if (GetIpForwardTable2(AF_UNSPEC, &routes) != NO_ERROR)
        return false;
    bool clean = true;
    for (ULONG i = 0; i < routes->NumEntries; ++i)
        clean &= routes->Table[i].InterfaceLuid.Value != luid.Value ||
                 routes->Table[i].DestinationPrefix.PrefixLength > 1;
    FreeMibTable(routes);
    return clean;
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
    stop();
    server_.close();
}
} // namespace quickclash::platform
