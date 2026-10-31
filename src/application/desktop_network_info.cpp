#include "application/desktop_runtime.h"
#include "infra/io/bounded_read.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <utility>

namespace quickclash::application {
void DesktopRuntime::cancelIpQueries(bool stale) {
    ++ipGeneration_;
    const auto replies = std::exchange(ipReplies_, {});
    for (const auto& reply : replies)
        if (reply) {
            disconnect(reply, nullptr, this, nullptr);
            reply->abort();
            reply->deleteLater();
        }
    auto info = featureState_.value(QStringLiteral("ipInfo")).toObject();
    for (const auto* family : {"v4", "v6"}) {
        auto pair = info.value(QLatin1String(family)).toObject();
        for (const auto* side : {"local", "proxy"}) {
            auto value = pair.value(QLatin1String(side)).toObject();
            if (value.isEmpty())
                continue;
            if (stale)
                value.insert(QStringLiteral("stale"), true);
            if (value.value(QStringLiteral("state")) == QStringLiteral("Running"))
                value.insert(QStringLiteral("state"), QStringLiteral("Cancelled"));
            pair.insert(QLatin1String(side), value);
        }
        if (!pair.isEmpty())
            info.insert(QLatin1String(family), pair);
    }
    if (!info.isEmpty())
        featureState_.insert(QStringLiteral("ipInfo"), info);
}
void DesktopRuntime::queryIp(const QString& family, const QUrl& source) {
    if (busy_ || (family != QStringLiteral("v4") && family != QStringLiteral("v6")))
        return;
    const QUrl url = source.isEmpty() ? QUrl(family == QStringLiteral("v6")
                                                 ? QStringLiteral("https://api-ipv6.ip.sb/geoip")
                                                 : QStringLiteral("https://api-ipv4.ip.sb/geoip"))
                                      : source;
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasFragment())
        return;
    cancelIpQueries(false);
    const auto generation = ipGeneration_;
    auto info = featureState_.value(QStringLiteral("ipInfo")).toObject();
    QJsonArray interfaces;
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            (interface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const auto& entry : interface.addressEntries()) {
            if (interfaces.size() >= 16)
                break;
            interfaces.append(QJsonObject{{QStringLiteral("name"), interface.humanReadableName()},
                                          {QStringLiteral("ip"), entry.ip().toString()}});
        }
    }
    info.insert(QStringLiteral("interfaces"), interfaces);
    auto pair = info.value(family).toObject();
    for (const auto* key : {"local", "proxy"}) {
        const QString side = QLatin1String(key);
        auto value = pair.value(side).toObject();
        value.insert(QStringLiteral("state"), side == QStringLiteral("proxy") && !engine_.ready()
                                                  ? QStringLiteral("ProxyUnavailable")
                                                  : QStringLiteral("Running"));
        value.insert(QStringLiteral("attemptedAt"), clock_().toString(Qt::ISODate));
        if (!value.contains(QStringLiteral("source")))
            value.insert(QStringLiteral("source"), url.host());
        value.insert(QStringLiteral("path"), side == QStringLiteral("local")
                                                 ? QStringLiteral("Direct")
                                                 : QStringLiteral("CurrentProxy"));
        if (value.contains(QStringLiteral("ip")))
            value.insert(QStringLiteral("stale"), true);
        pair.insert(side, value);
    }
    info.insert(family, pair);
    featureState_.insert(QStringLiteral("ipInfo"), info);
    for (const auto* key : {"local", "proxy"}) {
        const QString side = QLatin1String(key);
        const bool proxy = side == QStringLiteral("proxy");
        if (proxy && !engine_.ready())
            continue;
        auto& network = proxy ? ipProxyNetwork_ : ipDirectNetwork_;
        network.setProxy(proxy ? QNetworkProxy(QNetworkProxy::HttpProxy,
                                               QStringLiteral("127.0.0.1"), preferences().mixedPort)
                               : QNetworkProxy(QNetworkProxy::NoProxy));
        network.clearAccessCache();
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "QuickClash/1.0");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,
                             QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                             QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        request.setTransferTimeout(10000);
        auto* reply = network.get(request);
        ipReplies_.append(reply);
        reply->setReadBufferSize(16385);
        auto bytes = std::make_shared<QByteArray>();
        connect(reply, &QIODevice::readyRead, this, [reply, bytes] {
            if (!infra::appendAvailable(*reply, *bytes, 16384))
                reply->abort();
        });
        auto* deadline = new QTimer(reply);
        deadline->setSingleShot(true);
        deadline->start(12000);
        connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
        connect(
            reply, &QNetworkReply::finished, this,
            [this, reply, bytes, generation, family, side, url] {
                reply->deleteLater();
                if (generation != ipGeneration_)
                    return;
                ipReplies_.removeAll(reply);
                const bool read = infra::appendAvailable(*reply, *bytes, 16384);
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString state = QStringLiteral("NetworkError");
                const auto body = QJsonDocument::fromJson(*bytes).object();
                QHostAddress address(body.value(QStringLiteral("ip")).toString());
                const bool valid = !address.isNull() && !address.isLoopback() &&
                                   address.protocol() == (family == QStringLiteral("v4")
                                                              ? QAbstractSocket::IPv4Protocol
                                                              : QAbstractSocket::IPv6Protocol);
                if (!read || bytes->size() > 16384)
                    state = QStringLiteral("ResponseLimit");
                else if (status >= 300 && status < 400)
                    state = QStringLiteral("RedirectBlocked");
                else if (reply->error() == QNetworkReply::NoError && status == 200)
                    state = valid ? QStringLiteral("Ready") : QStringLiteral("InvalidResponse");
                else if (reply->error() == QNetworkReply::SslHandshakeFailedError)
                    state = QStringLiteral("TlsError");
                else if (reply->error() == QNetworkReply::TimeoutError ||
                         reply->error() == QNetworkReply::OperationCanceledError)
                    state = QStringLiteral("Timeout");
                else if (reply->error() == QNetworkReply::HostNotFoundError)
                    state = QStringLiteral("DnsError");
                else if (status > 0)
                    state = QStringLiteral("HttpError");
                auto info = featureState_.value(QStringLiteral("ipInfo")).toObject();
                auto pair = info.value(family).toObject();
                auto value = pair.value(side).toObject();
                value.insert(QStringLiteral("state"), state);
                value.insert(QStringLiteral("status"), status);
                if (state == QStringLiteral("Ready")) {
                    value =
                        QJsonObject{{QStringLiteral("state"), state},
                                    {QStringLiteral("ip"), address.toString()},
                                    {QStringLiteral("source"), url.host()},
                                    {QStringLiteral("time"), clock_().toString(Qt::ISODate)},
                                    {QStringLiteral("path"), side == QStringLiteral("local")
                                                                 ? QStringLiteral("Direct")
                                                                 : QStringLiteral("CurrentProxy")}};
                    for (const auto* field : {"isp", "organization", "country", "region", "city"})
                        value.insert(QLatin1String(field),
                                     body.value(QLatin1String(field)).toString().left(160));
                    const auto asn = body.value(QStringLiteral("asn"));
                    if (asn.isDouble())
                        value.insert(QStringLiteral("asn"), asn);
                    else if (asn.isString())
                        value.insert(QStringLiteral("asn"), asn.toString().left(40));
                }
                pair.insert(side, value);
                info.insert(family, pair);
                featureState_.insert(QStringLiteral("ipInfo"), info);
                publish();
            });
    }
    publish();
}
} // namespace quickclash::application
