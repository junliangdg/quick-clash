#include "application/desktop_runtime.h"
#include "infra/io/bounded_read.h"
#include <QCryptographicHash>
#include <QDir>
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
    const auto normalized = infra::coreSubscriptionConfig(bytes);
    if (normalized != bytes) {
        const auto profileId =
            id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
        const auto reference =
            QStringLiteral("data/profiles/%1/%2.yaml")
                .arg(profileId, QUuid::createUuid().toString(QUuid::WithoutBraces));
        auto candidates = std::make_shared<ConfigCandidates>(store_);
        const auto saved = store_.writeBytes(reference, bytes, true);
        if (const auto* failure = std::get_if<domain::Error>(&saved)) {
            error(failure->code);
            return;
        }
        candidates->references.append(reference);
        auto details = metadata;
        details.insert("providerMode", "runtime");
        importBytes(normalized, name, secretRef, profileId,
                    {{"sourceRef", reference},
                     {"sourceHash",
                      QString::fromLatin1(
                          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}},
                    details, std::move(candidates));
        return;
    }
    const auto result = infra::providerRequests(bytes);
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
        return;
    }
    const auto requests = std::get<QJsonArray>(result);
    if (requests.isEmpty()) {
        auto details = metadata;
        details.insert(QStringLiteral("providerMode"), QStringLiteral("snapshot"));
        details.insert(QStringLiteral("providerResources"), QJsonArray{});
        details.insert(QStringLiteral("managedProviders"), QJsonArray{});
        importBytes(std::move(bytes), name, secretRef, id, {}, details);
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
    providerImport_->metadata.insert(
        QStringLiteral("providerMode"),
        metadata.value(QStringLiteral("providerMode")) == QStringLiteral("runtime") ||
                (!metadata.contains(QStringLiteral("providerMode")) &&
                 profile(id).value(QStringLiteral("providerMode")) == QStringLiteral("runtime"))
            ? QStringLiteral("runtime")
            : QStringLiteral("snapshot"));
    if (infra::requiresRuntimeProviders(bytes))
        providerImport_->metadata.insert(QStringLiteral("providerMode"), QStringLiteral("runtime"));
    bytes = {};
    providerImport_->refreshKey = metadata.value(QStringLiteral("_providerRefreshKey")).toString();
    providerImport_->cachedResources =
        profile(id).value(QStringLiteral("providerResources")).toArray();
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
        if (infra::providerPayloadsRequireRuntime(context->stage.devices()))
            context->metadata.insert(QStringLiteral("providerMode"), QStringLiteral("runtime"));
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
        auto details = context->metadata;
        const bool dynamic =
            details.value(QStringLiteral("providerMode")) == QStringLiteral("runtime");
        QByteArray candidate;
        if (dynamic) {
            // Native options can rename/filter nodes or refer to RULE-SET from DNS,
            // sniffer and sub-rules. Validate this exact graph with the official core;
            // a flattened intermediate can have different or missing references.
            const auto original = store_.readBytes(context->sourceRef);
            if (!std::holds_alternative<QByteArray>(original)) {
                fail(QStringLiteral("DataReadFailed"));
                return;
            }
            auto runtime = infra::runtimeProviderConfig(std::get<QByteArray>(original),
                                                        context->stage.devices());
            if (const auto* failure = std::get_if<domain::Error>(&runtime)) {
                fail(failure->code);
                return;
            }
            candidate = std::move(std::get<QByteArray>(runtime));
        } else {
            const auto expanded =
                infra::expandProviderFiles(source, context->stage.devices(), *output);
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
            candidate = std::move(loaded.bytes);
        }
        source.close();
        output.reset();
        const auto devices = context->stage.devices();
        QJsonArray summaries;
        for (const auto& value : context->requests) {
            const auto entry = value.toObject();
            auto* device = devices.value(entry.value("key").toString());
            if (!device || !device->seek(0)) {
                fail(QStringLiteral("DataReadFailed"));
                return;
            }
            auto* file = qobject_cast<QFileDevice*>(device);
            auto payload =
                file ? infra::readFileBounded(*file, 4 * 1024 * 1024) : infra::FileReadResult{};
            if (!payload.ok) {
                fail(QStringLiteral("DataReadFailed"));
                return;
            }
            const auto checksum = QString::fromLatin1(
                QCryptographicHash::hash(payload.bytes, QCryptographicHash::Sha256).toHex());
            const auto urlHash =
                QString::fromLatin1(QCryptographicHash::hash(entry.value("url").toString().toUtf8(),
                                                             QCryptographicHash::Sha256)
                                        .toHex());
            QString reference;
            for (const auto& cached : context->cachedResources) {
                const auto old = cached.toObject();
                if (old.value("key") == entry.value("key") && old.value("sha256") == checksum &&
                    old.value("urlHash") == urlHash &&
                    std::holds_alternative<std::monostate>(
                        store_.verifyBytes(old.value("sourceRef").toString(), checksum)))
                    reference = old.value("sourceRef").toString();
            }
            if (reference.isEmpty()) {
                reference =
                    QStringLiteral("data/profiles/%1/provider_%2.yaml")
                        .arg(context->id, QUuid::createUuid().toString(QUuid::WithoutBraces));
                const auto saved = store_.writeBytes(reference, payload.bytes, true);
                if (const auto* failure = std::get_if<domain::Error>(&saved)) {
                    fail(failure->code);
                    return;
                }
                context->candidates->references.append(reference);
            }
            QJsonObject resource{
                {"key", entry.value("key")},
                {"name", entry.value("name")},
                {"kind", entry.value("kind")},
                {"sourceRef", reference},
                {"sha256", checksum},
                {"bytes", QString::number(payload.bytes.size())},
                {"urlHash", urlHash},
                {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
            // Reused resources retain their verified content's original update time.
            if (!context->refreshKey.isEmpty() && entry.value("key") != context->refreshKey)
                for (const auto& cached : context->cachedResources)
                    if (cached.toObject().value("key") == entry.value("key"))
                        resource.insert("updatedAt", cached.toObject().value("updatedAt"));
            context->resources.append(resource);
            summaries.append(QJsonObject{
                {QStringLiteral("name"), entry.value(QStringLiteral("name"))},
                {QStringLiteral("kind"), entry.value(QStringLiteral("kind"))},
                {QStringLiteral("mode"),
                 dynamic ? QStringLiteral("runtime") : QStringLiteral("snapshot")},
                {QStringLiteral("type"),
                 entry.value("type") == "file" ? QStringLiteral("File") : QStringLiteral("HTTP")},
                {QStringLiteral("bytes"), resource.value("bytes")},
                {QStringLiteral("updatedAt"), resource.value("updatedAt")}});
        }
        context->stage.cancel();
        details.insert(QStringLiteral("managedProviders"), summaries);
        details.insert(QStringLiteral("providerResources"), context->resources);
        providerImport_.reset();
        busy_ = false;
        importBytes(std::move(candidate), context->name, context->secretRef, context->id,
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
    if (entry.value("type") == "file") {
        auto path = entry.value("url").toString();
        if (QDir::isRelativePath(path)) {
            const auto base = context->metadata.value("_sourceDirectory").toString();
            if (base.isEmpty()) {
                fail(QStringLiteral("ProviderCacheUnavailable"));
                return;
            }
            path = QDir(base).absoluteFilePath(path);
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) ||
            std::holds_alternative<domain::Error>(context->stage.consume(file)) ||
            std::holds_alternative<domain::Error>(context->stage.finish())) {
            fail(QStringLiteral("ProviderCacheUnavailable"));
            return;
        }
        ++context->index;
        QTimer::singleShot(0, this, [this, weak = std::weak_ptr<ProviderImport>(context)] {
            if (weak.lock() == providerImport_)
                fetchProvider();
        });
        return;
    }
    if (!context->refreshKey.isEmpty() && entry.value("key") != context->refreshKey) {
        QJsonObject cached;
        for (const auto& value : context->cachedResources)
            if (value.toObject().value("key") == entry.value("key"))
                cached = value.toObject();
        const auto expectedUrlHash =
            QString::fromLatin1(QCryptographicHash::hash(entry.value("url").toString().toUtf8(),
                                                         QCryptographicHash::Sha256)
                                    .toHex());
        const auto path = store_.path(cached.value("sourceRef").toString());
        if (cached.isEmpty() || cached.value("urlHash") != expectedUrlHash ||
            std::holds_alternative<domain::Error>(store_.verifyBytes(
                cached.value("sourceRef").toString(), cached.value("sha256").toString())) ||
            !std::holds_alternative<QString>(path)) {
            fail(QStringLiteral("ProviderCacheUnavailable"));
            return;
        }
        QFile file(std::get<QString>(path));
        if (!file.open(QIODevice::ReadOnly) ||
            std::holds_alternative<domain::Error>(context->stage.consume(file)) ||
            std::holds_alternative<domain::Error>(context->stage.finish())) {
            fail(QStringLiteral("ProviderCacheUnavailable"));
            return;
        }
        ++context->index;
        QTimer::singleShot(0, this, [this, weak = std::weak_ptr<ProviderImport>(context)] {
            if (weak.lock() == providerImport_)
                fetchProvider();
        });
        return;
    }
    context->downloadTimer.start();
    context->redirects.clear();
    downloadProvider(context, entry, QUrl(entry.value("url").toString()));
}
void DesktopRuntime::downloadProvider(const std::shared_ptr<ProviderImport>& context,
                                      const QJsonObject& entry, const QUrl& url,
                                      bool headersAllowed) {
    context->redirects.insert(url.adjusted(QUrl::NormalizePathSegments));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10000);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setRawHeader("User-Agent", "clash.meta/1.19.33 QuickClash/" QUICKCLASH_VERSION);
    const auto headers = entry.value(QStringLiteral("headers")).toObject();
    if (headersAllowed)
        for (auto header = headers.begin(); header != headers.end(); ++header)
            request.setRawHeader(header.key().toLatin1(), header.value().toString().toUtf8());
    auto* reply = directNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(infra::kReadChunkSize);
    const auto weak = std::weak_ptr<ProviderImport>(context);
    connect(reply, &QIODevice::readyRead, this, [this, reply, weak] {
        const auto context = weak.lock();
        if (!context || context != providerImport_ || context->generation != operationGeneration_)
            return;
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        if (std::holds_alternative<domain::Error>(context->stage.consume(*reply)))
            reply->abort();
    });
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->start(static_cast<int>(qMax<qint64>(1, 15000 - context->downloadTimer.elapsed())));
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(
        reply, &QNetworkReply::finished, this, [this, reply, weak, entry, url, headersAllowed]() {
            reply->deleteLater();
            const auto context = weak.lock();
            if (!context || context != providerImport_ ||
                context->generation != operationGeneration_)
                return;
            downloadReply_.clear();
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                const auto next = url.resolved(
                    reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
                if (reply->error() == QNetworkReply::NoError && next.isValid() &&
                    (next.scheme() == "https" || next.scheme() == "http") &&
                    (url.scheme() != "https" || next.scheme() == "https") &&
                    !next.host().isEmpty() && next.userInfo().isEmpty() && !next.hasFragment() &&
                    next.toString().size() <= 8192 && context->redirects.size() <= 5 &&
                    !context->redirects.contains(next.adjusted(QUrl::NormalizePathSegments)) &&
                    context->downloadTimer.elapsed() < 15000) {
                    const bool sameOrigin = url.scheme() == next.scheme() &&
                                            url.host() == next.host() &&
                                            url.port(url.scheme() == "https" ? 443 : 80) ==
                                                next.port(next.scheme() == "https" ? 443 : 80);
                    downloadProvider(context, entry, next, headersAllowed && sameOrigin);
                    return;
                }
            }
            const auto consumed = context->stage.consume(*reply);
            if (std::holds_alternative<domain::Error>(consumed) ||
                reply->error() != QNetworkReply::NoError || status != 200 ||
                context->downloadTimer.elapsed() >= 15000 ||
                std::holds_alternative<domain::Error>(context->stage.finish())) {
                context->stage.cancel();
                providerImport_.reset();
                busy_ = false;
                error(QStringLiteral("ProviderDownloadFailed"));
                return;
            }
            ++context->index;
            // Let reply callbacks unwind before the next download stage.
            QTimer::singleShot(0, this, [this, weak] {
                if (weak.lock() == providerImport_)
                    fetchProvider();
            });
        });
}
void DesktopRuntime::refreshSnapshotProvider(const QString& kind, const QString& name) {
    const auto current = profile(selectedId_);
    if (current.value(QStringLiteral("providerMode")) == QStringLiteral("runtime")) {
        error(QStringLiteral("ProviderModeMismatch"));
        return;
    }
    const auto bytes = store_.readBytes(current.value("sourceRef").toString());
    if (!std::holds_alternative<QByteArray>(bytes) ||
        std::holds_alternative<domain::Error>(store_.verifyBytes(
            current.value("sourceRef").toString(), current.value("sourceHash").toString()))) {
        error(QStringLiteral("SourceIntegrityFailed"));
        return;
    }
    const auto requests = infra::providerRequests(std::get<QByteArray>(bytes));
    if (!std::holds_alternative<QJsonArray>(requests)) {
        error(QStringLiteral("InvalidProviderConfig"));
        return;
    }
    const auto key = kind + ':' + name;
    bool present = false;
    for (const auto& entry : std::get<QJsonArray>(requests))
        present |= entry.toObject().value("key") == key;
    if (!present) {
        error(QStringLiteral("ProviderNotFound"));
        return;
    }
    resolveImport(std::get<QByteArray>(bytes), current.value("name").toString(),
                  current.value("secretRef").toString(), selectedId_,
                  {{QStringLiteral("_providerRefreshKey"), key}});
}
} // namespace quickclash::application
