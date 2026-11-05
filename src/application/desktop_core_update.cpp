#include "application/desktop_runtime.h"
#include "infra/io/bounded_read.h"
#include "platform/core_artifacts.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTemporaryFile>

namespace quickclash::application {
void DesktopRuntime::coreUpdateCommand(const QString& command, const QStringList& args) {
    if (command == QStringLiteral("core_cancel")) {
        cancelCoreUpdate();
        return;
    }
    if (busy_ || store_.readOnly()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    if (command == QStringLiteral("core_check")) {
        const auto artifact = platform::coreArtifact();
        const auto id = artifact.value(QStringLiteral("id")).toString();
        coreUpdateState_ = {
            {QStringLiteral("state"), QStringLiteral("Available")},
            {QStringLiteral("candidate"), id},
            {QStringLiteral("version"), artifact.value(QStringLiteral("version"))},
            {QStringLiteral("route"),
             coreUpdateState_.value(QStringLiteral("route")).toString(QStringLiteral("Direct"))},
            {QStringLiteral("checkedAt"), clock_().toString(Qt::ISODate)}};
        publish();
    } else if (command == QStringLiteral("core_route")) {
        if (args.value(0) == QStringLiteral("Direct") ||
            args.value(0) == QStringLiteral("CurrentProxy")) {
            coreUpdateState_.insert(QStringLiteral("route"), args[0]);
            publish();
        }
    } else if (command == QStringLiteral("core_download")) {
        downloadCore();
    } else if (args.value(0) == QStringLiteral("confirmed")) {
        if (command == QStringLiteral("core_apply"))
            applyCore(coreUpdateState_.value(QStringLiteral("candidate")).toString());
        else if (command == QStringLiteral("core_bundled"))
            applyCore(QStringLiteral("bundled"));
        else if (command == QStringLiteral("core_rollback") &&
                 runtimePreferences_.contains(QStringLiteral("previousCoreArtifactId")))
            applyCore(
                runtimePreferences_.value(QStringLiteral("previousCoreArtifactId")).toString());
    }
}
void DesktopRuntime::cancelCoreUpdate() {
    if (!coreOperationActive_ || (coreUpdateState_.value("state") != "Downloading" &&
                                  coreUpdateState_.value("state") != "Extracting"))
        return;
    ++operationGeneration_;
    coreOperationActive_ = false;
    if (downloadReply_) {
        downloadReply_->abort();
        downloadReply_.clear();
    }
    const auto stopped = coreArchive_.cancel();
    if (const auto* failure = std::get_if<domain::Error>(&stopped)) {
        coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Uncertain"));
        error(failure->code);
    } else {
        coreStage_.reset();
        coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Cancelled"));
    }
    busy_ = false;
    publish();
}
void DesktopRuntime::downloadCore() {
    if (coreArchive_.running()) {
        error(QStringLiteral("CoreStopFailed"));
        return;
    }
    const auto artifact = platform::coreArtifact();
    const auto id = artifact.value(QStringLiteral("id")).toString();
    const auto target = platform::managedCorePath(bundledCoreExecutable(), id);
    if (artifact.isEmpty() || target.isEmpty() ||
        !QDir().mkpath(QFileInfo(target).absolutePath())) {
        error(QStringLiteral("CoreUpdateWriteFailed"));
        return;
    }
    if (platform::verifyCoreArtifact(target, id)) {
        coreUpdateState_.insert(QStringLiteral("candidate"), id);
        coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Verified"));
        publish();
        return;
    }
    if (runtimePreferences_.value("coreArtifactId") == id && engine_.running()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const bool proxy = coreUpdateState_.value("route") == "CurrentProxy";
    if (proxy && !engine_.ready()) {
        error(QStringLiteral("UpdateProxyUnavailable"));
        return;
    }
    resourceNetwork_.setProxy(proxy ? QNetworkProxy(QNetworkProxy::HttpProxy,
                                                    QStringLiteral("127.0.0.1"),
                                                    preferences().mixedPort)
                                    : QNetworkProxy(QNetworkProxy::NoProxy));
    coreStage_ = std::make_unique<QTemporaryDir>(QFileInfo(target).absolutePath() +
                                                 QStringLiteral("/stage_XXXXXX"));
    if (!coreStage_->isValid()) {
        coreStage_.reset();
        error(QStringLiteral("CoreUpdateWriteFailed"));
        return;
    }
    const auto archive = coreStage_->filePath(QStringLiteral("download.zip"));
    auto output = std::make_shared<QFile>(archive);
    if (!output->open(QIODevice::WriteOnly)) {
        coreStage_.reset();
        error(QStringLiteral("CoreUpdateWriteFailed"));
        return;
    }
    busy_ = true;
    coreOperationActive_ = true;
    lastError_.clear();
    coreUpdateState_.insert(QStringLiteral("candidate"), id);
    coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Downloading"));
    const auto generation = ++operationGeneration_;
    const QUrl source =
        coreUpdateSource_.isEmpty() ? QUrl(artifact.value("url").toString()) : coreUpdateSource_;
    if (source.scheme() != QStringLiteral("https") || source.host().isEmpty() ||
        !source.userInfo().isEmpty()) {
        output->close();
        coreStage_.reset();
        busy_ = false;
        error(QStringLiteral("CoreDownloadFailed"));
        return;
    }
    QNetworkRequest request(source);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(15000);
    auto* reply = resourceNetwork_.get(request);
    downloadReply_ = reply;
    reply->setReadBufferSize(infra::kReadChunkSize);
    auto total = std::make_shared<qint64>(0);
    auto valid = std::make_shared<bool>(true);
    const auto limit = artifact.value("archiveBytes").toInteger();
    const auto drain = [reply, output, total, valid, limit] {
        if (!*valid)
            return;
        *valid = infra::consumeAvailable(
                     *reply, *total, limit, [output, total, limit](QByteArrayView chunk) {
                         return *total <= limit &&
                                output->write(chunk.data(), chunk.size()) == chunk.size();
                     }) == infra::ReadStatus::Complete;
        if (!*valid)
            reply->abort();
    };
    connect(reply, &QIODevice::readyRead, this, drain);
    QTimer::singleShot(120000, reply, &QNetworkReply::abort);
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, output, total, valid, drain, generation, archive, id, target, limit] {
            reply->deleteLater();
            if (downloadReply_ == reply)
                downloadReply_.clear();
            if (generation != operationGeneration_) {
                output->close();
                return;
            }
            drain();
            output->close();
            if (!*valid || *total != limit || reply->error() != QNetworkReply::NoError ||
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
                coreStage_.reset();
                busy_ = false;
                error(QStringLiteral("CoreDownloadFailed"));
                return;
            }
            coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Extracting"));
            publish();
            if (generation != operationGeneration_)
                return;
            coreArchive_.extract(
                archive, coreStage_->path(), id,
                [this, generation, id, target](domain::CommandResult result) {
                    if (generation != operationGeneration_)
                        return;
                    const auto artifact = platform::coreArtifact(id);
                    const auto source = coreStage_->filePath(artifact.value("entry").toString());
                    if (const auto* failure = std::get_if<domain::Error>(&result)) {
                        if (!coreArchive_.running())
                            coreStage_.reset();
                        busy_ = false;
                        error(failure->code);
                        return;
                    }
                    if (QFileInfo(source).isSymbolicLink() ||
                        !platform::verifyCoreArtifact(source, id)) {
                        coreStage_.reset();
                        busy_ = false;
                        error(QStringLiteral("CoreIntegrityFailed"));
                        return;
                    }
                    QFile input(source);
                    QSaveFile destination(target);
                    destination.setDirectWriteFallback(false);
                    bool ok =
                        input.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly);
                    qint64 count = 0;
                    if (ok)
                        ok = infra::consumeAvailable(
                                 input, count, artifact.value("executableBytes").toInteger(),
                                 [&destination](QByteArrayView chunk) {
                                     return destination.write(chunk.data(), chunk.size()) ==
                                            chunk.size();
                                 }) == infra::ReadStatus::Complete;
                    ok = ok && count == artifact.value("executableBytes").toInteger() &&
                         destination.commit() && platform::verifyCoreArtifact(target, id);
                    input.close();
                    coreStage_.reset();
                    busy_ = false;
                    coreOperationActive_ = false;
                    if (!ok) {
                        coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Failed"));
                        error(QStringLiteral("CoreUpdateWriteFailed"));
                        return;
                    }
                    coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Verified"));
                    publish();
                });
        });
    publish();
}
void DesktopRuntime::applyCore(const QString& id) {
    if (id.isEmpty() || selectedId_.isEmpty()) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const auto path = platform::managedCorePath(bundledCoreExecutable(), id);
    if (!platform::verifyCoreArtifact(path, id)) {
        error(QStringLiteral("CoreIntegrityFailed"));
        return;
    }
    previousPreferences_ = runtimePreferences_;
    runtimePreferences_.insert(
        QStringLiteral("previousCoreArtifactId"),
        runtimePreferences_.value("coreArtifactId").toString(QStringLiteral("bundled")));
    runtimePreferences_.insert(QStringLiteral("coreArtifactId"), id);
    coreOperationActive_ = true;
    coreUpdateState_.insert(QStringLiteral("state"), QStringLiteral("Applying"));
    applyProfile(selectedId_, engine_.ready());
}
} // namespace quickclash::application
