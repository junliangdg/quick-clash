#include "infra/core/engine_session.h"
#include "infra/core/listener_ports.h"
#include "infra/core/service_protocol.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/json_store.h"
#include "platform/core_artifacts.h"
#include "platform/desktop_services.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

namespace quickclash::infra {
namespace {
domain::Error coreError(const char* code) {
    auto error = storageError(code);
    error.category = domain::ErrorCategory::Core;
    error.stage = QStringLiteral("core");
    return error;
}
} // namespace
EngineSession::EngineSession(QObject* parent)
    : EngineSession(platform::makeDesktopProcess(), parent) {}
EngineSession::EngineSession(std::unique_ptr<application::IDesktopProcess> process, QObject* parent)
    : QObject(parent), process_(std::move(process)) {
    servicePoll_.setInterval(1000);
    connect(&servicePoll_, &QTimer::timeout, this, [this] {
        if (!serviceMode_ || !ready_)
            return;
        syncServiceLogs();
        const auto generation = generation_;
        service_.command(QStringLiteral("status"), {},
                         [this, generation](domain::Result<QJsonObject> result) {
                             if (generation != generation_)
                                 return;
                             const auto health = interpretServiceStatus(result);
                             if (health.lifecycle == CoreLifecycle::Unknown) {
                                 // A timed-out or rejected status request is control-plane
                                 // uncertainty, not proof that the owned core exited. Keep the
                                 // session and only surface degraded TUN observation after
                                 // repeated failures.
                                 if (++statusFailures_ >= 3)
                                     setTunState(tunTracker_.observe(QStringLiteral("Unknown")));
                                 return;
                             }
                             statusFailures_ = 0;
                             if (health.lifecycle == CoreLifecycle::Exited) {
                                 coreExitDiagnostics_ = health.exitDiagnostics;
                                 stop();
                                 emit stopped(true);
                                 return;
                             }
                             setTunState(tunTracker_.observe(health.tunState));
                         });
        for (const auto& name : serviceStreams_) {
            const auto streamGeneration = streamGenerations_.value(name);
            if (name == QStringLiteral("logs") && !serviceLogDemand_.receiving(streamGeneration))
                continue;
            service_.command(
                name, {},
                [this, name, generation, streamGeneration](domain::Result<QJsonObject> result) {
                    if (generation != generation_ || !serviceMode_ ||
                        streamGeneration != streamGenerations_.value(name) ||
                        !serviceStreams_.contains(name))
                        return;
                    if (const auto* value = std::get_if<QJsonObject>(&result)) {
                        if (name == QStringLiteral("logs")) {
                            for (const auto& entry :
                                 value->value(QStringLiteral("entries")).toArray())
                                emit streamData(name, entry.toObject());
                            if (value->value(QStringLiteral("dropped")).toInt() > 0)
                                emit streamData(
                                    name,
                                    {{QStringLiteral("source"), QStringLiteral("Service")},
                                     {QStringLiteral("dropped"),
                                      value->value(QStringLiteral("dropped"))},
                                     {QStringLiteral("type"), QStringLiteral("warning")},
                                     {QStringLiteral("payload"),
                                      QStringLiteral("Service log buffer dropped old entries")}});
                        } else
                            emit streamData(name, *value);
                    }
                });
        }
    });
    connect(&service_, &ServiceClient::lost, this, [this](const QString& reason) {
        if (!serviceMode_)
            return;
        const bool unexpected = ready_;
        stop();
        emit stopped(unexpected);
        if (unexpected)
            return;
        // Only failures the user can act on keep their specific code; transport noise stays
        // generic so the UI never shows an internal identifier.
        const auto actionable = reason == QStringLiteral("ServiceInstallationRequired") ||
                                reason == QStringLiteral("ServiceNotRunning") ||
                                reason == QStringLiteral("ServiceClientUntrusted") ||
                                reason == QStringLiteral("ServiceProtocolMismatch") ||
                                reason == QStringLiteral("ServiceSessionBusy");
        emit failed(actionable ? reason : QStringLiteral("ServiceUnavailable"));
    });
    network_.setProxy(QNetworkProxy::NoProxy);
    probeTimer_.setInterval(150);
    deadline_.setSingleShot(true);
    deadline_.setInterval(10000);
    connect(&probeTimer_, &QTimer::timeout, this, &EngineSession::probe);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        fail(configPending_      ? QStringLiteral("CoreConfigReadbackFailed")
             : listenersPending_ ? QStringLiteral("CoreListenersNotOwned")
                                 : QStringLiteral("CoreReadyTimeout"));
    });
    connect(process_.get(), &application::IDesktopProcess::finished, this, [this](int) {
        const bool unexpected = ready_;
        stop();
        emit stopped(unexpected);
        if (!unexpected) {
            emit failed(QStringLiteral("CoreExitedDuringStart"));
        }
    });
}
EngineSession::~EngineSession() {
    serviceMode_ = false;
    service_.close();
    stop();
}
void EngineSession::start(const QString& executable, const QString& config,
                          const QString& directory, const QByteArray& secret, quint16 mixedPort,
                          quint16 controllerPort, const QString& expectedMode, bool serviceMode,
                          const QJsonObject& geoSnapshot, const QString& storageRoot,
                          const QString& coreArtifactId) {
    const auto platform = platform::desktopPlatformInfo();
    if (platform.capabilities.value(QStringLiteral("core")) != QStringLiteral("Supported")) {
        emit failed(QStringLiteral("CorePlatformUnsupported"));
        return;
    }
    if (serviceMode && serviceCleanupUncertain_) {
        emit failed(QStringLiteral("ServiceCleanupUncertain"));
        return;
    }
    if (serviceStopping_) {
        afterServiceStop_ = [this, executable, config, directory, secret, mixedPort, controllerPort,
                             expectedMode, serviceMode, geoSnapshot, storageRoot, coreArtifactId] {
            start(executable, config, directory, secret, mixedPort, controllerPort, expectedMode,
                  serviceMode, geoSnapshot, storageRoot, coreArtifactId);
        };
        return;
    }
    if (running()) {
        emit failed(QStringLiteral("CoreAlreadyRunning"));
        return;
    }
    if (secret.size() < 32 || mixedPort < 1024 || controllerPort < 1024 ||
        mixedPort == controllerPort) {
        emit failed(QStringLiteral("InvalidCoreSession"));
        return;
    }
    if (!platform::verifyCoreArtifact(executable, coreArtifactId)) {
        emit failed(QStringLiteral("CoreIntegrityFailed"));
        return;
    }
    secret_ = secret;
    coreExitDiagnostics_ = {};
    statusFailures_ = 0;
    tunTracker_.reset();
    setTunState(QStringLiteral("Off"));
    listenerIssue_ = {};
    mixedPort_ = mixedPort;
    controllerPort_ = controllerPort;
    expectedMode_ = expectedMode;
    expectedVersion_ =
        platform::coreArtifact(coreArtifactId).value(QStringLiteral("version")).toString();
    ++generation_;
    if (serviceMode) {
        QFile source(config);
        if (!source.open(QIODevice::ReadOnly) || source.size() > 16 * 1024 * 1024) {
            fail(QStringLiteral("ServiceConfigRejected"));
            return;
        }
        const auto loaded = readFileBounded(source, 16 * 1024 * 1024);
        if (!loaded.ok) {
            fail(QStringLiteral("ServiceConfigRejected"));
            return;
        }
        serviceMode_ = true;
        const auto generation = generation_;
        service_.command(
            QStringLiteral("start"),
            {{QStringLiteral("yaml"), QString::fromLatin1(loaded.bytes.toBase64())},
             {QStringLiteral("mixedPort"), mixedPort},
             {QStringLiteral("mode"), expectedMode},
             {QStringLiteral("tun"), true},
             {QStringLiteral("dnsMode"), QStringLiteral("profile")},
             {QStringLiteral("coreArtifactId"), coreArtifactId},
             {QStringLiteral("geo"), QJsonObject{{QStringLiteral("snapshot"), geoSnapshot},
                                                 {QStringLiteral("storageRoot"), storageRoot}}}},
            [this, generation](domain::Result<QJsonObject> result) {
                if (generation != generation_)
                    return;
                if (const auto* error = std::get_if<domain::Error>(&result)) {
                    listenerIssue_ =
                        QJsonDocument::fromJson(error->redactedDetail.toUtf8()).object();
                    fail(error->code);
                    return;
                }
                const auto value = std::get<QJsonObject>(result);
                if (value.value(QStringLiteral("version")).toString() != expectedVersion_) {
                    fail(QStringLiteral("CoreVersionMismatch"));
                    return;
                }
                if (!value.value(QStringLiteral("tun")).toBool()) {
                    fail(QStringLiteral("ServiceTunReadbackFailed"));
                    return;
                }
                servicePid_ = static_cast<quint32>(value.value(QStringLiteral("pid")).toInt());
                tunDevice_ = value.value(QStringLiteral("tunDevice")).toString();
                ready_ = true;
                setTunState(QStringLiteral("On"));
                servicePoll_.start();
                emit becameReady(value.value(QStringLiteral("version")).toString());
                refreshGroups();
            });
        return;
    }
    {
        QTcpServer mixed;
        QTcpServer controller;
        if (!mixed.listen(QHostAddress::LocalHost, mixedPort))
            listenerIssue_ = listenerPortIssue(mixed, mixedPort, QStringLiteral("mixed"));
        else if (!controller.listen(QHostAddress::LocalHost, controllerPort))
            listenerIssue_ =
                listenerPortIssue(controller, controllerPort, QStringLiteral("controller"));
        if (!listenerIssue_.isEmpty()) {
            fail(listenerIssue_.value(QStringLiteral("code")).toString());
            return;
        }
    }
    const auto result = process_->start(
        executable, {QStringLiteral("-f"), config, QStringLiteral("-d"), directory}, directory);
    if (const auto* error = std::get_if<domain::Error>(&result)) {
        fail(error->code);
        return;
    }
    probeTimer_.start();
    deadline_.start();
    probe();
}
void EngineSession::stop() {
    ++generation_;
    groupsPending_ = false;
    groupsQueued_ = false;
    ready_ = false;
    setTunState(QStringLiteral("Off"));
    const bool stopService = serviceMode_;
    serviceMode_ = false;
    servicePid_ = 0;
    tunDevice_.clear();
    servicePoll_.stop();
    serviceStreams_.clear();
    serviceLogDemand_ = {};
    afterServiceStop_ = {};
    if (stopService && !serviceStopping_ && service_.hadSession()) {
        serviceStopping_ = true;
        auto completeStop = [this](domain::Result<QJsonObject> result) {
            serviceStopping_ = false;
            auto continuation = std::move(afterServiceStop_);
            afterServiceStop_ = {};
            service_.close();
            if (const auto* error = std::get_if<domain::Error>(&result)) {
                serviceCleanupUncertain_ = true;
                emit failed(error->code);
                return;
            }
            if (continuation)
                continuation();
        };
        // Starting may have allocated a new epoch before its final response. Read the
        // authoritative state before stopping; never guess or reuse the previous epoch.
        service_.command(QStringLiteral("status"), {},
                         [this, completeStop](domain::Result<QJsonObject> state) {
                             if (std::holds_alternative<domain::Error>(state))
                                 completeStop(std::move(state));
                             else
                                 service_.command(QStringLiteral("stop"), {}, completeStop);
                         });
    } else if (!serviceStopping_)
        service_.close();
    probing_ = false;
    listenersPending_ = false;
    configPending_ = false;
    probeTimer_.stop();
    deadline_.stop();
    const auto streams = streams_;
    streams_.clear();
    for (const auto& stream : streams)
        if (stream)
            stream->abort();
    const auto replies = replies_;
    replies_.clear();
    for (const auto& reply : replies) {
        if (reply) {
            reply->abort();
        }
    }
    process_->stop();
    secret_.fill('\0');
    secret_.clear();
}
void EngineSession::setTunState(const QString& state) {
    if (tunState_ == state)
        return;
    tunState_ = state;
    emit tunStateChanged(state);
}
void EngineSession::probeService(ApiHandler handler) {
    service_.command(QStringLiteral("status"), {},
                     [this, handler = std::move(handler)](domain::Result<QJsonObject> result) {
                         if (const auto* value = std::get_if<QJsonObject>(&result);
                             value && value->value(QStringLiteral("clean")).toBool()) {
                             serviceCleanupUncertain_ = false;
                             service_.command(QStringLiteral("tun_preflight"), {},
                                              std::move(handler));
                         } else if (std::holds_alternative<QJsonObject>(result)) {
                             serviceCleanupUncertain_ = true;
                             handler(coreError("ServiceCleanupUncertain"));
                         } else
                             handler(std::move(result));
                     });
}
void EngineSession::serviceResources(ApiHandler handler) {
    service_.command(QStringLiteral("resources"), {}, std::move(handler));
}
void EngineSession::syncServiceLogs() {
    if (!serviceMode_ || !ready_)
        return;
    const auto streamGeneration = streamGenerations_.value(QStringLiteral("logs"));
    const auto enabled =
        serviceLogDemand_.begin(streamGeneration, serviceStreams_.contains(QStringLiteral("logs")));
    if (!enabled)
        return;
    const auto generation = generation_;
    service_.command(QStringLiteral("log_demand"), {{QStringLiteral("enabled"), *enabled}},
                     [this, generation, streamGeneration,
                      enabled = *enabled](domain::Result<QJsonObject> result) {
                         if (generation != generation_ || !serviceMode_)
                             return;
                         const bool success = std::holds_alternative<QJsonObject>(result);
                         serviceLogDemand_.finish(streamGeneration, enabled, success);
                         if (success)
                             syncServiceLogs();
                     });
}
void EngineSession::api(const QByteArray& method, const QByteArray& path, const QJsonObject& body,
                        ApiHandler handler) {
    if (!ready_) {
        handler(coreError("CoreNotReady"));
        return;
    }
    request(method, path, body, std::move(handler));
}
void EngineSession::setStream(const QString& name, bool enabled) {
    if (name != QStringLiteral("traffic") && name != QStringLiteral("logs"))
        return;
    const bool wasEnabled =
        serviceMode_ ? serviceStreams_.contains(name) : bool(streams_.value(name));
    if (wasEnabled != (enabled && ready_))
        ++streamGenerations_[name];
    if (serviceMode_) {
        if (enabled && ready_)
            serviceStreams_.insert(name);
        else
            serviceStreams_.remove(name);
        if (name == QStringLiteral("logs"))
            syncServiceLogs();
        return;
    }
    if (!enabled || !ready_) {
        const auto reply = streams_.take(name);
        if (reply)
            reply->abort();
        return;
    }
    if (streams_.value(name))
        return;
    const auto generation = generation_;
    const auto path = name == QStringLiteral("logs") ? "/logs?level=info" : "/traffic";
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                                     .arg(controllerPort_)
                                     .arg(QString::fromLatin1(path))));
    request.setRawHeader("Authorization", "Bearer " + secret_);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(5000);
    auto* reply = network_.get(request);
    reply->setReadBufferSize(65537);
    streams_.insert(name, reply);
    auto buffer = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [this, reply, buffer, generation, name] {
        if (generation != generation_ || streams_.value(name) != reply)
            return;
        if (!appendAvailable(*reply, *buffer, 65536)) {
            reply->abort();
            return;
        }
        qsizetype newline = 0;
        while ((newline = buffer->indexOf('\n')) >= 0) {
            const auto line = buffer->first(newline);
            buffer->remove(0, newline + 1);
            const auto document = QJsonDocument::fromJson(line);
            if (document.isObject())
                emit streamData(name, document.object());
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, name] {
        if (generation == generation_ && streams_.value(name) == reply)
            streams_.remove(name);
        reply->deleteLater();
    });
}
void EngineSession::fail(const QString& code) {
    stop();
    emit failed(code);
}
void EngineSession::request(const QByteArray& method, const QByteArray& path,
                            const QJsonObject& body, ReplyHandler handler) {
    if (serviceMode_) {
        QString action;
        QJsonObject payload;
        if (method == "GET" && path == "/proxies")
            action = QStringLiteral("groups");
        else if (method == "GET" && path == "/rules")
            action = QStringLiteral("rules");
        else if (method == "GET" && path == "/connections")
            action = QStringLiteral("connections");
        else if (method == "GET" && path == "/providers/proxies")
            action = QStringLiteral("proxy_providers");
        else if (method == "GET" && path == "/providers/rules")
            action = QStringLiteral("rule_providers");
        else if (method == "PUT" &&
                 (path.startsWith("/providers/proxies/") || path.startsWith("/providers/rules/")) &&
                 body.isEmpty()) {
            const auto parts = path.split('/');
            if (parts.size() == 4 && !parts[3].isEmpty()) {
                action = QStringLiteral("refresh_provider");
                payload = {{QStringLiteral("kind"), QString::fromLatin1(parts[2])},
                           {QStringLiteral("name"), QUrl::fromPercentEncoding(parts[3])}};
            }
        } else if (method == "POST" && path == "/cache/dns/flush")
            action = QStringLiteral("flush_dns");
        else if (method == "DELETE" && path == "/connections")
            action = QStringLiteral("close_connections");
        else if (method == "DELETE" && path.startsWith("/connections/")) {
            action = QStringLiteral("close_connection");
            payload.insert(QStringLiteral("id"), QUrl::fromPercentEncoding(path.mid(13)));
        } else if (path.startsWith("/proxies/")) {
            const auto name = QUrl::fromPercentEncoding(path.mid(9).split('/').first());
            if (method == "PUT") {
                action = QStringLiteral("select");
                payload = {{QStringLiteral("group"), name},
                           {QStringLiteral("member"), body.value(QStringLiteral("name"))}};
            } else if (method == "GET" && path.contains("/delay?")) {
                const QUrl url(QStringLiteral("http://localhost") + QString::fromLatin1(path));
                action = QStringLiteral("delay");
                payload = {{QStringLiteral("name"), name},
                           {QStringLiteral("url"), QUrlQuery(url).queryItemValue(
                                                       QStringLiteral("url"), QUrl::FullyDecoded)}};
            } else if (method == "GET") {
                service_.command(
                    QStringLiteral("groups"), {},
                    [name, handler = std::move(handler)](domain::Result<QJsonObject> result) {
                        if (const auto* value = std::get_if<QJsonObject>(&result))
                            handler(value->value(QStringLiteral("proxies"))
                                        .toObject()
                                        .value(name)
                                        .toObject());
                        else
                            handler(std::get<domain::Error>(result));
                    });
                return;
            }
        }
        if (action.isEmpty()) {
            handler(coreError("ServiceActionRejected"));
            return;
        }
        service_.command(action, payload, std::move(handler));
        return;
    }
    if (replies_.size() >= kServiceRequestLimit) {
        handler(coreError("CoreRequestBusy"));
        return;
    }
    const auto generation = generation_;
    const auto url =
        QUrl::fromEncoded("http://127.0.0.1:" + QByteArray::number(controllerPort_) + path);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + secret_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    const bool providerRefresh = method == "PUT" && path.startsWith("/providers/");
    request.setTransferTimeout(providerRefresh ? 12000 : 3000);
    auto* reply = network_.sendCustomRequest(
        request, method,
        method == "GET" ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact));
    replies_.append(reply);
    reply->setReadBufferSize(kReadChunkSize);
    auto bytes = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, bytes, tooLarge] {
        if (!appendAvailable(*reply, *bytes, 2 * 1024 * 1024)) {
            *tooLarge = bytes->size() > 2 * 1024 * 1024;
            reply->abort();
        }
    });
    auto* timeout = new QTimer(reply);
    auto expired = std::make_shared<bool>(false);
    timeout->setSingleShot(true);
    timeout->start(providerRefresh ? 12500 : 3500);
    connect(timeout, &QTimer::timeout, reply, [reply, expired] {
        *expired = true;
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, tooLarge, expired, generation, handler = std::move(handler)] {
                replies_.removeAll(reply);
                reply->deleteLater();
                if (generation != generation_) {
                    return;
                }
                const auto readOk = appendAvailable(*reply, *bytes, 2 * 1024 * 1024);
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (*tooLarge || bytes->size() > 2 * 1024 * 1024) {
                    handler(coreError("CoreResponseTooLarge"));
                    return;
                }
                if (!readOk || reply->error() != QNetworkReply::NoError || status < 200 ||
                    status >= 300) {
                    handler(coreError(*expired || reply->error() == QNetworkReply::TimeoutError ||
                                              status == 504
                                          ? "CoreRequestTimeout"
                                      : status == 404 ? "CoreTargetNotFound"
                                      : status == 401 ? "CoreAuthenticationFailed"
                                                      : "CoreRequestFailed"));
                    return;
                }
                if (bytes->isEmpty()) {
                    handler(QJsonObject{});
                    return;
                }
                // Release transport storage before publishing or starting a dependent request.
                const auto document = QJsonDocument::fromJson(std::exchange(*bytes, {}));
                if (!document.isObject()) {
                    handler(coreError("CoreResponseInvalid"));
                    return;
                }
                handler(document.object());
            });
}
void EngineSession::probe() {
    if (probing_ || ready_ || !running()) {
        return;
    }
    probing_ = true;
    request("GET", "/version", {}, [this](domain::Result<QJsonObject> result) {
        if (std::holds_alternative<domain::Error>(result)) {
            probing_ = false;
            return;
        }
        const auto version =
            std::get<QJsonObject>(result).value(QStringLiteral("version")).toString();
        if (version != expectedVersion_) {
            fail(QStringLiteral("CoreVersionMismatch"));
            return;
        }
        request("GET", "/configs", {}, [this, version](domain::Result<QJsonObject> configs) {
            probing_ = false;
            if (std::holds_alternative<domain::Error>(configs)) {
                return;
            }
            const auto value = std::get<QJsonObject>(configs);
            // The controller may answer with defaults while Mihomo applies the configuration.
            configPending_ = value.value(QStringLiteral("mode")).toString() != expectedMode_ ||
                             value.value(QStringLiteral("mixed-port")).toInt() != mixedPort_ ||
                             value.value(QStringLiteral("allow-lan")).toBool(true) ||
                             value.value(QStringLiteral("tun"))
                                 .toObject()
                                 .value(QStringLiteral("enable"))
                                 .toBool();
            if (configPending_) {
                listenersPending_ = false;
                return;
            }
            // Mihomo exposes its controller before all inbound listeners finish starting.
            // Keep checking within the startup deadline; authentication alone is not Ready.
            listenersPending_ =
                !process_->ownsListener(mixedPort_) || !process_->ownsListener(controllerPort_);
            if (listenersPending_)
                return;
            ready_ = true;
            probeTimer_.stop();
            deadline_.stop();
            emit becameReady(version);
            refreshGroups();
        });
    });
}
void EngineSession::refreshGroups() {
    if (!ready_) {
        return;
    }
    if (groupsPending_) {
        groupsQueued_ = true;
        return;
    }
    groupsPending_ = true;
    const auto generation = generation_;
    const auto finish = [this, generation] {
        if (generation != generation_)
            return;
        groupsPending_ = false;
        if (std::exchange(groupsQueued_, false))
            refreshGroups();
    };
    request("GET", "/proxies", {}, [this, generation, finish](domain::Result<QJsonObject> result) {
        if (!ready_ || generation != generation_)
            return;
        if (const auto* value = std::get_if<QJsonObject>(&result)) {
            auto objects = value->value(QStringLiteral("proxies")).toObject();
            // /proxies omits provider-only nodes in the pinned core. Include their safe
            // descriptors for capabilities and conservative identity handling.
            request("GET", "/providers/proxies", {},
                    [this, generation, finish,
                     objects = std::move(objects)](domain::Result<QJsonObject> providers) mutable {
                        if (!ready_ || generation != generation_)
                            return;
                        if (const auto* response = std::get_if<QJsonObject>(&providers)) {
                            QJsonObject namesByProvider;
                            int nameCount = 0;
                            bool completeNames = true;
                            const auto entries = response->value("providers").toObject();
                            for (auto it = entries.begin(); it != entries.end(); ++it) {
                                const auto provider = it.value().toObject();
                                if (provider.value("vehicleType") != "HTTP")
                                    continue;
                                QJsonArray names;
                                for (const auto& value : provider.value("proxies").toArray()) {
                                    auto node = value.toObject();
                                    const auto name = node.value("name").toString();
                                    if (name.isEmpty())
                                        continue;
                                    if (objects.size() >= 5503) {
                                        completeNames = false;
                                        continue;
                                    }
                                    if (name.size() <= 512 && nameCount < 5000) {
                                        names.append(name);
                                        ++nameCount;
                                    } else {
                                        completeNames = false;
                                    }
                                    node.insert("providerDynamic", true);
                                    if (!objects.contains(name))
                                        objects.insert(name, node);
                                    else {
                                        auto existing = objects.value(name).toObject();
                                        existing.insert("providerDynamic", true);
                                        objects.insert(name, existing);
                                    }
                                }
                                if (namesByProvider.size() < 32)
                                    namesByProvider.insert(it.key(), names);
                                else
                                    completeNames = false;
                            }
                            emit providerNodeNamesChanged(completeNames ? namesByProvider
                                                                        : QJsonObject{});
                        } else {
                            // A partial read cannot prove static/dynamic name collisions safe
                            // for restoring saved selections. Keep the previous confirmed view.
                            emit groupsRefreshFailed(std::get<domain::Error>(providers).code);
                            finish();
                            return;
                        }
                        emit groupsChanged(objects);
                        finish();
                    });
        } else {
            emit groupsRefreshFailed(std::get<domain::Error>(result).code);
            finish();
        }
    });
}
void EngineSession::select(const QString& group, const QString& member) {
    if (!ready_ || group.isEmpty() || member.isEmpty()) {
        emit mutationFinished(QStringLiteral("select"), false);
        return;
    }
    const auto path = "/proxies/" + QUrl::toPercentEncoding(group);
    request("PUT", path, {{QStringLiteral("name"), member}},
            [this, group, member](domain::Result<QJsonObject>) {
                // Mutation response loss is resolved by readback, without repeating the PUT.
                request("GET", "/proxies/" + QUrl::toPercentEncoding(group), {},
                        [this, member](domain::Result<QJsonObject> result) {
                            const auto* object = std::get_if<QJsonObject>(&result);
                            emit mutationFinished(QStringLiteral("select"),
                                                  object && object->value(QStringLiteral("now")) ==
                                                                member);
                            refreshGroups();
                        });
            });
}
void EngineSession::setMode(const QString& mode) {
    if (!ready_ || (mode != QStringLiteral("rule") && mode != QStringLiteral("global") &&
                    mode != QStringLiteral("direct"))) {
        emit mutationFinished(QStringLiteral("mode"), false);
        return;
    }
    request("PATCH", "/configs", {{QStringLiteral("mode"), mode}},
            [this, mode](domain::Result<QJsonObject>) {
                request("GET", "/configs", {}, [this, mode](domain::Result<QJsonObject> result) {
                    const auto* object = std::get_if<QJsonObject>(&result);
                    emit mutationFinished(QStringLiteral("mode"),
                                          object && object->value(QStringLiteral("mode")) == mode);
                });
            });
}
void EngineSession::reload(const QString& config) {
    if (!ready_) {
        emit mutationFinished(QStringLiteral("reload"), false);
        return;
    }
    request("PUT", "/configs", {{QStringLiteral("path"), config}},
            [this](domain::Result<QJsonObject> result) {
                emit mutationFinished(QStringLiteral("reload"),
                                      std::holds_alternative<QJsonObject>(result));
            });
}
} // namespace quickclash::infra
