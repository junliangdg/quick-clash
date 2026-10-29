#include "application/desktop_runtime.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/geo_resource_writer.h"
#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
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
        runtimePreferences_.value(QStringLiteral("tunDesired")).toBool() ||
        geoSourceBase_.scheme() != QStringLiteral("https") || geoSourceBase_.host().isEmpty() ||
        !geoSourceBase_.userInfo().isEmpty() || geoSourceBase_.hasFragment() ||
        geoSourceBase_.hasQuery()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    busy_ = true;
    lastError_.clear();
    geoOperationActive_ = true;
    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Downloading"));
    publish();
    fetchGeo(0, {}, ++operationGeneration_);
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
    validator_->stop();
    validationDeadline_.stop();
    validationCompletion_ = {};
    staging_.reset();
    restorePreferences();
    busy_ = false;
    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Cancelled"));
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
    validate(std::get<infra::PreflightConfig>(probe).semanticYaml, [this, generation](bool valid) {
        if (generation != operationGeneration_)
            return;
        busy_ = false;
        if (!valid) {
            restorePreferences();
            featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Failed"));
            error(QStringLiteral("GeoResourceIntegrityFailed"));
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
    QNetworkRequest request(geoSourceBase_.resolved(QUrl(file)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(15000);
    auto* reply = directNetwork_.get(request);
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
                snapshot.insert(
                    file, QJsonObject{{QStringLiteral("sha256"), resource.sha256},
                                      {QStringLiteral("sourceRef"), resource.reference},
                                      {QStringLiteral("updatedAt"),
                                       QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
                fetchGeo(index + 1, snapshot, generation);
            });
}
} // namespace quickclash::application
