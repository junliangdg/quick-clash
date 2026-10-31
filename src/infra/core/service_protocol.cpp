#include "infra/core/service_protocol.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>

namespace quickclash::infra {
namespace {
bool exactFields(const QJsonObject& object, const QStringList& keys) {
    if (object.size() != keys.size())
        return false;
    for (const auto& key : keys)
        if (!object.contains(key))
            return false;
    return true;
}
bool requestId(const QJsonValue& value) {
    if (!value.isString())
        return false;
    const auto id = value.toString();
    bool ok = false;
    const auto number = id.toULongLong(&ok);
    return ok && number > 0 && QString::number(number) == id;
}
} // namespace
bool ServiceProtocol::validEnvelope(const QJsonObject& message) {
    const auto action = message.value(QStringLiteral("action"));
    const auto session = message.value(QStringLiteral("sessionId"));
    return exactFields(message, {QStringLiteral("protocolVersion"), QStringLiteral("requestId"),
                                 QStringLiteral("sessionId"), QStringLiteral("action"),
                                 QStringLiteral("payload"), QStringLiteral("engineEpoch")}) &&
           message.value(QStringLiteral("protocolVersion")) == 1 &&
           requestId(message.value(QStringLiteral("requestId"))) && action.isString() &&
           !action.toString().isEmpty() && action.toString().size() <= 64 && session.isString() &&
           session.toString().size() <= 64 &&
           message.value(QStringLiteral("engineEpoch")).isString() &&
           message.value(QStringLiteral("engineEpoch")).toString().size() <= 64 &&
           message.value(QStringLiteral("payload")).isObject();
}
bool ServiceProtocol::validResponse(const QJsonObject& message) {
    const bool error = message.contains(QStringLiteral("error"));
    auto fields = QStringList{QStringLiteral("protocolVersion"), QStringLiteral("requestId"),
                              error ? QStringLiteral("error") : QStringLiteral("result")};
    if (error && message.contains(QStringLiteral("detail"))) {
        fields.append(QStringLiteral("detail"));
        const auto detail = message.value(QStringLiteral("detail")).toObject();
        if (!exactFields(
                detail, {QStringLiteral("code"), QStringLiteral("port"), QStringLiteral("role")}) ||
            detail.value(QStringLiteral("code")) != message.value(QStringLiteral("error")) ||
            !QStringList{QStringLiteral("CoreListenerAddressInUse"),
                         QStringLiteral("CoreListenerAccessDenied"),
                         QStringLiteral("CoreListenerPortUnavailable")}
                 .contains(message.value(QStringLiteral("error")).toString()) ||
            !QStringList{QStringLiteral("mixed"), QStringLiteral("controller")}.contains(
                detail.value(QStringLiteral("role")).toString()) ||
            detail.value(QStringLiteral("port")).toDouble() !=
                detail.value(QStringLiteral("port")).toInt() ||
            detail.value(QStringLiteral("port")).toInt() < 1024 ||
            detail.value(QStringLiteral("port")).toInt() > 65535)
            return false;
    }
    return exactFields(message, fields) && message.value(QStringLiteral("protocolVersion")) == 1 &&
           requestId(message.value(QStringLiteral("requestId"))) &&
           (error ? message.value(QStringLiteral("error")).isString() &&
                        !message.value(QStringLiteral("error")).toString().isEmpty() &&
                        message.value(QStringLiteral("error")).toString().size() <= 128
                  : message.value(QStringLiteral("result")).isObject());
}
ServiceProtocol::Decision ServiceProtocol::accept(const QJsonObject& message) {
    Decision decision;
    if (!validEnvelope(message)) {
        decision.error = QStringLiteral("ServiceProtocolMismatch");
        return decision;
    }
    decision.request = {message.value(QStringLiteral("requestId")).toString(),
                        message.value(QStringLiteral("action")).toString(),
                        message.value(QStringLiteral("sessionId")).toString(),
                        message.value(QStringLiteral("engineEpoch")).toString(),
                        message.value(QStringLiteral("payload")).toObject()};
    const auto& id = decision.request.id;
    const auto digest = QCryptographicHash::hash(
        QJsonDocument(message).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    if (fingerprints_.contains(id)) {
        if (fingerprints_.value(id) != digest) {
            decision.disposition = Disposition::Disconnect;
            return decision;
        }
        if (results_.contains(id)) {
            decision.disposition = Disposition::Cached;
            decision.response = results_.value(id);
        } else
            decision.disposition = Disposition::Pending;
        return decision;
    }
    const auto numeric = id.toULongLong();
    if (numeric <= lastId_ || pending_.size() >= 4) {
        decision.error = QStringLiteral("ServiceReplayRejected");
        return decision;
    }
    lastId_ = numeric;
    fingerprints_.insert(id, digest);
    pending_.insert(id);
    decision.disposition = Disposition::Execute;
    return decision;
}
void ServiceProtocol::complete(const QString& id, const QJsonObject& response) {
    // Protocol errors and replay rejects must not replace an existing operation result.
    if (!pending_.remove(id))
        return;
    results_.insert(id, response);
    order_.append(id);
    while (order_.size() > 32) {
        const auto expired = order_.takeFirst();
        fingerprints_.remove(expired);
        results_.remove(expired);
    }
}
bool ServiceProtocol::pending(const QString& id) const {
    return pending_.contains(id);
}
void ServiceProtocol::clear() {
    *this = ServiceProtocol{};
}
QString ServiceProtocol::executableBuild(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject ServiceProtocol::capabilities(const QString& serviceBuild) {
    return {{QStringLiteral("protocolVersion"), 1},
            {QStringLiteral("coreVersion"), QStringLiteral("v1.19.33")},
            {QStringLiteral("fixedCommands"), true},
            {QStringLiteral("networkConflictPreflight"), 1},
            {QStringLiteral("lifecycleDiagnostics"), 2},
            {QStringLiteral("portableResources"), 1},
            {QStringLiteral("serviceBuild"), serviceBuild},
            {QStringLiteral("proxyRecovery"), QStringLiteral("NotAvailable")},
            {QStringLiteral("reconnectPolicy"), QStringLiteral("StopOwnedSession")}};
}
} // namespace quickclash::infra
