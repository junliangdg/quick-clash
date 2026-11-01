#include "application/desktop_runtime.h"
#include "domain/subscription_name.h"
#include "domain/subscription_usage.h"
#include "infra/io/bounded_read.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <utility>

namespace quickclash::application {
namespace {
constexpr qint64 kSourceLimit = 16 * 1024 * 1024;
QString digest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString sourceKey(const QString& url, const QJsonObject& headers) {
    return digest(QJsonDocument(QJsonObject{{QStringLiteral("url"), url},
                                            {QStringLiteral("headers"), headers}})
                      .toJson(QJsonDocument::Compact));
}
bool sameOrigin(const QUrl& left, const QUrl& right) {
    return left.scheme() == right.scheme() && left.host() == right.host() &&
           left.port(443) == right.port(443);
}
bool validRoute(const QString& route) {
    return route == QStringLiteral("Direct") || route == QStringLiteral("RunningProxy") ||
           route == QStringLiteral("Auto");
}
QString validator(const QByteArray& value) {
    if (value.size() > 4096 || value.contains('\r') || value.contains('\n'))
        return {};
    return QString::fromLatin1(value);
}
} // namespace

void DesktopRuntime::requestSubscriptionUrl(const QString& id, const QString& token) {
    QString url;
    const auto reference = profile(id).value(QStringLiteral("secretRef")).toString();
    if (!reference.isEmpty() && !token.isEmpty() && token.size() <= 80) {
        const auto secret = credentials_->read(reference);
        if (const auto* bytes = std::get_if<QByteArray>(&secret)) {
            const auto envelope = QJsonDocument::fromJson(*bytes).object();
            url = envelope.isEmpty() ? QString::fromUtf8(*bytes)
                                     : envelope.value(QStringLiteral("url")).toString();
            if (url.size() > 8192)
                url.clear();
        }
    }
    emit subscriptionUrlReady(id, token, url);
}

void DesktopRuntime::setSubscriptionRoute(const QString& id, const QString& route) {
    if (busy_ || store_.readOnly() || !validRoute(route) ||
        profile(id).value(QStringLiteral("secretRef")).toString().isEmpty()) {
        error(QStringLiteral("InvalidSubscriptionRoute"));
        return;
    }
    auto catalog = profiles_;
    for (qsizetype index = 0; index < catalog.size(); ++index) {
        auto item = catalog[index].toObject();
        if (item.value(QStringLiteral("id")) == id) {
            item.insert(QStringLiteral("downloadRoute"), route);
            catalog[index] = item;
        }
    }
    if (saveCatalog(catalog)) {
        lastError_.clear();
        publish();
    }
}

void DesktopRuntime::download(const QString& url, const QString& name, const QString& id,
                              const QJsonObject& headers, const QString& route,
                              const QJsonObject& cache) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const auto normalizedUrl = url.trimmed();
    const QUrl source(normalizedUrl);
    if (!source.isValid() || source.scheme() != QStringLiteral("https") ||
        source.host().isEmpty() || !source.userInfo().isEmpty() || source.hasFragment() ||
        url.size() > 8192) {
        error(QStringLiteral("HttpsSubscriptionRequired"));
        return;
    }
    const auto selectedRoute =
        route.isEmpty()
            ? profile(id).value(QStringLiteral("downloadRoute")).toString(QStringLiteral("Direct"))
            : route;
    if (!validRoute(selectedRoute)) {
        error(QStringLiteral("InvalidSubscriptionRoute"));
        return;
    }
    if (selectedRoute == QStringLiteral("RunningProxy") && !engine_.ready()) {
        error(QStringLiteral("SubscriptionProxyUnavailable"));
        return;
    }
    if (headers.size() > 16) {
        error(QStringLiteral("InvalidSubscriptionHeaders"));
        return;
    }
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const auto key = it.key().toLower();
        if ((key != QStringLiteral("authorization") && key != QStringLiteral("user-agent") &&
             key != QStringLiteral("accept")) ||
            !it.value().isString() || it.value().toString().size() > 4096 ||
            it.value().toString().contains(QLatin1Char('\r')) ||
            it.value().toString().contains(QLatin1Char('\n'))) {
            error(QStringLiteral("InvalidSubscriptionHeaders"));
            return;
        }
    }
    auto context = std::make_shared<SubscriptionDownload>();
    context->generation = ++operationGeneration_;
    context->url = normalizedUrl;
    context->name = name;
    context->id = id;
    context->route = selectedRoute;
    context->headers = headers;
    context->proxy = selectedRoute == QStringLiteral("RunningProxy");
    context->proxyEpoch = epoch_;
    const auto previous = profile(id);
    if (cache.value(QStringLiteral("sourceKey")) == sourceKey(normalizedUrl, headers) &&
        cache.value(QStringLiteral("sourceHash")) == previous.value(QStringLiteral("sourceHash")) &&
        !previous.value(QStringLiteral("sourceHash")).toString().isEmpty())
        context->cache = cache;
    context->timer.start();
    busy_ = true;
    lastError_.clear();
    featureState_.insert(
        QStringLiteral("subscriptionDownload"),
        QJsonObject{{QStringLiteral("policy"), selectedRoute},
                    {QStringLiteral("path"), context->proxy ? "RunningProxy" : "Direct"},
                    {QStringLiteral("state"), "Downloading"}});
    publish();
    requestSubscription(context, source);
}

void DesktopRuntime::requestSubscription(const std::shared_ptr<SubscriptionDownload>& context,
                                         const QUrl& url) {
    if (context->generation != operationGeneration_)
        return;
    const auto remaining = 20000 - context->timer.elapsed();
    if (remaining <= 0 || (context->proxy && (!engine_.ready() || context->proxyEpoch != epoch_))) {
        failSubscription(remaining <= 0 ? QStringLiteral("SubscriptionDownloadFailed")
                                        : QStringLiteral("SubscriptionProxyUnavailable"));
        return;
    }
    context->bytes.clear();
    context->visited.insert(url.adjusted(QUrl::NormalizePathSegments));
    subscriptionNetwork_.setProxy(
        context->proxy
            ? QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                            static_cast<quint16>(
                                effectivePreferences_.value(QStringLiteral("mixedPort")).toInt()))
            : QNetworkProxy(QNetworkProxy::NoProxy));
    subscriptionNetwork_.clearAccessCache();
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "clash.meta/1.19.33 QuickClash/0.1.0");
    request.setRawHeader("Accept", "application/yaml, text/yaml, text/plain;q=0.9, */*;q=0.1");
    if (context->authorization)
        for (auto it = context->headers.begin(); it != context->headers.end(); ++it)
            request.setRawHeader(it.key().toLatin1(), it.value().toString().toUtf8());
    if (!context->cacheRetry &&
        context->cache.value(QStringLiteral("finalUrl")) == url.toString()) {
        const auto etag =
            validator(context->cache.value(QStringLiteral("etag")).toString().toLatin1());
        const auto modified =
            validator(context->cache.value(QStringLiteral("modified")).toString().toLatin1());
        if (!etag.isEmpty())
            request.setRawHeader("If-None-Match", etag.toLatin1());
        if (!modified.isEmpty())
            request.setRawHeader("If-Modified-Since", modified.toLatin1());
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    auto* reply = subscriptionNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(infra::kReadChunkSize);
    const auto consume = [context, reply] {
        return infra::consumeAvailable(*reply, context->received, kSourceLimit,
                                       [context](QByteArrayView bytes) {
                                           context->bytes.append(bytes.data(), bytes.size());
                                           return true;
                                       }) == infra::ReadStatus::Complete;
    };
    connect(reply, &QIODevice::readyRead, this, [reply, consume] {
        if (!consume())
            reply->abort();
    });
    auto* deadline = new QTimer(reply);
    const auto transferTimedOut = std::make_shared<bool>(false);
    deadline->setSingleShot(true);
    deadline->setTimerType(Qt::PreciseTimer);
    deadline->start(static_cast<int>(qMin<qint64>(15000, remaining)));
    connect(deadline, &QTimer::timeout, reply, [reply, transferTimedOut] {
        *transferTimedOut = true;
        reply->abort();
    });
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, context, url, consume, transferTimedOut] {
            reply->deleteLater();
            if (context->generation != operationGeneration_ || downloadReply_ != reply)
                return;
            downloadReply_.clear();
            const bool readOk = consume();
            const auto fail = [this](const QString& code) { failSubscription(code); };
            if (context->received > kSourceLimit) {
                fail(QStringLiteral("SubscriptionTooLarge"));
                return;
            }
            if (context->timer.elapsed() >= 20000) {
                fail(QStringLiteral("SubscriptionDownloadFailed"));
                return;
            }
            if (context->proxy && (!engine_.ready() || context->proxyEpoch != epoch_)) {
                fail(QStringLiteral("SubscriptionProxyUnavailable"));
                return;
            }
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                const auto next = url.resolved(
                    reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
                if (!readOk || reply->error() != QNetworkReply::NoError || !next.isValid() ||
                    next.scheme() != QStringLiteral("https") || next.host().isEmpty() ||
                    !next.userInfo().isEmpty() || next.hasFragment() ||
                    next.toString().size() > 8192 || context->redirects >= 5 ||
                    context->visited.contains(next.adjusted(QUrl::NormalizePathSegments))) {
                    fail(QStringLiteral("SubscriptionRedirectBlocked"));
                    return;
                }
                ++context->redirects;
                if (!sameOrigin(url, next))
                    context->authorization = false;
                requestSubscription(context, next);
                return;
            }
            if (status == 304 && readOk && reply->error() == QNetworkReply::NoError) {
                const auto previous = profile(context->id);
                const auto reference = previous.value(QStringLiteral("sourceRef")).toString();
                const auto expected = context->cache.value(QStringLiteral("sourceHash")).toString();
                const auto cached = store_.readBytes(reference);
                const bool valid =
                    !context->cacheRetry && !expected.isEmpty() &&
                    context->cache.value(QStringLiteral("finalUrl")) == url.toString() &&
                    expected == previous.value(QStringLiteral("sourceHash")) &&
                    std::holds_alternative<QByteArray>(cached) &&
                    digest(std::get<QByteArray>(cached)) == expected;
                if (!valid) {
                    if (context->cacheRetry) {
                        fail(QStringLiteral("SubscriptionCacheInvalid"));
                        return;
                    }
                    context->cacheRetry = true;
                    context->cache = {};
                    requestSubscription(context, url);
                    return;
                }
                context->bytes = std::get<QByteArray>(cached);
                finishSubscription(context, reply, status);
                return;
            }
            if (!readOk || reply->error() != QNetworkReply::NoError || status != 200) {
                const auto networkError = reply->error();
                const bool retryable = *transferTimedOut ||
                                       networkError == QNetworkReply::ConnectionRefusedError ||
                                       networkError == QNetworkReply::RemoteHostClosedError ||
                                       networkError == QNetworkReply::HostNotFoundError ||
                                       networkError == QNetworkReply::TimeoutError ||
                                       networkError == QNetworkReply::NetworkSessionFailedError ||
                                       networkError == QNetworkReply::TemporaryNetworkFailureError;
                if (context->route == QStringLiteral("Auto") && !context->fallback && status == 0 &&
                    retryable && engine_.ready()) {
                    context->fallback = true;
                    context->proxy = true;
                    context->proxyEpoch = epoch_;
                    context->visited.clear();
                    context->authorization = true;
                    auto state =
                        featureState_.value(QStringLiteral("subscriptionDownload")).toObject();
                    state.insert(QStringLiteral("path"), QStringLiteral("RunningProxy"));
                    state.insert(QStringLiteral("fallback"), true);
                    featureState_.insert(QStringLiteral("subscriptionDownload"), state);
                    requestSubscription(context, QUrl(context->url));
                    return;
                }
                fail(status == 401 || status == 403
                         ? QStringLiteral("SubscriptionAuthenticationFailed")
                         : QStringLiteral("SubscriptionDownloadFailed"));
                return;
            }
            finishSubscription(context, reply, status);
        });
}

void DesktopRuntime::failSubscription(const QString& code) {
    busy_ = false;
    auto state = featureState_.value(QStringLiteral("subscriptionDownload")).toObject();
    state.insert(QStringLiteral("state"), QStringLiteral("Failed"));
    featureState_.insert(QStringLiteral("subscriptionDownload"), state);
    error(code);
}

void DesktopRuntime::finishSubscription(const std::shared_ptr<SubscriptionDownload>& context,
                                        QNetworkReply* reply, int status) {
    busy_ = false;
    const auto previous = profile(context->id);
    const auto bodyHash = digest(context->bytes);
    const bool unchanged =
        !previous.isEmpty() && !previous.contains(QStringLiteral("snapshotRef")) &&
        previous.value(QStringLiteral("sourceHash")) == bodyHash &&
        std::holds_alternative<std::monostate>(
            store_.verifyBytes(previous.value(QStringLiteral("sourceRef")).toString(), bodyHash));
    if (!unchanged) {
        const auto payloadError = infra::subscriptionPayloadError(context->bytes);
        if (!payloadError.isEmpty()) {
            failSubscription(payloadError);
            return;
        }
    }
    auto cache = status == 304 ? context->cache : QJsonObject{};
    cache.insert(QStringLiteral("sourceKey"), sourceKey(context->url, context->headers));
    cache.insert(QStringLiteral("sourceHash"), bodyHash);
    cache.insert(QStringLiteral("finalUrl"), reply->url().toString());
    for (const auto& header :
         {qMakePair(QByteArray("ETag"), QStringLiteral("etag")),
          qMakePair(QByteArray("Last-Modified"), QStringLiteral("modified"))}) {
        if (status == 200 || reply->hasRawHeader(header.first))
            cache.insert(header.second, validator(reply->rawHeader(header.first)));
    }
    const auto secretRef = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto secured = credentials_->write(
        secretRef, QJsonDocument(QJsonObject{{QStringLiteral("url"), context->url},
                                             {QStringLiteral("headers"), context->headers},
                                             {QStringLiteral("cache"), cache}})
                       .toJson(QJsonDocument::Compact));
    if (const auto* failure = std::get_if<domain::Error>(&secured)) {
        failSubscription(failure->code);
        return;
    }
    QJsonObject metadata{
        {QStringLiteral("downloadRoute"), context->route},
        {QStringLiteral("lastDownloadPath"), context->proxy ? "RunningProxy" : "Direct"}};
    if (status != 304 || reply->hasRawHeader("subscription-userinfo")) {
        metadata.insert(QStringLiteral("usage"),
                        domain::parseSubscriptionUsage(reply->rawHeader("subscription-userinfo")));
        metadata.insert(QStringLiteral("usageCollectedAt"), clock_().toString(Qt::ISODate));
    }
    auto state = featureState_.value(QStringLiteral("subscriptionDownload")).toObject();
    state.insert(QStringLiteral("state"), QStringLiteral("Downloaded"));
    featureState_.insert(QStringLiteral("subscriptionDownload"), state);
    if (unchanged) {
        context->bytes = {};
        auto catalog = profiles_;
        for (qsizetype index = 0; index < catalog.size(); ++index) {
            auto item = catalog[index].toObject();
            if (item.value(QStringLiteral("id")) != context->id)
                continue;
            item.insert(QStringLiteral("secretRef"), secretRef);
            item.insert(QStringLiteral("updatedAt"), clock_().toString(Qt::ISODate));
            for (auto it = metadata.begin(); it != metadata.end(); ++it)
                item.insert(it.key(), it.value());
            catalog[index] = item;
        }
        if (saveCatalog(catalog)) {
            collectGarbage();
            if (context->id == selectedId_ &&
                previous.value(QStringLiteral("pendingApply")).toBool() &&
                previous.value(QStringLiteral("autoApply")).toBool()) {
                applyProfile(context->id, engine_.ready());
                return;
            }
            lastError_.clear();
            publish();
        }
        return;
    }
    auto name = context->name.trimmed();
    if (context->id.isEmpty() && name.isEmpty())
        name = domain::subscriptionName(reply->rawHeader("profile-title"),
                                        reply->rawHeader("Content-Disposition"));
    resolveImport(std::exchange(context->bytes, {}), name, secretRef, context->id, metadata);
}
} // namespace quickclash::application
