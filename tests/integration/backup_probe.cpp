#include "backup_probe.h"
#include "application/desktop_runtime.h"
#include "network_fixture.h"
#ifdef Q_OS_WIN
#include "platform/process_memory.h"
#endif
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer time;
    time.start();
    while (!condition() && time.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
QString digest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
int port() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
} // namespace
int backupProbe() {
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!std::holds_alternative<std::monostate>(store.open()))
        return 1;
    application::DesktopRuntime runtime(store);
    runtime.initialize();
    auto mixed = port(), controller = port();
    while (mixed == controller)
        controller = port();
    runtime.setPorts(mixed, controller);
    const auto check = [](bool result, const char* message) {
        if (!result)
            std::fprintf(stderr, "Backup assertion: %s\n", message);
        return result;
    };
    const auto done = [&] {
        return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
    };
    const auto profiles = [&] { return runtime.snapshot().value("profiles").toArray(); };
    const auto features = [&] { return runtime.snapshot().value("features").toObject(); };
    const auto token = [&] {
        return features().value("backupPreview").toObject().value("token").toString();
    };
    const auto files = [&] { return std::get<QStringList>(store.files("data/profiles")); };
    TlsFixture origin;
    if (!origin.initialize())
        return 2;
    origin.payloads.insert("/nodes", "proxies: [{name: node, type: http, server: 127.0.0.1, port: "
                                     "9, password: public_password}]\n");
    origin.subscription =
        "# original comment\nproxy-providers:\n  managed:\n    type: http\n    url: "
        "https://127.0.0.1:" +
        QByteArray::number(origin.serverPort()) +
        "/nodes?token=public_resource_token\n    interval: 3600\nproxy-groups:\n- {name: manual, "
        "type: select, use: [managed]}\nrules:\n- MATCH,manual\n";
    // The fixture dispatches exact request paths, including query strings.
    origin.payloads.insert("/nodes?token=public_resource_token", origin.payloads.value("/nodes"));
    runtime.addSubscription(
        QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort()),
        "Public backup fixture");
    if (!check(done() && profiles().size() == 1, "provider source imported"))
        return 3;
    const auto id = profiles().first().toObject().value("id").toString();
    runtime.configureSubscription(id, 0, false);
    runtime.setDemands({"editor"});
    QJsonObject editor;
    QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                     [&editor](const application::RuntimeUpdate& update) {
                         if (update.values.contains("editor"))
                             editor = update.values.value("editor").toObject();
                     });
    runtime.featureCommand("editor_open", {id, "overlay"});
    if (!waitFor([&] { return editor.contains("token"); }))
        return 4;
    const auto editToken = editor.value("token").toString();
    const auto overlay = QStringLiteral(
        "{\"rules\":{\"mode\":\"prepend\",\"items\":[\"DOMAIN,backup.invalid,DIRECT\"]}}");
    runtime.featureCommand("editor_preview", {editToken, overlay});
    if (!check(done() && waitFor([&] { return editor.contains("previewDigest"); }),
               "override preview"))
        return 5;
    runtime.featureCommand("editor_apply", {editToken, overlay, "", "confirmed"});
    if (!check(done(), "override applied"))
        return 6;
    runtime.setDemands({});
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(), "live session ready"))
        return 7;
    const auto pid = runtime.snapshot().value("corePid");
    const auto selection = runtime.snapshot().value("selectionGeneration");
    const auto beforeFiles = files();
    runtime.featureCommand("backup", {"confirmed", "source"});
    const auto archiveUrl = features().value("backupUrl").toString();
    QFile archive(QUrl(archiveUrl).toLocalFile());
    if (!archive.open(QIODevice::ReadOnly))
        return 8;
    const auto originalArchive = QJsonDocument::fromJson(archive.readAll()).object();
    archive.close();
    const auto entry = originalArchive.value("profiles").toArray().first().toObject();
    if (!check(originalArchive.value("schemaVersion") == 2 && entry.contains("snapshotYaml") &&
                   !entry.value("overlay").toObject().isEmpty() &&
                   QByteArray::fromBase64(entry.value("yaml").toString().toLatin1()) ==
                       origin.subscription &&
                   !entry.contains("secretRef"),
               "source preserves exact comments, snapshot and overrides"))
        return 9;
    runtime.featureCommand("preview_backup", {archiveUrl});
    if (!check(!token().isEmpty() && profiles().size() == 1 &&
                   !QJsonDocument(features().value("backupPreview").toObject())
                        .toJson()
                        .contains("public_password"),
               "preview changes no catalog and contains no raw credentials"))
        return 10;
    const auto oldToken = token();
    runtime.featureCommand("cancel_backup_restore", {});
    if (!check(files() == beforeFiles && token().isEmpty(), "cancel removes preview staging"))
        return 11;
    runtime.featureCommand("restore_backup", {oldToken, "confirmed"});
    if (!check(runtime.snapshot().value("error") == "BackupPreviewRequired",
               "cancelled token rejected"))
        return 12;
    runtime.featureCommand("preview_backup", {archiveUrl});
    const auto previewToken = token();
    // Confirm uses the staged bytes; changing the selected file cannot inject a later payload.
    if (!archive.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 13;
    archive.write("{}");
    archive.close();
    runtime.featureCommand("restore_backup", {previewToken, "confirmed"});
    if (!check(done() && profiles().size() == 2 && runtime.snapshot().value("corePid") == pid &&
                   runtime.snapshot().value("selectionGeneration") == selection &&
                   runtime.snapshot().value("ready").toBool(),
               "reviewed snapshot restores atomically without touching session"))
        return 14;
    auto catalog = *std::get<std::optional<QJsonObject>>(store.read("data/catalog.json"));
    const auto restored = catalog.value("profiles").toArray().last().toObject();
    if (!check(restored.value("overlay") == entry.value("overlay") &&
                   restored.value("secretRef").toString().isEmpty() &&
                   !restored.contains("updateIntervalMinutes") && restored.contains("snapshotRef"),
               "restored profile has no subscription or old schedule"))
        return 15;
    runtime.featureCommand("backup", {"confirmed", "offline"});
    const auto offlineUrl = features().value("backupUrl").toString();
    QFile offline(QUrl(offlineUrl).toLocalFile());
    if (!offline.open(QIODevice::ReadOnly))
        return 16;
    auto offlineArchive = QJsonDocument::fromJson(offline.readAll()).object();
    offline.close();
    const auto offlineEntry = offlineArchive.value("profiles").toArray().first().toObject();
    const auto offlineBytes =
        QByteArray::fromBase64(offlineEntry.value("yaml").toString().toLatin1());
    if (!check(offlineBytes.contains("backup.invalid") &&
                   !offlineBytes.contains("public_resource_token") &&
                   !offlineEntry.contains("overlay"),
               "offline backup includes effective overrides but excludes provider URLs"))
        return 17;
    const auto writeFixture = [&](QJsonObject value) {
        const auto path = directory.filePath("malformed.json");
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QString{};
        output.write(QJsonDocument(value).toJson(QJsonDocument::Compact));
        return QUrl::fromLocalFile(path).toString();
    };
    const auto stableFiles = files();
    // Schema preflight accepts this rule's shape, but official Mihomo rejects its CIDR.
    auto rejected = offlineArchive;
    auto rejectedRows = rejected.value("profiles").toArray();
    auto rejectedEntry = rejectedRows.last().toObject();
    const QByteArray rejectedYaml(
        "proxies: []\nproxy-groups: []\nrules: ['IP-CIDR,not_a_subnet,DIRECT']\n");
    rejectedEntry.insert("yaml", QString::fromLatin1(rejectedYaml.toBase64()));
    rejectedEntry.insert("sha256", digest(rejectedYaml));
    rejectedRows[rejectedRows.size() - 1] = rejectedEntry;
    rejected.insert("profiles", rejectedRows);
    runtime.featureCommand("preview_backup", {writeFixture(rejected)});
    if (!check(!token().isEmpty(), "semantic rejection fixture reaches official validation"))
        return 24;
    runtime.featureCommand("restore_backup", {token(), "confirmed"});
    if (!check(done() && runtime.snapshot().value("error") == "BackupValidationFailed" &&
                   profiles().size() == 2 && files() == stableFiles &&
                   runtime.snapshot().value("corePid") == pid,
               "later official validation failure rolls back the entire import"))
        return 25;
    runtime.featureCommand("preview_backup", {offlineUrl});
    const auto staleToken = token();
    runtime.renameProfile(id, "Renamed backup fixture");
    runtime.featureCommand("restore_backup", {staleToken, "confirmed"});
    if (!check(runtime.snapshot().value("error") == "BackupPreviewRequired" &&
                   profiles().size() == 2,
               "catalog changes invalidate the reviewed preview"))
        return 26;
    runtime.featureCommand("cancel_backup_restore", {});
#ifdef Q_OS_WIN
    if (QCoreApplication::arguments().contains("--backup-memory")) {
        auto measured = offlineArchive;
        auto measuredRows = measured.value("profiles").toArray();
        auto measuredEntry = measuredRows.first().toObject();
        const auto measuredYaml =
            QByteArray("#") + QByteArray(512 * 1024, 'x') + '\n' + offlineBytes;
        measuredEntry.insert("yaml", QString::fromLatin1(measuredYaml.toBase64()));
        measuredEntry.insert("sha256", digest(measuredYaml));
        measuredRows[0] = measuredEntry;
        measured.insert("profiles", measuredRows);
        const auto measuredUrl = writeFixture(measured);
        const auto baseline = platform::currentProcessMemory();
        auto peak = baseline;
        QElapsedTimer duration;
        duration.start();
        int iterations = 0;
        while (duration.elapsed() < 10000) {
            runtime.featureCommand("preview_backup", {measuredUrl});
            if (token().isEmpty())
                return 27;
            const auto sample = platform::currentProcessMemory();
            for (const auto* key : {"privateWorkingSet", "workingSet", "privateCommit"})
                if (sample.value(key).toString().toULongLong() >
                    peak.value(key).toString().toULongLong())
                    peak.insert(key, sample.value(key));
            runtime.featureCommand("cancel_backup_restore", {});
            if (files() != stableFiles)
                return 28;
            QCoreApplication::processEvents();
            QThread::msleep(50);
            ++iterations;
        }
        const QJsonObject report{{"durationMs", QString::number(duration.elapsed())},
                                 {"iterations", iterations},
                                 {"baseline", baseline},
                                 {"peak", peak},
                                 {"released", platform::currentProcessMemory()},
                                 {"core", platform::processMemory(platform::processIdentity(
                                              static_cast<quint32>(pid.toInt())))}};
        std::puts(QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
    }
#endif
    auto malformed = offlineArchive;
    auto rows = malformed.value("profiles").toArray();
    auto bad = rows.last().toObject();
    bad.insert("sha256", "bad");
    rows[rows.size() - 1] = bad;
    malformed.insert("profiles", rows);
    runtime.featureCommand("preview_backup", {writeFixture(malformed)});
    if (!check(runtime.snapshot().value("error") == "InvalidBackup" && files() == stableFiles &&
                   profiles().size() == 2,
               "late bad digest cleans earlier files and preserves catalog"))
        return 18;
    rows = offlineArchive.value("profiles").toArray();
    bad = rows.first().toObject();
    bad.insert("sourceRef", "../../escape.yaml");
    rows[0] = bad;
    malformed = offlineArchive;
    malformed.insert("profiles", rows);
    runtime.featureCommand("preview_backup", {writeFixture(malformed)});
    if (!check(runtime.snapshot().value("error") == "InvalidBackup" && files() == stableFiles,
               "path fields rejected"))
        return 19;
    runtime.featureCommand("preview_backup", {offlineUrl});
    const auto cancelToken = token();
    runtime.featureCommand("restore_backup", {cancelToken, "confirmed"});
    runtime.featureCommand("cancel_backup_restore", {});
    QCoreApplication::processEvents();
    if (!check(!runtime.snapshot().value("busy").toBool() && files() == stableFiles &&
                   profiles().size() == 2 && runtime.snapshot().value("corePid") == pid,
               "validation cancellation cannot commit or stop live core"))
        return 20;
    auto legacy = offlineArchive;
    legacy.insert("schemaVersion", 1);
    legacy.remove("scope");
    runtime.featureCommand("preview_backup", {writeFixture(legacy)});
    if (!check(!token().isEmpty(), "schema1 remains readable"))
        return 21;
    runtime.featureCommand("cancel_backup_restore", {});
    runtime.stop();
    QTemporaryDir emptyDirectory;
    infra::JsonStore emptyStore(emptyDirectory.path());
    if (!std::holds_alternative<std::monostate>(emptyStore.open()))
        return 22;
    application::DesktopRuntime empty(emptyStore);
    empty.initialize();
    empty.featureCommand("preview_backup", {offlineUrl});
    const auto emptyToken = empty.snapshot()
                                .value("features")
                                .toObject()
                                .value("backupPreview")
                                .toObject()
                                .value("token")
                                .toString();
    empty.featureCommand("restore_backup", {emptyToken, "confirmed"});
    if (!check(waitFor([&] { return !empty.snapshot().value("busy").toBool(); }) &&
                   empty.snapshot().value("profiles").toArray().size() == 2 &&
                   !empty.snapshot().value("ready").toBool() &&
                   empty.snapshot().value("selectedId").toString().isEmpty() &&
                   !empty.snapshot().value("tunDesired").toBool(),
               "empty repository restore never selects or starts a session"))
        return 23;
    std::puts("Backup source/offline, reviewed snapshot, atomic restore, corruption/path "
              "rejection, cancellation and empty repository checks passed");
    return 0;
}
} // namespace quickclash::test
