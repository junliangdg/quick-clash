#include "domain/support_report.h"
#include <QJsonArray>
#include <QStringList>
#include <QSysInfo>
#include <algorithm>

namespace quickclash::domain {
namespace {
QString choice(const QJsonValue& value, const QStringList& allowed) {
    return allowed.contains(value.toString()) ? value.toString() : QStringLiteral("Unknown");
}
} // namespace
QJsonObject supportReport(const QJsonObject& runtime) {
    const auto settings = runtime.value("settingsState").toObject().value("saved").toObject();
    const auto features = runtime.value("features").toObject();
    QJsonObject counts{{"app", 0}, {"core", 0}, {"other", 0}};
    const auto logs = runtime.value("logs").toArray();
    for (qsizetype i = 0; i < std::min<qsizetype>(100, logs.size()); ++i) {
        const auto source = choice(logs[i].toObject().value("source"), {"App", "Core"});
        const auto key = source == "App" ? "app" : source == "Core" ? "core" : "other";
        counts.insert(key, counts.value(key).toInt() + 1);
    }
    QJsonArray errors;
    const QStringList codes{"SchemaTooNew",
                            "MetadataRecoveryRequired",
                            "CatalogRecoveryRequired",
                            "ManifestRecoveryRequired",
                            "SettingsRecoveryRequired",
                            "SourceIntegrityFailed",
                            "ArtifactIntegrityFailed",
                            "JournalRecoveryRequired",
                            "JournalManifestMismatch",
                            "ProxyLeaseConflict",
                            "OperationUnavailable",
                            "DataWriteFailed",
                            "DataReadFailed"};
    const auto recent = runtime.value("errors").toArray();
    for (qsizetype i = std::max<qsizetype>(0, recent.size() - 16); i < recent.size(); ++i)
        errors.append(choice(recent[i].toObject().value("code"), codes));
#ifdef Q_OS_WIN
    const auto platform = QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    const auto platform = QStringLiteral("macOS");
#else
    const auto platform = QStringLiteral("Other");
#endif
    return {
        {"schemaVersion", 1},
        {"application", "QuickClash"},
        {"version", "0.1.0"},
        {"coreVersion", "v1.19.33"},
        {"platform", platform},
        {"architecture", choice(QSysInfo::currentCpuArchitecture(), {"x86_64", "arm64", "i386"})},
        {"status", choice(runtime.value("status"),
                          {"Ready", "Stopped", "Failed", "Starting", "Applying", "Recovering"})},
        {"readOnly", runtime.value("readOnly").toBool()},
        {"hasError", !runtime.value("error").toString().isEmpty()},
        {"recentErrors", errors},
        {"profileCount", std::min<qsizetype>(100, runtime.value("profiles").toArray().size())},
        {"settings",
         QJsonObject{
             {"mode", choice(settings.value("mode"), {"rule", "direct", "global"})},
             {"dnsMode", choice(settings.value("dnsMode"), {"profile", "fake-ip", "redir-host"})},
             {"tunDesired", settings.value("tunDesired").toBool()}}},
        {"logs", counts},
        {"logMessagesIncluded", false},
        {"resources",
         QJsonObject{
             {"ruleCount", std::min<qsizetype>(20000, runtime.value("rules").toArray().size())},
             {"providerCount",
              std::min<qsizetype>(1000, runtime.value("providers").toArray().size())},
             {"memorySampleAvailable", features.value("memory").isObject()}}}};
}
} // namespace quickclash::domain
