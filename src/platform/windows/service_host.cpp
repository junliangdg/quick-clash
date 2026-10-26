#include "platform/windows/service_host.h"
#include "infra/config/config_preflight.h"
#include "infra/config/geo_resources.h"
#include "infra/io/bounded_read.h"
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
            if (!authenticated(socket) || buffers_.size() >= 4) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            buffers_.insert(socket, {});
            connect(socket, &QIODevice::readyRead, this, [this, socket] { receive(socket); });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                if (owner_ == socket) {
                    stop();
                    owner_.clear();
                }
                buffers_.remove(socket);
                ids_.remove(socket);
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
    server_.setSocketOptions(QLocalServer::UserAccessOption | QLocalServer::GroupAccessOption |
                             QLocalServer::OtherAccessOption);
    if (!server_.listen(QStringLiteral("QuickClash.Service.v1")))
        return false;
    tick_.start();
    heartbeat_.start();
    return true;
}
bool ServiceHost::authenticated(QLocalSocket* socket) const {
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
    if (token)
        CloseHandle(token);
    CloseHandle(process);
    if (!sessionRead || session == 0 || session != WTSGetActiveConsoleSessionId())
        return false;
    const auto expected = QDir(installation()).filePath(QStringLiteral("quickclash.exe"));
    return QDir::cleanPath(QString::fromWCharArray(path, static_cast<int>(length)))
               .compare(expected, Qt::CaseInsensitive) == 0;
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
        bool validId = false;
        const auto numeric = id.toULongLong(&validId);
        if (!validId || request.value(QStringLiteral("protocolVersion")).toInt() != 1 ||
            !fixedFields(request, {QStringLiteral("protocolVersion"), QStringLiteral("requestId"),
                                   QStringLiteral("sessionId"), QStringLiteral("action"),
                                   QStringLiteral("payload")})) {
            answer(socket, id, failure("ServiceProtocolMismatch"));
            continue;
        }
        if (owner_ && owner_ != socket) {
            answer(socket, id, failure("ServiceSessionBusy"));
            continue;
        }
        if (!owner_) {
            owner_ = socket;
            results_.clear();
            resultOrder_.clear();
            pending_.clear();
            requests_.clear();
            requestWindow_.start();
            requestCount_ = 0;
        }
        lastHeartbeat_.restart();
        const auto fingerprint = QCryptographicHash::hash(
            QJsonDocument(request).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
        if (requests_.contains(id) && requests_.value(id) != fingerprint) {
            socket->abort();
            return;
        }
        if (results_.contains(id)) {
            socket->write(QJsonDocument(results_.value(id)).toJson(QJsonDocument::Compact) + '\n');
            continue;
        }
        if (pending_.contains(id))
            continue;
        if (numeric <= ids_.value(socket) || pending_.size() >= 4) {
            answer(socket, id, failure("ServiceReplayRejected"));
            continue;
        }
        if (requestWindow_.elapsed() >= 1000) {
            requestWindow_.restart();
            requestCount_ = 0;
        }
        if (++requestCount_ > 24) {
            socket->abort();
            return;
        }
        ids_.insert(socket, numeric);
        pending_.insert(id);
        requests_.insert(id, fingerprint);
        const auto action = request.value(QStringLiteral("action")).toString();
        if (ready_ && action != QStringLiteral("status") && action != QStringLiteral("heartbeat") &&
            action != QStringLiteral("resources") && action != QStringLiteral("stop") &&
            request.value(QStringLiteral("sessionId")).toString() != epoch_) {
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
        pending_.remove(id);
        results_.insert(id, response);
        resultOrder_.append(id);
        while (resultOrder_.size() > 32) {
            const auto expired = resultOrder_.takeFirst();
            results_.remove(expired);
            requests_.remove(expired);
        }
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
        const auto sample = [](HANDLE process) {
            PROCESS_MEMORY_COUNTERS_EX2 counters{};
            counters.cb = sizeof(counters);
            if (!GetProcessMemoryInfo(process,
                                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                      sizeof(counters)))
                return QJsonObject{};
            return QJsonObject{
                {QStringLiteral("privateWorkingSet"),
                 QString::number(counters.PrivateWorkingSetSize)},
                {QStringLiteral("workingSet"), QString::number(counters.WorkingSetSize)},
                {QStringLiteral("privateCommit"), QString::number(counters.PrivateUsage)}};
        };
        QJsonObject result{{QStringLiteral("service"), sample(GetCurrentProcess())}};
        if (core_.running()) {
            const auto handle =
                OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, core_.pid());
            if (handle) {
                result.insert(QStringLiteral("core"), sample(handle));
                CloseHandle(handle);
            }
        }
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
                           {QStringLiteral("tun"), ready_ && tunObserved()},
                           {QStringLiteral("pid"), static_cast<int>(core_.pid())},
                           {QStringLiteral("epoch"), epoch_},
                           {QStringLiteral("clean"), !core_.running() && !busy_ && tunClean()}});
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
            logBuffer_ = {};
            droppedLogs_ = 0;
            answer(socket, id, QJsonObject{});
            return;
        }
        if (!ready_ || busy_) {
            answer(socket, id, failure("ServiceCoreNotReady"));
            return;
        }
        if (!logStream_) {
            logStream_ = infra::pipeJsonStream(this, pipe_, "/logs?level=info",
                                               [this](const QJsonObject& entry) {
                                                   logBuffer_.append(entry);
                                                   while (logBuffer_.size() > 200) {
                                                       logBuffer_.removeFirst();
                                                       ++droppedLogs_;
                                                   }
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
        if (!logStream_ && !logBuffer_.isEmpty()) {
            logBuffer_.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("warning")},
                {QStringLiteral("payload"), QStringLiteral("Service log stream disconnected")}});
        }
        answer(socket, id,
               QJsonObject{{QStringLiteral("entries"), logBuffer_},
                           {QStringLiteral("dropped"), droppedLogs_}});
        logBuffer_ = {};
        droppedLogs_ = 0;
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
    if (!payload.isEmpty()) {
        answer(socket, id, failure("ServiceFieldsRejected"));
        return;
    }
    if (action == QStringLiteral("groups")) {
        QPointer<QLocalSocket> peer(socket);
        infra::pipeHttp(this, pipe_, "GET", "/proxies", {},
                        [this, peer, id](domain::Result<QJsonObject> result) {
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
    if (busy_ || core_.running() ||
        !fixedFields(payload, {QStringLiteral("yaml"), QStringLiteral("mixedPort"),
                               QStringLiteral("mode"), QStringLiteral("dnsMode")})) {
        answer(socket, id, failure("ServiceStartRejected"));
        return;
    }
    const auto encoded = payload.value(QStringLiteral("yaml")).toString();
    if (encoded.size() > 23 * 1024 * 1024) {
        answer(socket, id, failure("ServiceConfigLimit"));
        return;
    }
    const auto decoded =
        QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    domain::RuntimePreferences pref;
    const auto port = payload.value(QStringLiteral("mixedPort")).toInt();
    if (!decoded || port < 1024 || port > 65535) {
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
        root.remove("external-controller");
        root["external-controller-pipe"] =
            QDir::toNativeSeparators(QStringLiteral("//./pipe/") + pipe_).toStdString();
        root["secret"] = QUuid::createUuid().toString().toStdString();
        root["tun"]["enable"] = true;
        root["tun"]["device"] = "QuickClash";
        root["tun"]["stack"] = "mixed";
        root["tun"]["auto-route"] = true;
        root["tun"]["auto-detect-interface"] = true;
        root["tun"]["dns-hijack"].push_back("any:53");
        root["dns"]["enable"] = true;
        if (!root["dns"]["nameserver"])
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
    epoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto saved = store_.writeBytes(QStringLiteral("session/config.yaml"), configBytes, true);
    if (const auto* error = std::get_if<domain::Error>(&saved)) {
        answer(socket, id, *error);
        return;
    }
    const auto resources = infra::prepareGeoResources(configBytes, directory_);
    if (const auto* error = std::get_if<domain::Error>(&resources)) {
        answer(socket, id, *error);
        return;
    }
    busy_ = true;
    mixedPort_ = pref.mixedPort;
    QPointer<QLocalSocket> peer(socket);
    validationConnection_ =
        connect(&validator_, &OwnedProcess::finished, this, [this, peer, id](int exit) {
            disconnect(validationConnection_);
            if (!peer || owner_ != peer || !busy_)
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
    QTimer::singleShot(12000, this, [this, peer, id] {
        if (peer == owner_ && busy_ && pending_.contains(id)) {
            stop();
            answer(peer, id, failure("ServiceStartTimeout"));
        }
    });
}
void ServiceHost::probe(QLocalSocket* socket, const QString& id, int attempt) {
    if (owner_ != socket || !busy_)
        return;
    QPointer<QLocalSocket> peer(socket);
    infra::pipeHttp(
        this, pipe_, "GET", "/configs", {},
        [this, peer, id, attempt](domain::Result<QJsonObject> result) {
            if (!peer || owner_ != peer || !busy_)
                return;
            const auto* value = std::get_if<QJsonObject>(&result);
            if (value && value->value(QStringLiteral("mixed-port")).toInt() == mixedPort_ &&
                value->value(QStringLiteral("tun"))
                    .toObject()
                    .value(QStringLiteral("enable"))
                    .toBool() &&
                core_.ownsListener(mixedPort_) && tunObserved()) {
                busy_ = false;
                ready_ = true;
                answer(peer, id,
                       QJsonObject{{QStringLiteral("version"), QStringLiteral("v1.19.33")},
                                   {QStringLiteral("pid"), static_cast<int>(core_.pid())},
                                   {QStringLiteral("epoch"), epoch_},
                                   {QStringLiteral("tun"), true}});
                return;
            }
            if (attempt >= 12 || !core_.running()) {
                stop();
                answer(peer, id, failure("ServiceTunReadbackFailed"));
                return;
            }
            QTimer::singleShot(250, this, [this, peer, id, attempt] {
                if (peer)
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
    logBuffer_ = {};
    droppedLogs_ = 0;
    disconnect(validationConnection_);
    busy_ = false;
    ready_ = false;
    validator_.stop();
    core_.stop();
    groups_ = {};
    const auto removed = store_.remove(QStringLiteral("session/config.yaml"));
    Q_UNUSED(removed)
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
    if (!core_.running() && tunClean()) {
        answer(socket, id, QJsonObject{{QStringLiteral("stopped"), true}});
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
