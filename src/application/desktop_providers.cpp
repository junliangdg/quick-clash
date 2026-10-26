#include "application/desktop_runtime.h"
#include "infra/io/bounded_read.h"
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>

namespace quickclash::application {
void DesktopRuntime::resolveImport(QByteArray bytes, const QString& name, const QString& secretRef,
                                   const QString& id, const QJsonObject& metadata) {
    if (profiles_.size() >= 100 && id.isEmpty()) {
        error(QStringLiteral("ProfileLimitExceeded"));
        return;
    }
    const auto result = infra::providerRequests(bytes);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
        return;
    }
    const auto requests = std::get<QJsonArray>(result);
    if (requests.isEmpty()) {
        importBytes(std::move(bytes), name, secretRef, id, {}, metadata);
        return;
    }
    busy_ = true;
    publish();
    providerImport_ = std::make_shared<ProviderImport>(store_);
    providerImport_->id = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    providerImport_->sourceRef =
        QStringLiteral("data/profiles/%1/%2.yaml")
            .arg(providerImport_->id, QUuid::createUuid().toString(QUuid::WithoutBraces));
    providerImport_->sourceHash =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    providerImport_->candidates = std::make_shared<ConfigCandidates>(store_);
    const auto saved = store_.writeBytes(providerImport_->sourceRef, bytes, true);
    bytes = {};
    if (const auto* failure = std::get_if<domain::Error>(&saved)) {
        providerImport_.reset();
        busy_ = false;
        error(failure->code);
        return;
    }
    providerImport_->candidates->references.append(providerImport_->sourceRef);
    providerImport_->requests = requests;
    providerImport_->name = name;
    providerImport_->secretRef = secretRef;
    providerImport_->metadata = metadata;
    providerImport_->generation = ++operationGeneration_;
    fetchProvider();
}
void DesktopRuntime::fetchProvider() {
    auto context = providerImport_;
    if (!context || context->generation != operationGeneration_)
        return;
    const auto fail = [&](const QString& code) {
        context->stage.cancel();
        providerImport_.reset();
        busy_ = false;
        error(code);
    };
    if (context->index == context->requests.size()) {
        auto temporary = store_.beginConfigTemporary();
        if (const auto* failure = std::get_if<domain::Error>(&temporary)) {
            fail(failure->code);
            return;
        }
        auto output = std::get<std::unique_ptr<QTemporaryFile>>(std::move(temporary));
        const auto checked = store_.verifyBytes(context->sourceRef, context->sourceHash);
        const auto sourcePath = store_.path(context->sourceRef);
        if (std::holds_alternative<domain::Error>(checked) ||
            !std::holds_alternative<QString>(sourcePath)) {
            fail(QStringLiteral("SourceIntegrityFailed"));
            return;
        }
        QFile source(std::get<QString>(sourcePath));
        if (!source.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("DataReadFailed"));
            return;
        }
        const auto expanded = infra::expandProviderFiles(source, context->stage.devices(), *output);
        context->stage.cancel();
        if (const auto* failure = std::get_if<domain::Error>(&expanded)) {
            fail(failure->code);
            return;
        }
        if (!output->flush() || !output->seek(0)) {
            fail(QStringLiteral("DataReadFailed"));
            return;
        }
        auto loaded = infra::readFileBounded(*output, 16 * 1024 * 1024);
        if (!loaded.ok) {
            fail(QStringLiteral("DataReadFailed"));
            return;
        }
        source.close();
        output.reset();
        auto details = context->metadata;
        QJsonArray summaries;
        for (const auto& value : context->requests) {
            const auto entry = value.toObject();
            summaries.append(
                QJsonObject{{QStringLiteral("name"), entry.value(QStringLiteral("name"))},
                            {QStringLiteral("kind"), entry.value(QStringLiteral("kind"))},
                            {QStringLiteral("updatedAt"),
                             QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
        }
        details.insert(QStringLiteral("managedProviders"), summaries);
        providerImport_.reset();
        busy_ = false;
        importBytes(std::move(loaded.bytes), context->name, context->secretRef, context->id,
                    {{QStringLiteral("sourceRef"), context->sourceRef},
                     {QStringLiteral("sourceHash"), context->sourceHash}},
                    details, std::move(context->candidates));
        return;
    }
    const auto entry = context->requests[context->index].toObject();
    const auto begun = context->stage.begin(entry.value(QStringLiteral("key")).toString());
    if (const auto* failure = std::get_if<domain::Error>(&begun)) {
        fail(failure->code);
        return;
    }
    QNetworkRequest request(QUrl(entry.value(QStringLiteral("url")).toString()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10000);
    request.setRawHeader("User-Agent", "clash.meta/1.19.33 QuickClash/0.1.0");
    auto* reply = directNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(infra::kReadChunkSize);
    const auto weak = std::weak_ptr<ProviderImport>(context);
    connect(reply, &QIODevice::readyRead, this, [this, reply, weak] {
        const auto context = weak.lock();
        if (!context || context != providerImport_ || context->generation != operationGeneration_)
            return;
        if (std::holds_alternative<domain::Error>(context->stage.consume(*reply)))
            reply->abort();
    });
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->start(15000);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, weak]() {
        reply->deleteLater();
        const auto context = weak.lock();
        if (!context || context != providerImport_ || context->generation != operationGeneration_)
            return;
        downloadReply_.clear();
        const auto consumed = context->stage.consume(*reply);
        if (std::holds_alternative<domain::Error>(consumed) ||
            reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
            std::holds_alternative<domain::Error>(context->stage.finish())) {
            context->stage.cancel();
            providerImport_.reset();
            busy_ = false;
            error(QStringLiteral("ProviderDownloadFailed"));
            return;
        }
        ++context->index;
        // Let reply callbacks unwind before the next download/expansion stage.
        QTimer::singleShot(0, this, [this, weak] {
            if (weak.lock() == providerImport_)
                fetchProvider();
        });
    });
}
} // namespace quickclash::application
