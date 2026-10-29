#include "application/desktop_runtime.h"
#include "domain/support_report.h"
#include "infra/storage/storage_maintenance.h"
#include <QJsonDocument>
#include <QUrl>

namespace quickclash::application {
void DesktopRuntime::clearMaintenancePreview() {
    maintenancePreview_ = {};
    maintenanceToken_.clear();
    maintenanceKind_.clear();
    maintenanceFingerprint_.clear();
    featureState_.remove(QStringLiteral("maintenancePreview"));
}
void DesktopRuntime::maintenanceCommand(const QString& command, const QStringList& args) {
    if (command == QStringLiteral("cancel_maintenance")) {
        clearMaintenancePreview();
        publish();
        return;
    }
    const auto idle = [this] {
        return !busy_ && !applying_ && !downloadReply_ && !configTransaction_ && !backupRestore_ &&
               !providerImport_ && !geoOperationActive_ && !validator_->running();
    };
    const auto repairAllowed = [this, &idle] {
        if (!idle() || !store_.readOnly() || engine_.running() || proxyLease_->active() ||
            proxyLease_->conflict() || recoveryArchived_)
            return false;
        const auto safe = proxyLease_->safeToArchive();
        return std::holds_alternative<bool>(safe) && std::get<bool>(safe);
    };
    if (command == QStringLiteral("diagnostics") || command == QStringLiteral("storage_preview") ||
        command == QStringLiteral("repair_preview")) {
        clearMaintenancePreview();
        maintenanceKind_ = command;
        if (command == QStringLiteral("diagnostics")) {
            // snapshot() never requests page data; only the small App ring is counted.
            auto state = snapshot();
            state.insert(QStringLiteral("logs"), appLogs_.entries());
            maintenancePreview_ = domain::supportReport(state);
        } else {
            if ((command == QStringLiteral("repair_preview") && !repairAllowed()) ||
                (command == QStringLiteral("storage_preview") && !idle())) {
                clearMaintenancePreview();
                error(QStringLiteral("MaintenanceUnavailable"));
                return;
            }
            const auto inventory =
                infra::inspectStorage(store_, command == QStringLiteral("repair_preview"));
            if (const auto* failure = std::get_if<domain::Error>(&inventory)) {
                clearMaintenancePreview();
                error(failure->code);
                return;
            }
            maintenancePreview_ = std::get<infra::StorageInventory>(inventory).summary();
            maintenanceFingerprint_ = std::get<infra::StorageInventory>(inventory).fingerprint;
        }
        maintenanceToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        featureState_.insert(
            QStringLiteral("maintenancePreview"),
            QJsonObject{
                {"token", maintenanceToken_},
                {"kind", maintenanceKind_},
                {"text", QString::fromUtf8(
                             QJsonDocument(maintenancePreview_).toJson(QJsonDocument::Indented))}});
        publish();
        return;
    }
    if (maintenanceToken_.isEmpty() || args.value(0) != maintenanceToken_ ||
        args.value(1) != maintenanceKind_) {
        error(QStringLiteral("StoragePreviewStale"));
        return;
    }
    const auto kind = maintenanceKind_;
    const auto preview = maintenancePreview_;
    const auto fingerprint = maintenanceFingerprint_;
    clearMaintenancePreview();
    if (kind == QStringLiteral("diagnostics")) {
        const auto result = store_.writeSupport(preview);
        if (const auto* failure = std::get_if<domain::Error>(&result))
            error(failure->code);
        else
            featureState_.insert(
                QStringLiteral("exportUrl"),
                QUrl::fromLocalFile(store_.root() + '/' + std::get<QString>(result)).toString());
    } else if (kind == QStringLiteral("repair_preview")) {
        if (!repairAllowed()) {
            error(QStringLiteral("MaintenanceUnavailable"));
            return;
        }
        const auto result = store_.archiveForRecovery(fingerprint);
        if (const auto* failure = std::get_if<domain::Error>(&result))
            error(failure->code);
        else {
            recoveryArchived_ = true;
            featureState_.remove(QStringLiteral("exportUrl"));
            featureState_.remove(QStringLiteral("backupUrl"));
            subscriptionTimer_.stop();
            featureState_.insert(
                QStringLiteral("recoveryArchiveUrl"),
                QUrl::fromLocalFile(store_.root() + '/' + std::get<QString>(result)).toString());
            featureState_.insert(QStringLiteral("maintenanceResult"),
                                 QStringLiteral("RestartRequired"));
        }
    } else {
        if (!idle() || store_.readOnly() || repository_.recoveryRequired()) {
            error(QStringLiteral("MaintenanceUnavailable"));
            return;
        }
        const auto inventory = infra::inspectStorage(store_);
        if (const auto* failure = std::get_if<domain::Error>(&inventory)) {
            error(failure->code);
            return;
        }
        if (std::get<infra::StorageInventory>(inventory).fingerprint != fingerprint) {
            error(QStringLiteral("StoragePreviewStale"));
            return;
        }
        const auto result =
            infra::trimStorage(store_, std::get<infra::StorageInventory>(inventory));
        if (const auto* failure = std::get_if<domain::Error>(&result))
            error(failure->code);
        else
            featureState_.insert(QStringLiteral("storageCleanup"), std::get<QJsonObject>(result));
    }
    publish();
}
} // namespace quickclash::application
