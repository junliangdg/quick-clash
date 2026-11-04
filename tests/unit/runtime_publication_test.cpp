#include "application/desktop_runtime.h"
#include "infra/storage/latest_mailbox.h"
#include "infra/storage/settings_service.h"
#include "platform/process_memory.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
class ProxySettings final : public quickclash::platform::ProxySettings {
  public:
    quickclash::domain::Result<QJsonObject> read() override {
        return QJsonObject{};
    }
    quickclash::domain::CommandResult write(const QJsonObject&) override {
        ++writes;
        return std::monostate{};
    }
    int writes = 0;
};
} // namespace
class RuntimePublicationTest final : public QObject {
    Q_OBJECT
  private slots:
    void maintenancePreviewMemoryProbe() {
        if (!qEnvironmentVariableIsSet("QUICKCLASH_MAINTENANCE_MEMORY"))
            QSKIP("Opt-in 10 second resource probe");
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.initialize();
        const auto baseline = quickclash::platform::currentProcessMemory();
        QJsonObject maximum = baseline;
        QElapsedTimer elapsed;
        elapsed.start();
        int iterations = 0;
        while (elapsed.elapsed() < 10000) {
            runtime.featureCommand("diagnostics", {});
            const auto preview = runtime.snapshot()
                                     .value("features")
                                     .toObject()
                                     .value("maintenancePreview")
                                     .toObject();
            QVERIFY(preview.value("text").toString().size() < 4096);
            runtime.featureCommand("cancel_maintenance", {});
            runtime.featureCommand("storage_preview", {});
            runtime.featureCommand("cancel_maintenance", {});
            QTest::qWait(50);
            const auto sample = quickclash::platform::currentProcessMemory();
            for (const auto* key : {"privateWorkingSet", "workingSet", "privateCommit"})
                if (sample.value(key).toString().toULongLong() >
                    maximum.value(key).toString().toULongLong())
                    maximum.insert(key, sample.value(key));
            ++iterations;
        }
        const auto after = quickclash::platform::currentProcessMemory();
        qInfo().noquote() << "Maintenance baseline:"
                          << QJsonDocument(baseline).toJson(QJsonDocument::Compact);
        qInfo().noquote() << "Maintenance sampled maximum:"
                          << QJsonDocument(maximum).toJson(QJsonDocument::Compact);
        qInfo().noquote() << "Maintenance after release:"
                          << QJsonDocument(after).toJson(QJsonDocument::Compact) << "iterations"
                          << iterations << "elapsedMs" << elapsed.elapsed();
        QVERIFY(!runtime.snapshot().value("features").toObject().contains("maintenancePreview"));
        QVERIFY(runtime.snapshot().value("connections").toArray().isEmpty() &&
                runtime.snapshot().value("logs").toArray().isEmpty());
        QCOMPARE(runtime.snapshot().value("corePid").toInt(), 0);
        QCOMPARE(settings.writes, 0);
    }
    void damagedSettingsStillExposeRecoveryCommands() {
        QTemporaryDir directory;
        {
            quickclash::infra::JsonStore store(directory.path());
            QVERIFY(std::holds_alternative<std::monostate>(store.open()));
            QVERIFY(std::holds_alternative<std::monostate>(
                store.writeBytes("config/settings.json", "{\"schemaVersion\":99}")));
        }
        quickclash::infra::SettingsService service(directory.path());
        QSignalSpy loaded(&service, &quickclash::infra::SettingsService::loaded);
        QSignalSpy control(&service, &quickclash::infra::SettingsService::runtimeChanged);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 3000);
        QVERIFY(!control.isEmpty() &&
                control.last().first().toJsonObject().value("readOnly").toBool());
        service.command("diagnostics");
        QTRY_VERIFY_WITH_TIMEOUT(control.last()
                                     .first()
                                     .toJsonObject()
                                     .value("features")
                                     .toObject()
                                     .contains("maintenancePreview"),
                                 3000);
        const auto value = control.last().first().toJsonObject();
        QVERIFY(!value.value("ready").toBool() && value.value("corePid").toInt() == 0);
        service.command("cancel_maintenance");
        QTRY_VERIFY_WITH_TIMEOUT(!control.last()
                                      .first()
                                      .toJsonObject()
                                      .value("features")
                                      .toObject()
                                      .contains("maintenancePreview"),
                                 3000);
    }
    void supportPreviewIsFrozenAndWorksWithoutCore() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.initialize();
        runtime.featureCommand("diagnostics", {});
        const auto preview =
            runtime.snapshot().value("features").toObject().value("maintenancePreview").toObject();
        const auto token = preview.value("token").toString();
        QVERIFY(!token.isEmpty());
        QVERIFY(std::get<QStringList>(store.files("logs")).isEmpty());
        QVERIFY(runtime.snapshot().value("logs").toArray().isEmpty());
        QVERIFY(runtime.snapshot().value("connections").toArray().isEmpty());
        runtime.featureCommand("confirm_maintenance", {"old", "diagnostics"});
        QVERIFY(!runtime.snapshot().value("features").toObject().contains("exportUrl"));
        store.freeze();
        runtime.featureCommand("confirm_maintenance", {token, "diagnostics"});
        const auto url =
            runtime.snapshot().value("features").toObject().value("exportUrl").toString();
        QVERIFY(!url.isEmpty());
        QFile file(QUrl(url).toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object(),
                 QJsonDocument::fromJson(preview.value("text").toString().toUtf8()).object());
        runtime.featureCommand("diagnostics", {});
        const auto cancelled = runtime.snapshot()
                                   .value("features")
                                   .toObject()
                                   .value("maintenancePreview")
                                   .toObject()
                                   .value("token")
                                   .toString();
        runtime.featureCommand("cancel_maintenance", {});
        runtime.featureCommand("confirm_maintenance", {cancelled, "diagnostics"});
        QCOMPARE(std::get<QStringList>(store.files("logs")).size(),
                 2); // errors + one confirmed report
        QCOMPARE(settings.writes, 0);
    }
    void readOnlyRepairPreservesFutureSchemaAndRestartsEmpty() {
        QTemporaryDir directory;
        QString archive;
        {
            quickclash::infra::JsonStore store(directory.path());
            QVERIFY(std::holds_alternative<std::monostate>(store.open()));
            QVERIFY(std::holds_alternative<std::monostate>(
                store.writeBytes("data/catalog.json", "{\"schemaVersion\":99}")));
            ProxySettings settings;
            quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
            runtime.initialize();
            QVERIFY(store.readOnly());
            runtime.featureCommand("repair_preview", {});
            const auto preview = runtime.snapshot()
                                     .value("features")
                                     .toObject()
                                     .value("maintenancePreview")
                                     .toObject();
            QVERIFY(!preview.value("token").toString().isEmpty());
            runtime.featureCommand("confirm_maintenance",
                                   {preview.value("token").toString(), "repair_preview"});
            const auto features = runtime.snapshot().value("features").toObject();
            QCOMPARE(features.value("maintenanceResult").toString(), QString("RestartRequired"));
            archive = QUrl(features.value("recoveryArchiveUrl").toString()).toLocalFile();
            QFile original(archive + "/data/catalog.json");
            QVERIFY(original.open(QIODevice::ReadOnly));
            QCOMPARE(original.readAll(), QByteArray("{\"schemaVersion\":99}"));
            QVERIFY(store.readOnly() && !runtime.snapshot().value("ready").toBool());
            QCOMPARE(settings.writes, 0);
        }
        quickclash::infra::JsonStore reopened(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(reopened.open()));
        QVERIFY(!reopened.readOnly());
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(reopened, nullptr, &settings);
        runtime.initialize();
        QVERIFY(runtime.snapshot().value("profiles").toArray().isEmpty());
        QVERIFY(QFileInfo::exists(archive + "/data/catalog.json"));
        QVERIFY(!runtime.snapshot().value("ready").toBool());
    }
    void storageCleanupRejectsChangedInventoryAndUnresolvedLease() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.initialize();
        runtime.featureCommand("storage_preview", {});
        const auto token = runtime.snapshot()
                               .value("features")
                               .toObject()
                               .value("maintenancePreview")
                               .toObject()
                               .value("token")
                               .toString();
        QVERIFY(std::holds_alternative<std::monostate>(
            store.writeBytes("cache/downloads/late", "changed")));
        runtime.featureCommand("confirm_maintenance", {token, "storage_preview"});
        QCOMPARE(runtime.snapshot().value("error").toString(), QString("StoragePreviewStale"));
        QVERIFY(QFileInfo::exists(directory.filePath("cache/downloads/late")));
        QVERIFY(std::holds_alternative<std::monostate>(
            store.write("data/leases/windows_lan.json", {{"status", "Applied"}})));
        store.freeze();
        runtime.featureCommand("repair_preview", {});
        QVERIFY(!runtime.snapshot().value("features").toObject().contains("maintenancePreview"));
        QVERIFY(QFileInfo::exists(directory.filePath("data/leases/windows_lan.json")));
        QCOMPARE(settings.writes, 0);
    }
    void garbageCollectionKeepsDraftsAndStopsOnBadMetadata() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        QVERIFY(std::holds_alternative<std::monostate>(
            store.writeBytes("data/profiles/draft/source.yaml", "draft source")));
        QVERIFY(std::holds_alternative<std::monostate>(store.write(
            "data/drafts/draft.json", {{"baseSourceRef", "data/profiles/draft/source.yaml"}})));
        const auto delayKey = "data/credentials/6c7a5e7f-56c4-4f6e-a2c1-15c0a2f2ef31.blob";
        QVERIFY(std::holds_alternative<std::monostate>(store.writeBytes(delayKey, "protected")));
        ProxySettings settings;
        {
            quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
            runtime.initialize();
            QVERIFY(QFileInfo::exists(directory.filePath("data/profiles/draft/source.yaml")));
            QVERIFY(QFileInfo::exists(directory.filePath(delayKey)));
        }
        QVERIFY(std::holds_alternative<std::monostate>(
            store.writeBytes("data/profiles/orphan/source.yaml", "retain on failure")));
        QVERIFY(std::holds_alternative<std::monostate>(
            store.writeBytes("data/drafts/broken.json", "{broken")));
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.initialize();
        QVERIFY(QFileInfo::exists(directory.filePath("data/profiles/orphan/source.yaml")));
    }
    void resourceSamplingIsSharedAndReleased() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.setDemands({"resources"});
        const auto first = runtime.snapshot().value("features").toObject();
        const auto host = first.value("memory").toObject().value("host").toObject();
#ifdef Q_OS_WIN
        QVERIFY(host.value("privateWorkingSet").toString().toULongLong() > 0);
        auto identity = quickclash::platform::processIdentity(
            static_cast<quint32>(QCoreApplication::applicationPid()));
        QVERIFY(identity.created != 0);
        QVERIFY(!quickclash::platform::processMemory(identity).isEmpty());
        ++identity.created;
        QVERIFY(quickclash::platform::processMemory(identity).isEmpty());
        QVERIFY(quickclash::platform::processMemory({}).isEmpty());
#endif
        for (int i = 0; i < 20; ++i)
            runtime.setDemands({"resources"});
        QCOMPARE(runtime.snapshot().value("features").toObject().value("memorySample"),
                 first.value("memorySample"));
        // The one-second feature timer refreshes resources only after the full
        // five-second sampling interval. Keep enough headroom for delayed timer
        // delivery on a loaded Windows CI runner without disabling the timeout.
        QTRY_VERIFY_WITH_TIMEOUT(
            runtime.snapshot().value("features").toObject().value("memorySample") !=
                first.value("memorySample"),
            15000);
        const auto second = runtime.snapshot().value("features").toObject();
        QVERIFY(second.value("memorySample") != first.value("memorySample"));
        qInfo().noquote()
            << "Resource first:"
            << QJsonDocument(first.value("memory").toObject()).toJson(QJsonDocument::Compact);
        qInfo().noquote()
            << "Resource second:"
            << QJsonDocument(second.value("memory").toObject()).toJson(QJsonDocument::Compact);
        runtime.setDemands({});
        QVERIFY(!runtime.snapshot().value("features").toObject().contains("memory"));
        QTest::qWait(5100);
        QVERIFY(!runtime.snapshot().value("features").toObject().contains("memory"));
        runtime.setDemands({"resources"});
        QVERIFY(runtime.snapshot().value("features").toObject().contains("memory"));
        QCOMPARE(settings.writes, 0);
    }
    void logExportIsFrozenAndInvalidatedOnRelease() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.setDemands({"logs"});
        runtime.featureCommand("not_ready", {});
        const auto count = runtime.snapshot().value("logs").toArray().size();
        runtime.featureCommand("preview_logs", {"include_messages"});
        const auto preview =
            runtime.snapshot().value("features").toObject().value("logExportPreview").toObject();
        const auto token = preview.value("token").toString();
        QVERIFY(!token.isEmpty() &&
                preview.value("text").toString().contains("OperationUnavailable"));
        QVERIFY(!QFileInfo::exists(directory.filePath("logs/redacted_logs.json")));
        runtime.featureCommand("not_ready", {});
        runtime.featureCommand("preview_logs", {"hide_messages", token});
        const auto hidden =
            runtime.snapshot().value("features").toObject().value("logExportPreview").toObject();
        QCOMPARE(hidden.value("logEntries").toInt(), count);
        QVERIFY(!hidden.value("text").toString().contains("OperationUnavailable"));
        runtime.featureCommand("preview_logs",
                               {"include_messages", hidden.value("token").toString()});
        const auto finalToken = runtime.snapshot()
                                    .value("features")
                                    .toObject()
                                    .value("logExportPreview")
                                    .toObject()
                                    .value("token")
                                    .toString();
        runtime.featureCommand("export_logs", {finalToken});
        const auto report = store.read("logs/redacted_logs.json");
        QVERIFY(std::holds_alternative<std::optional<QJsonObject>>(report));
        const auto exported = *std::get<std::optional<QJsonObject>>(report);
        QCOMPARE(exported.value("logs").toArray().size(), count);
        runtime.featureCommand("preview_logs", {"hide_messages"});
        auto next =
            runtime.snapshot().value("features").toObject().value("logExportPreview").toObject();
        QVERIFY2(!next.value("text").toString().contains("OperationUnavailable"),
                 qPrintable(next.value("text").toString()));
        runtime.featureCommand("cancel_log_export", {});
        QVERIFY(!runtime.snapshot().value("features").toObject().contains("logExportPreview"));
        quickclash::application::RuntimeDemands demands;
        demands.setActive({"logs", "log_feed"});
        demands.setActive({"logs"});
        const auto pausedCount = runtime.snapshot().value("logs").toArray().size();
        runtime.applyDemands(demands);
        runtime.featureCommand("not_ready", {});
        QCOMPARE(runtime.snapshot().value("logs").toArray().size(), pausedCount);
        runtime.featureCommand("preview_logs", {});
        next = runtime.snapshot().value("features").toObject().value("logExportPreview").toObject();
        QCOMPARE(next.value("logGaps").toString(), QStringLiteral("1"));
        const auto old = next.value("token").toString();
        runtime.setDemands({});
        QVERIFY(!runtime.snapshot().value("features").toObject().contains("logExportPreview"));
        runtime.featureCommand("export_logs", {old});
        QCOMPARE(*std::get<std::optional<QJsonObject>>(store.read("logs/redacted_logs.json")),
                 exported);
        QCOMPARE(settings.writes, 0);
    }
    void websiteValidationAndIdleCancellation() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.initialize();
        runtime.testWebsite(QStringLiteral("https://127.0.0.1:1/"));
        QCOMPARE(runtime.snapshot().value("error"), QJsonValue("ReadyProxyAndHttpsTargetRequired"));
        runtime.featureCommand("test_batch", {"wrong_path", "https://127.0.0.1:1/"});
        QCOMPARE(runtime.snapshot().value("error"), QJsonValue("ReadyProxyAndHttpsTargetRequired"));
        QVERIFY(runtime.snapshot().value("testTarget").toString().isEmpty());
        runtime.featureCommand("test_batch", {"Direct", "https://user:secret@127.0.0.1/"});
        QVERIFY(runtime.snapshot().value("testTarget").toString().isEmpty());
        const auto before = runtime.snapshot().value("features");
        runtime.featureCommand("cancel_test", {});
        QCOMPARE(runtime.snapshot().value("features"), before);
        runtime.testWebsite(QStringLiteral("https://127.0.0.1:1/"),
                            QStringLiteral("SystemDefault"));
        const auto probe =
            runtime.snapshot().value("features").toObject().value("websiteTest").toObject();
        QCOMPARE(probe.value("path"), QJsonValue("SystemDefault"));
        QVERIFY(!probe.value("resolved").toString().isEmpty());
        runtime.featureCommand("cancel_test", {});
        QCOMPARE(settings.writes, 0);
    }
    void networkSettingsValidationPreservesSavedValues() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.initialize();
        const auto defaults = runtime.snapshot().value("preferences");
        const auto epoch = runtime.snapshot().value("epoch").toString();
        runtime.featureCommand("network_settings", {"rule", "7890", "7890", "profile", epoch, "0"});
        QCOMPARE(runtime.snapshot().value("error"), QJsonValue("InvalidNetworkSettings"));
        QCOMPARE(runtime.snapshot().value("preferences"), defaults);
        runtime.featureCommand("network_settings",
                               {"invalid", "7890", "9090", "profile", epoch, "0"});
        QCOMPARE(runtime.snapshot().value("preferences"), defaults);
        runtime.featureCommand("network_settings",
                               {"global", "7891", "9091", "redir-host", epoch, "0"});
        const auto state = runtime.snapshot().value("settingsState").toObject();
        QCOMPARE(state.value("saved").toObject().value("mode"), QJsonValue("global"));
        QCOMPARE(state.value("saved").toObject().value("dnsMode"), QJsonValue("redir-host"));
        QVERIFY(!state.value("pending").toBool());
        QVERIFY(!state.value("effective").toObject().contains("mode"));
        quickclash::application::DesktopRuntime reopened(store, nullptr, &settings);
        reopened.initialize();
        QCOMPARE(reopened.snapshot().value("settingsState").toObject().value("saved"),
                 state.value("saved"));
        QCOMPARE(settings.writes, 0);
    }
    void settingsRequestReceiptsAndWriteFailurePreserveSavedFile() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        runtime.initialize();
        const auto epoch = runtime.snapshot().value("epoch").toString();
        const auto receipt = [&runtime](const char* name) {
            return runtime.snapshot()
                .value("features")
                .toObject()
                .value(QLatin1String(name))
                .toObject();
        };
        runtime.featureCommand("network_settings",
                               {"global", "17891", "19091", "profile", epoch, "0", "request-a"});
        QCOMPARE(receipt("settingsNetworkRequest"),
                 (QJsonObject{{"id", "request-a"}, {"state", "Accepted"}}));
        const auto saved = runtime.snapshot().value("settingsState").toObject().value("saved");
        const auto path = directory.filePath("config/runtime_defaults.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        file.close();
#ifdef Q_OS_WIN
        // Deny writing/replacement of only this temporary fixture, including QSaveFile commit.
        const auto handle =
            CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        runtime.featureCommand("network_settings",
                               {"direct", "17892", "19092", "profile", epoch, "0", "request-b"});
        CloseHandle(handle);
        QCOMPARE(receipt("settingsNetworkRequest").value("id"), QJsonValue("request-b"));
        QCOMPARE(runtime.snapshot().value("settingsState").toObject().value("saved"), saved);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
        file.close();
#endif
        runtime.featureCommand("network_settings", {"direct", "17892", "19092", "profile",
                                                    "old-epoch", "0", "request-c"});
        QCOMPARE(receipt("settingsNetworkRequest"),
                 (QJsonObject{{"id", "request-c"}, {"state", "Rejected"}}));
        QCOMPARE(runtime.snapshot().value("settingsState").toObject().value("saved"), saved);
        runtime.featureCommand("delay_target", {"http://invalid.example/", "target-a"});
        QCOMPARE(receipt("settingsTargetRequest"),
                 (QJsonObject{{"id", "target-a"}, {"state", "Failed"}}));
        store.freeze();
        runtime.featureCommand("delay_target", {"https://example.org/", "target-b"});
        QCOMPARE(receipt("settingsTargetRequest"),
                 (QJsonObject{{"id", "target-b"}, {"state", "Failed"}}));
        QCOMPARE(settings.writes, 0);
    }
    void serviceLogDemandRetriesAndClearsReusedLease() {
        quickclash::infra::ServiceLogDemand demand;
        QVERIFY(!demand.begin(0, false).has_value());
        const auto first = demand.begin(1, true);
        QVERIFY(first.has_value() && *first);
        QVERIFY(!demand.begin(1, true).has_value());
        demand.finish(1, true, true);
        QVERIFY(demand.receiving(1));
        const auto disable = demand.begin(2, false);
        QVERIFY(disable.has_value() && !*disable);
        demand.finish(2, false, false);
        QVERIFY(!demand.receiving(2));
        const auto retry = demand.begin(2, false);
        QVERIFY(retry.has_value() && !*retry);
        // Re-entry while disable is in flight cannot bypass the acknowledged reset.
        QVERIFY(!demand.begin(3, true).has_value());
        demand.finish(2, false, true);
        const auto reenable = demand.begin(3, true);
        QVERIFY(reenable.has_value() && *reenable);
        demand.finish(3, true, true);
        QVERIFY(demand.receiving(3));
        // An old enable acknowledgment also needs a reset before a newer enabled lease.
        const auto reset = demand.begin(5, true);
        QVERIFY(reset.has_value() && !*reset);
        QVERIFY(!demand.receiving(5));
        demand.finish(5, false, true);
        const auto fresh = demand.begin(5, true);
        QVERIFY(fresh.has_value() && *fresh);
        demand.finish(5, true, true);
        QVERIFY(demand.receiving(5));
        QVERIFY(!demand.begin(5, true).has_value());
    }
    void readTicketsRejectSameSessionReuse() {
        using namespace quickclash::application;
        for (const auto* name : {"connections", "rules", "providers", "resources"}) {
            const auto stream = QLatin1String(name);
            RuntimeDemands demands;
            demands.setActive({stream});
            const RuntimeRequest request{stream, "session", demands.generations.value(stream), 4,
                                         10};
            QVERIFY(request.matches(demands, "session", 4, 10));
            demands.setActive({});
            QVERIFY(!request.matches(demands, "session", 4, 10));
            demands.setActive({stream});
            QVERIFY(!request.matches(demands, "session", 4, 10));
            const RuntimeRequest current{stream, "session", demands.generations.value(stream), 4,
                                         11};
            QVERIFY(current.matches(demands, "session", 4, 11));
            QVERIFY(!current.matches(demands, "other", 4, 11));
            QVERIFY(!current.matches(demands, "session", 5, 11));
            QVERIFY(!current.matches(demands, "session", 4, 12));
        }
    }
    void workerReleaseAndCompleteResend() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        QSignalSpy updates(&runtime, &quickclash::application::DesktopRuntime::dataPublished);
        runtime.setDemands({"logs", "rules", "providers", "connections"});
        // Buffer capacity is covered in-memory by contracts_test. Keep this integration test
        // focused on publication/release behavior instead of persisting thousands of errors.
        for (int i = 0; i < 3; ++i)
            runtime.featureCommand("not_ready", {});
        QTRY_VERIFY(!updates.isEmpty());
        QCOMPARE(runtime.snapshot().value("logs").toArray().size(), qsizetype{3});
        QCOMPARE(runtime.snapshot().value("droppedLogs").toString(), QStringLiteral("0"));
        QCOMPARE(runtime.snapshot().value("dataLifecycle").toObject().value("appLogCount").toInt(),
                 3);
        updates.clear();
        runtime.setDemands({});
        const auto released = runtime.snapshot();
        for (const auto* channel : {"connections", "rules", "providers", "logs"}) {
            QVERIFY(released.value(QLatin1String(channel)).toArray().isEmpty());
            QVERIFY(!released.value("dataLifecycle")
                         .toObject()
                         .value("publishedChannels")
                         .toArray()
                         .contains(QLatin1String(channel)));
        }
        QCOMPARE(released.value("dataLifecycle").toObject().value("providerDraftCount").toInt(), 0);
        QVERIFY(released.value("rulePage").toObject().isEmpty());
        QVERIFY(released.value("providerPage").toObject().isEmpty());
        QVERIFY(
            released.value("dataLifecycle").toObject().value("pendingReads").toArray().isEmpty());
        QTRY_VERIFY(!updates.isEmpty());
        const auto invalidated =
            qvariant_cast<quickclash::application::RuntimeUpdate>(updates.last()[0]);
        QVERIFY(invalidated.invalidated.contains("logs"));
        QVERIFY(invalidated.invalidated.contains("rule_status"));
        QVERIFY(invalidated.invalidated.contains("provider_status"));
        QVERIFY(!invalidated.values.contains("logs"));
        updates.clear();
        runtime.setDemands({"rules", "providers", "connections"});
        QTRY_VERIFY(!updates.isEmpty());
        const auto reacquired =
            qvariant_cast<quickclash::application::RuntimeUpdate>(updates.last()[0]);
        for (const auto* channel : {"connections", "rules", "providers"})
            QVERIFY(reacquired.values.contains(QLatin1String(channel)));
        QCOMPARE(settings.writes, 0);
    }
    void compactStateAndCoalescing() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        ProxySettings settings;
        quickclash::application::DesktopRuntime runtime(store, nullptr, &settings);
        QSignalSpy controls(&runtime, &quickclash::application::DesktopRuntime::changed);
        QSignalSpy updates(&runtime, &quickclash::application::DesktopRuntime::dataPublished);
        QSignalSpy operations(&runtime,
                              &quickclash::application::DesktopRuntime::operationCompleted);
        for (int i = 0; i < 100; ++i)
            runtime.setDemands({});
        QCOMPARE(controls.size(), 1);
        const auto compact = controls[0][0].toJsonObject();
        for (const auto* field : {"profiles", "groups", "connections", "rules", "providers", "logs",
                                  "delays", "coreUptimeSeconds"})
            QVERIFY(!compact.contains(QLatin1String(field)));
        QVERIFY(compact.contains("status"));
        QVERIFY(runtime.snapshot().contains("connections"));
        QTRY_COMPARE(updates.size(), 1);
        controls.clear();
        updates.clear();
        runtime.setDemands({});
        QTest::qWait(10);
        QCOMPARE(controls.size(), 0);
        QCOMPARE(updates.size(), 0);
        quickclash::infra::LatestMailbox mailbox;
        connect(&runtime, &quickclash::application::DesktopRuntime::dataPublished, &mailbox,
                mailbox.publisher(), Qt::DirectConnection);
        for (int i = 0; i < 4; ++i)
            QVERIFY(!runtime.stopProxy(quickclash::domain::StopReason::UserStop).isNull());
        QCOMPARE(operations.size(), 4);
        QCOMPARE(settings.writes, 0);
    }
};
QTEST_GUILESS_MAIN(RuntimePublicationTest)
#include "runtime_publication_test.moc"
