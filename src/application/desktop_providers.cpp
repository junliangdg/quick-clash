#include "application/desktop_runtime.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>

namespace quickclash::application {
void DesktopRuntime::resolveImport(const QByteArray& bytes, const QString& name,
                                   const QString& secretRef, const QString& id,
                                   const QJsonObject& metadata) {
    const auto result = infra::providerRequests(bytes);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
        return;
    }
    const auto requests = std::get<QJsonArray>(result);
    if (requests.isEmpty()) {
        importBytes(bytes, name, secretRef, id, {}, metadata);
        return;
    }
    busy_ = true;
    publish();
    fetchProvider(bytes, requests, 0, {}, name, secretRef, id, metadata, ++operationGeneration_);
}
void DesktopRuntime::fetchProvider(const QByteArray& source, const QJsonArray& requests,
                                   qsizetype index, QHash<QString, QByteArray> payloads,
                                   const QString& name, const QString& secretRef, const QString& id,
                                   const QJsonObject& metadata, quint64 generation) {
    if (generation != operationGeneration_)
        return;
    if (index == requests.size()) {
        busy_ = false;
        const auto expanded = infra::expandProviders(source, payloads);
        if (const auto* failure = std::get_if<domain::Error>(&expanded)) {
            error(failure->code);
            return;
        }
        auto details = metadata;
        QJsonArray summaries;
        for (const auto& value : requests) {
            const auto entry = value.toObject();
            summaries.append(
                QJsonObject{{QStringLiteral("name"), entry.value(QStringLiteral("name"))},
                            {QStringLiteral("kind"), entry.value(QStringLiteral("kind"))},
                            {QStringLiteral("updatedAt"),
                             QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
        }
        details.insert(QStringLiteral("managedProviders"), summaries);
        importBytes(std::get<QByteArray>(expanded), name, secretRef, id, source, details);
        return;
    }
    QNetworkRequest request(
        QUrl(requests[index].toObject().value(QStringLiteral("url")).toString()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10000);
    request.setRawHeader("User-Agent", "clash.meta/1.19.33 QuickClash/0.1.0");
    auto* reply = directNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(4 * 1024 * 1024 + 1);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes] {
        bytes->append(
            reply->read(std::min(reply->bytesAvailable(), 4 * 1024 * 1024 + 1 - bytes->size())));
        if (bytes->size() > 4 * 1024 * 1024)
            reply->abort();
    });
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->start(15000);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, source, requests, index, payloads = std::move(payloads), name,
             secretRef, id, metadata, generation]() mutable {
                reply->deleteLater();
                if (generation != operationGeneration_)
                    return;
                bytes->append(reply->read(
                    std::min(reply->bytesAvailable(), 4 * 1024 * 1024 + 1 - bytes->size())));
                qsizetype total = bytes->size();
                for (const auto& payload : payloads)
                    total += payload.size();
                if (reply->error() != QNetworkReply::NoError ||
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
                    total > 16 * 1024 * 1024) {
                    busy_ = false;
                    error(QStringLiteral("ProviderDownloadFailed"));
                    return;
                }
                payloads.insert(requests[index].toObject().value(QStringLiteral("key")).toString(),
                                *bytes);
                fetchProvider(source, requests, index + 1, std::move(payloads), name, secretRef, id,
                              metadata, generation);
            });
}
} // namespace quickclash::application
