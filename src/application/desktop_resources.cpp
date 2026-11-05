#include "application/desktop_runtime.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/geo_resource_writer.h"
#include <QDebug>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <memory>
#include <utility>

namespace quickclash::application {
namespace {
void discardGeo(infra::GeoResourceWriter& writer) {
    const auto cleaned = writer.cancel();
    if (const auto* failure = std::get_if<domain::Error>(&cleaned))
        qWarning("Geo staging cleanup failed: %s", qPrintable(failure->code));
}
} // namespace
void DesktopRuntime::updateGeo() {
    if (busy_ || store_.readOnly() || selectedId_.isEmpty() ||
        geoSourceBase_.scheme() != QStringLiteral("https") || geoSourceBase_.host().isEmpty() ||
        !geoSourceBase_.userInfo().isEmpty() || geoSourceBase_.hasFragment() ||
        geoSourceBase_.hasQuery()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    busy_ = true;
    lastError_.clear();
    geoOperationActive_ = true;
    geoSettings_.insert(QStringLiteral("lastAttempt"), clock_().toString(Qt::ISODate));
    const auto attempt = store_.write(QStringLiteral("config/geo_update.json"), geoSettings_);
    if (const auto* failure = std::get_if<domain::Error>(&attempt)) {
        busy_ = false;
        error(failure->code);
        return;
    }
    scheduleGeo();
    const bool proxy =
        geoSettings_.value(QStringLiteral("route")) == QStringLiteral("CurrentProxy");
    if (proxy && !engine_.ready()) {
        busy_ = false;
        geoOperationActive_ = false;
        error(QStringLiteral("UpdateProxyUnavailable"));
        scheduleGeo();
        return;
    }
    resourceNetwork_.setProxy(proxy ? QNetworkProxy(QNetworkProxy::HttpProxy,
                                                    QStringLiteral("127.0.0.1"),
                                                    preferences().mixedPort)
                                    : QNetworkProxy(QNetworkProxy::NoProxy));
    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Downloading"));
    publish();
    resolveGeoSnapshot(++operationGeneration_);
}
void DesktopRuntime::resolveGeoSnapshot(quint64 generation) {
    if (geoSourceBase_.host() != QStringLiteral("github.com")) {
        // Injected controlled HTTPS origin used by integration tests.
        geoResolvedBase_ = geoSourceBase_;
        geoRevision_ = QStringLiteral("controlled");
        fetchGeo(0, {}, generation);
        return;
    }
    QNetworkRequest request(QUrl(
        QStringLiteral("https://api.github.com/repos/MetaCubeX/meta-rules-dat/commits/release")));
    request.setRawHeader("User-Agent", "QuickClash");
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    auto* reply = resourceNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(64 * 1024);
    auto bytes = std::make_shared<QByteArray>();
    const auto drain = [reply, bytes] {
        while (reply->bytesAvailable() > 0 && bytes->size() <= 256 * 1024)
            bytes->append(reply->read(std::min<qint64>(64 * 1024, 256 * 1024 + 1 - bytes->size())));
        if (bytes->size() > 256 * 1024)
            reply->abort();
    };
    connect(reply, &QIODevice::readyRead, this, drain);
    QTimer::singleShot(30000, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, drain, generation] {
        drain();
        reply->deleteLater();
        if (downloadReply_ == reply)
            downloadReply_.clear();
        if (generation != operationGeneration_)
            return;
        const auto revision =
            QJsonDocument::fromJson(*bytes).object().value(QStringLiteral("sha")).toString();
        if (reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
            bytes->size() > 256 * 1024 ||
            !QRegularExpression(QStringLiteral("^[0-9a-f]{40}$")).match(revision).hasMatch()) {
            busy_ = false;
            error(QStringLiteral("GeoResourceDownloadFailed"));
            scheduleGeo();
            return;
        }
        geoRevision_ = revision;
        geoResolvedBase_ =
            QUrl(QStringLiteral("https://raw.githubusercontent.com/MetaCubeX/meta-rules-dat/%1/")
                     .arg(revision));
        fetchGeo(0, {}, generation);
    });
}
void DesktopRuntime::scheduleGeo() {
    geoTimer_.stop();
    const auto hours = geoSettings_.value(QStringLiteral("intervalHours")).toInt();
    if (!backgroundManaged_ || hours <= 0 || shuttingDown_)
        return;
    const auto last = QDateTime::fromString(
        geoSettings_.value(QStringLiteral("lastAttempt")).toString(), Qt::ISODate);
    const auto remaining = last.isValid() ? clock_().msecsTo(last.addSecs(hours * 3600)) : 1000;
    geoTimer_.start(static_cast<int>(std::clamp<qint64>(remaining, 1000, 7 * 24 * 3600 * 1000)));
}
void DesktopRuntime::configureGeo(const QString& command, const QStringList& args) {
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    if (command == QStringLiteral("apply_pending_geo")) {
        if (geoPending_.isEmpty() || selectedId_.isEmpty() ||
            args.value(0) != QStringLiteral("confirmed"))
            return;
        geoScheduled_ = false;
        applyGeoSnapshot(geoPending_);
        return;
    }
    const auto route = args.value(0);
    const auto hours = args.value(1).toInt();
    if ((route != QStringLiteral("Direct") && route != QStringLiteral("CurrentProxy")) ||
        (hours != 0 && hours != 24 && hours != 168)) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    auto settings = geoSettings_;
    settings.insert(QStringLiteral("route"), route);
    settings.insert(QStringLiteral("intervalHours"), hours);
    const auto saved = store_.write(QStringLiteral("config/geo_update.json"), settings);
    if (const auto* failure = std::get_if<domain::Error>(&saved)) {
        error(failure->code);
        return;
    }
    geoSettings_ = settings;
    scheduleGeo();
    publish();
}
void DesktopRuntime::cancelGeo() {
    const auto state = featureState_.value(QStringLiteral("geoResult")).toString();
    if (!geoOperationActive_ ||
        (state != QStringLiteral("Downloading") && state != QStringLiteral("Validating")))
        return;
    ++operationGeneration_;
    geoOperationActive_ = false;
    if (downloadReply_) {
        downloadReply_->abort();
        downloadReply_.clear();
    }
    const auto stopped = stopValidation();
    if (const auto* failure = std::get_if<domain::Error>(&stopped))
        error(failure->code);
    restorePreferences();
    busy_ = false;
    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Cancelled"));
    geoScheduled_ = false;
    scheduleGeo();
    publish();
}
void DesktopRuntime::applyGeoSnapshot(const QJsonObject& snapshot) {
    previousPreferences_ = runtimePreferences_;
    runtimePreferences_.insert(QStringLiteral("previousGeoSnapshot"),
                               runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject());
    runtimePreferences_.insert(QStringLiteral("geoSnapshot"), snapshot);
    busy_ = true;
    lastError_.clear();
    geoOperationActive_ = true;
    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Validating"));
    const auto generation = ++operationGeneration_;
    publish();
    // Exercise both databases even when the selected profile does not use Geo rules.
    const auto probe = infra::preflightConfig(
        "proxies: []\nproxy-groups: []\nrules: ['GEOIP,CN,DIRECT', 'GEOSITE,cn,DIRECT', "
        "'MATCH,DIRECT']\n",
        preferences());
    if (const auto* failure = std::get_if<domain::Error>(&probe)) {
        restorePreferences();
        busy_ = false;
        error(failure->code);
        return;
    }
    validate(std::get<infra::PreflightConfig>(probe).semanticYaml,
             [this, generation, snapshot](bool valid) {
                 if (generation != operationGeneration_)
                     return;
                 busy_ = false;
                 if (!valid) {
                     restorePreferences();
                     featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Failed"));
                     error(QStringLiteral("GeoResourceIntegrityFailed"));
                     return;
                 }
                 if (geoScheduled_) {
                     restorePreferences();
                     const auto saved = store_.write(QStringLiteral("config/geo_pending.json"),
                                                     {{QStringLiteral("snapshot"), snapshot}});
                     if (const auto* failure = std::get_if<domain::Error>(&saved)) {
                         error(failure->code);
                         return;
                     }
                     geoPending_ = snapshot;
                     geoOperationActive_ = false;
                     geoScheduled_ = false;
                     featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Pending"));
                     publish();
                     return;
                 }
                 featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Applying"));
                 applyProfile(selectedId_, engine_.ready());
             });
}
void DesktopRuntime::fetchGeo(int index, QJsonObject snapshot, quint64 generation) {
    if (generation != operationGeneration_)
        return;
    if (index == 2) {
        geoSettings_.insert(QStringLiteral("lastAttempt"), clock_().toString(Qt::ISODate));
        geoSettings_.insert(QStringLiteral("lastSuccess"), clock_().toString(Qt::ISODate));
        const auto savedSettings =
            store_.write(QStringLiteral("config/geo_update.json"), geoSettings_);
        if (const auto* failure = std::get_if<domain::Error>(&savedSettings)) {
            busy_ = false;
            error(failure->code);
            return;
        }
        scheduleGeo();
        const auto current = runtimePreferences_.value(QStringLiteral("geoSnapshot")).toObject();
        bool unchanged = !current.isEmpty();
        for (const auto* file : {"geoip.dat", "geosite.dat"})
            unchanged &=
                current.value(QLatin1String(file)).toObject().value(QStringLiteral("sourceRef")) ==
                snapshot.value(QLatin1String(file)).toObject().value(QStringLiteral("sourceRef"));
        if (unchanged) {
            busy_ = false;
            geoOperationActive_ = false;
            featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Unchanged"));
            publish();
            return;
        }
        applyGeoSnapshot(snapshot);
        return;
    }
    const auto file = index == 0 ? QStringLiteral("geoip.dat") : QStringLiteral("geosite.dat");
    auto pending = store_.beginGeoWrite();
    if (const auto* failure = std::get_if<domain::Error>(&pending)) {
        busy_ = false;
        featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Failed"));
        error(failure->code);
        return;
    }
    auto writer = std::shared_ptr<infra::GeoResourceWriter>(
        std::move(std::get<std::unique_ptr<infra::GeoResourceWriter>>(pending)));
    QNetworkRequest request(geoResolvedBase_.resolved(QUrl(file)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(15000);
    auto* reply = resourceNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(infra::kReadChunkSize);
    connect(reply, &QIODevice::readyRead, this, [this, reply, writer, generation] {
        if (generation != operationGeneration_) {
            discardGeo(*writer);
            reply->abort();
            return;
        }
        if (std::holds_alternative<domain::Error>(writer->consume(*reply)))
            reply->abort();
    });
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->start(30000);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, writer, file, index, snapshot, generation]() mutable {
                reply->deleteLater();
                if (downloadReply_ == reply)
                    downloadReply_.clear();
                if (generation != operationGeneration_) {
                    discardGeo(*writer);
                    return;
                }
                const auto consumed = writer->consume(*reply);
                if (reply->error() != QNetworkReply::NoError ||
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
                    std::holds_alternative<domain::Error>(consumed)) {
                    discardGeo(*writer);
                    busy_ = false;
                    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Failed"));
                    error(QStringLiteral("GeoResourceDownloadFailed"));
                    return;
                }
                const auto saved = writer->finish(file);
                if (const auto* failure = std::get_if<domain::Error>(&saved)) {
                    busy_ = false;
                    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Failed"));
                    error(failure->code);
                    return;
                }
                const auto& resource = std::get<infra::StoredGeoResource>(saved);
                snapshot.insert(file,
                                QJsonObject{{QStringLiteral("sha256"), resource.sha256},
                                            {QStringLiteral("sourceRef"), resource.reference},
                                            {QStringLiteral("updatedAt"),
                                             QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                                            {QStringLiteral("revision"), geoRevision_}});
                fetchGeo(index + 1, snapshot, generation);
            });
}
} // namespace quickclash::application
