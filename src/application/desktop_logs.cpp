#include "application/desktop_runtime.h"
#include <QJsonDocument>

namespace quickclash::application {
void DesktopRuntime::clearLogExport() {
    logExport_ = {};
    logExportFrozen_ = {};
    logExportToken_.clear();
    featureState_.remove(QStringLiteral("logExportPreview"));
    featureState_.remove(QStringLiteral("logExportResult"));
}
void DesktopRuntime::prepareLogExport(bool hideMessages, const QString& token) {
    if (!featureDemands_.contains(QStringLiteral("logs"))) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    if (!token.isEmpty() && (token != logExportToken_ || logExportFrozen_.isEmpty())) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    if (token.isEmpty()) {
        clearLogExport();
        logExportFrozen_ = logs_.status();
        logExportFrozen_.insert(QStringLiteral("createdAt"), clock_().toString(Qt::ISODate));
        logExportFrozen_.insert(QStringLiteral("logs"), logs_.entries());
    }
    QJsonArray entries;
    for (const auto& value : logExportFrozen_.value(QStringLiteral("logs")).toArray()) {
        auto entry = value.toObject();
        if (hideMessages)
            entry.insert(QStringLiteral("message"), QStringLiteral("[hidden]"));
        entries.append(entry);
    }
    logExport_ = logExportFrozen_;
    logExport_.insert(QStringLiteral("application"), QStringLiteral("QuickClash"));
    logExport_.insert(QStringLiteral("schemaVersion"), 1);
    logExport_.insert(QStringLiteral("messagesHidden"), hideMessages);
    logExport_.insert(QStringLiteral("logs"), entries);
    logExport_.insert(QStringLiteral("from"),
                      entries.isEmpty() ? QJsonValue{}
                                        : entries.first().toObject().value(QStringLiteral("time")));
    logExport_.insert(QStringLiteral("to"),
                      entries.isEmpty() ? QJsonValue{}
                                        : entries.last().toObject().value(QStringLiteral("time")));
    logExportToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto preview = logExport_;
    preview.remove(QStringLiteral("logs"));
    preview.insert(QStringLiteral("token"), logExportToken_);
    const auto text = QJsonDocument(logExport_).toJson(QJsonDocument::Indented);
    preview.insert(QStringLiteral("text"), QString::fromUtf8(text.left(16384)));
    preview.insert(QStringLiteral("previewTruncated"), text.size() > 16384);
    featureState_.insert(QStringLiteral("logExportPreview"), preview);
    publish();
}
void DesktopRuntime::exportLogs(const QString& token) {
    if (token.isEmpty() || token != logExportToken_ || logExport_.isEmpty() ||
        !featureDemands_.contains(QStringLiteral("logs"))) {
        error(QStringLiteral("OperationUnavailable"));
        return;
    }
    const auto path = QStringLiteral("logs/redacted_logs.json");
    const auto result =
        store_.writeBytes(path, QJsonDocument(logExport_).toJson(QJsonDocument::Compact));
    if (const auto* failure = std::get_if<domain::Error>(&result)) {
        error(failure->code);
        return;
    }
    clearLogExport();
    featureState_.insert(QStringLiteral("logExportResult"), QStringLiteral("Exported"));
    featureState_.insert(QStringLiteral("exportUrl"),
                         QUrl::fromLocalFile(store_.root() + '/' + path).toString());
    publish();
}
} // namespace quickclash::application
