#include "application/desktop_runtime.h"
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace quickclash::application {
void DesktopRuntime::updateGeo() {
    if (busy_ || store_.readOnly() || selectedId_.isEmpty() ||
        runtimePreferences_.value(QStringLiteral("tunDesired")).toBool()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    busy_ = true;
    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Downloading"));
    publish();
    fetchGeo(0, {}, ++operationGeneration_);
}
void DesktopRuntime::fetchGeo(int index, QJsonObject snapshot, quint64 generation) {
    if (generation != operationGeneration_)
        return;
    if (index == 2) {
        previousPreferences_ = runtimePreferences_;
        runtimePreferences_.insert(QStringLiteral("previousGeoSnapshot"),
                                   runtimePreferences_.value(QStringLiteral("geoSnapshot")));
        runtimePreferences_.insert(QStringLiteral("geoSnapshot"), snapshot);
        // Exercise both databases even when the selected profile does not use Geo rules.
        const auto probe = infra::preflightConfig(
            "rules: ['GEOIP,CN,DIRECT', 'GEOSITE,cn,DIRECT', 'MATCH,DIRECT']\n", preferences());
        if (const auto* failure = std::get_if<domain::Error>(&probe)) {
            restorePreferences();
            busy_ = false;
            error(failure->code);
            return;
        }
        validate(
            std::get<infra::PreflightConfig>(probe).semanticYaml, [this, generation](bool valid) {
                if (generation != operationGeneration_)
                    return;
                busy_ = false;
                if (!valid) {
                    restorePreferences();
                    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Failed"));
                    error(QStringLiteral("GeoResourceIntegrityFailed"));
                    return;
                }
                featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Validated"));
                applyProfile(selectedId_, engine_.ready());
            });
        return;
    }
    const auto file = index == 0 ? QStringLiteral("geoip.dat") : QStringLiteral("geosite.dat");
    QNetworkRequest request(QUrl(
        QStringLiteral("https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/") +
        file));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(15000);
    auto* reply = directNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(32 * 1024 * 1024 + 1);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes] {
        bytes->append(reply->read(32 * 1024 * 1024 + 1 - bytes->size()));
        if (bytes->size() > 32 * 1024 * 1024)
            reply->abort();
    });
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->start(30000);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, file, index, snapshot, generation]() mutable {
                reply->deleteLater();
                if (generation != operationGeneration_)
                    return;
                bytes->append(reply->read(32 * 1024 * 1024 + 1 - bytes->size()));
                if (reply->error() != QNetworkReply::NoError ||
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
                    bytes->isEmpty() || bytes->size() > 32 * 1024 * 1024) {
                    busy_ = false;
                    featureState_.insert(QStringLiteral("geoResult"), QStringLiteral("Failed"));
                    error(QStringLiteral("GeoResourceDownloadFailed"));
                    return;
                }
                const auto digest = QString::fromLatin1(
                    QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256).toHex());
                const auto reference = QStringLiteral("data/geo/%1/%2").arg(digest, file);
                const auto saved = store_.writeBytes(reference, *bytes, true);
                if (const auto* failure = std::get_if<domain::Error>(&saved)) {
                    busy_ = false;
                    error(failure->code);
                    return;
                }
                snapshot.insert(
                    file, QJsonObject{{QStringLiteral("sha256"), digest},
                                      {QStringLiteral("sourceRef"), reference},
                                      {QStringLiteral("updatedAt"),
                                       QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
                fetchGeo(index + 1, snapshot, generation);
            });
}
} // namespace quickclash::application
