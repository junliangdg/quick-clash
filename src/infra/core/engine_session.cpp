#include "infra/core/engine_session.h"
#include "infra/storage/json_store.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QUrl>

namespace quickclash::infra {
namespace {
domain::Error coreError(const char* code) {
    auto error = storageError(code);
    error.category = domain::ErrorCategory::Core;
    error.stage = QStringLiteral("core");
    return error;
}
} // namespace
EngineSession::EngineSession(QObject* parent) : QObject(parent) {
    network_.setProxy(QNetworkProxy::NoProxy);
    probeTimer_.setInterval(150);
    deadline_.setSingleShot(true);
    deadline_.setInterval(10000);
    connect(&probeTimer_, &QTimer::timeout, this, &EngineSession::probe);
    connect(&deadline_, &QTimer::timeout, this,
            [this] { fail(QStringLiteral("CoreReadyTimeout")); });
    connect(&process_, &platform::OwnedProcess::finished, this, [this](int) {
        const bool unexpected = ready_;
        stop();
        emit stopped(unexpected);
        if (!unexpected) {
            emit failed(QStringLiteral("CoreExitedDuringStart"));
        }
    });
}
EngineSession::~EngineSession() {
    stop();
}
void EngineSession::start(const QString& executable, const QString& config,
                          const QString& directory, const QByteArray& secret, quint16 mixedPort,
                          quint16 controllerPort, const QString& expectedMode) {
    if (running()) {
        emit failed(QStringLiteral("CoreAlreadyRunning"));
        return;
    }
    if (secret.size() < 32 || mixedPort < 1024 || controllerPort < 1024 ||
        mixedPort == controllerPort) {
        emit failed(QStringLiteral("InvalidCoreSession"));
        return;
    }
    QFile binary(executable);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!binary.open(QIODevice::ReadOnly) || !hash.addData(&binary) ||
        hash.result().toHex() !=
            QByteArrayLiteral("1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a")) {
        emit failed(QStringLiteral("CoreIntegrityFailed"));
        return;
    }
    secret_ = secret;
    mixedPort_ = mixedPort;
    controllerPort_ = controllerPort;
    expectedMode_ = expectedMode;
    ++generation_;
    const auto result = process_.start(
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
    ready_ = false;
    probing_ = false;
    probeTimer_.stop();
    deadline_.stop();
    const auto replies = replies_;
    replies_.clear();
    for (const auto& reply : replies) {
        if (reply) {
            reply->abort();
        }
    }
    process_.stop();
    secret_.fill('\0');
    secret_.clear();
}
void EngineSession::fail(const QString& code) {
    stop();
    emit failed(code);
}
void EngineSession::request(const QByteArray& method, const QByteArray& path,
                            const QJsonObject& body, ReplyHandler handler) {
    if (replies_.size() >= 4) {
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
    request.setTransferTimeout(3000);
    auto* reply = network_.sendCustomRequest(
        request, method,
        method == "GET" ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact));
    replies_.append(reply);
    reply->setReadBufferSize(2 * 1024 * 1024 + 1);
    auto bytes = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, bytes, tooLarge] {
        bytes->append(reply->read(2 * 1024 * 1024 + 1 - bytes->size()));
        if (bytes->size() > 2 * 1024 * 1024) {
            *tooLarge = true;
            reply->abort();
        }
    });
    auto* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    timeout->start(3500);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, tooLarge, generation, handler = std::move(handler)] {
                replies_.removeAll(reply);
                reply->deleteLater();
                if (generation != generation_) {
                    return;
                }
                bytes->append(reply->read(2 * 1024 * 1024 + 1 - bytes->size()));
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (*tooLarge || bytes->size() > 2 * 1024 * 1024) {
                    handler(coreError("CoreResponseTooLarge"));
                    return;
                }
                if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
                    handler(coreError(status == 401 ? "CoreAuthenticationFailed"
                                                    : "CoreRequestFailed"));
                    return;
                }
                if (bytes->isEmpty()) {
                    handler(QJsonObject{});
                    return;
                }
                const auto document = QJsonDocument::fromJson(*bytes);
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
        if (version != QStringLiteral("v1.19.33")) {
            fail(QStringLiteral("CoreVersionMismatch"));
            return;
        }
        request("GET", "/configs", {}, [this, version](domain::Result<QJsonObject> configs) {
            probing_ = false;
            if (std::holds_alternative<domain::Error>(configs)) {
                return;
            }
            const auto value = std::get<QJsonObject>(configs);
            if (value.value(QStringLiteral("mode")).toString() != expectedMode_) {
                fail(QStringLiteral("CoreModeReadbackFailed"));
                return;
            }
            if (value.value(QStringLiteral("mixed-port")).toInt() != mixedPort_ ||
                value.value(QStringLiteral("allow-lan")).toBool(true) ||
                value.value(QStringLiteral("tun"))
                    .toObject()
                    .value(QStringLiteral("enable"))
                    .toBool() ||
                !process_.ownsListener(mixedPort_) || !process_.ownsListener(controllerPort_)) {
                fail(QStringLiteral("CoreListenersNotOwned"));
                return;
            }
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
    request("GET", "/proxies", {}, [this](domain::Result<QJsonObject> result) {
        if (const auto* value = std::get_if<QJsonObject>(&result)) {
            emit groupsChanged(value->value(QStringLiteral("proxies")).toObject());
        } else {
            emit failed(std::get<domain::Error>(result).code);
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
