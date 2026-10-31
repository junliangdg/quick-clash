#include "infra/storage/config_repository.h"
#include "infra/storage/runtime_provider_cache.h"
#include "infra/storage/settings_repository.h"
#include "infra/storage/storage_maintenance.h"
#include "platform/desktop_services.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
template <class T> bool good(const T& result) {
    if (const auto* error = std::get_if<quickclash::domain::Error>(&result)) {
        std::cerr << error->code.toStdString() << '\n';
        return false;
    }
    return true;
}
void rawWrite(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "raw fixture open");
    check(file.write(bytes) == bytes.size(), "raw fixture write");
}
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    using namespace quickclash;
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary root");
#ifdef Q_OS_WIN
        {
            QTemporaryDir recoveryRoot;
            infra::JsonStore store(recoveryRoot.path());
            check(good(store.open()) && good(store.writeBytes("config/settings.json", "{broken")) &&
                      good(store.writeBytes("cache/downloads/source", "retain")),
                  "failed move fixture");
            const auto path = recoveryRoot.filePath("config/settings.json");
            const HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                                              FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            check(locked != INVALID_HANDLE_VALUE, "hold fixture without delete sharing");
            store.freeze();
            const auto inventory = infra::inspectStorage(store, true);
            if (!good(inventory)) {
                CloseHandle(locked);
                check(false, "locked inventory");
            }
            const auto archive =
                store.archiveForRecovery(std::get<infra::StorageInventory>(inventory).fingerprint);
            CloseHandle(locked);
            check(std::holds_alternative<domain::Error>(archive) &&
                      std::get<domain::Error>(archive).code == "RecoveryArchiveFailed",
                  "move failure rolls back");
            check(std::get<QByteArray>(store.readBytes("config/settings.json")) == "{broken" &&
                      std::get<QByteArray>(store.readBytes("cache/downloads/source")) == "retain" &&
                      !QFileInfo::exists(recoveryRoot.filePath("recovery_pending.json")),
                  "rollback preserves every original and clears pending marker");
        }
#endif
        {
            QTemporaryDir protectedRoot;
            infra::JsonStore store(protectedRoot.path());
            check(good(store.open()) && good(store.writeBytes("backups/large.bin", "protected")),
                  "protected excess fixture");
            QFile large(protectedRoot.filePath("backups/large.bin"));
            check(large.open(QIODevice::ReadWrite) && large.resize(infra::kStorageBudget + 1),
                  "sparse protected file");
            large.close();
            const auto inventory = infra::inspectStorage(store);
            const auto trimmed =
                infra::trimStorage(store, std::get<infra::StorageInventory>(inventory));
            check(good(trimmed) &&
                      !std::get<QJsonObject>(trimmed).value("budgetSatisfied").toBool() &&
                      std::get<QJsonObject>(trimmed).value("removedFiles").toInt() == 0,
                  "soft total budget never sacrifices protected data");
            const auto unchanged = infra::inspectStorage(store);
            check(std::get<infra::StorageInventory>(unchanged).total == infra::kStorageBudget + 1,
                  "protected file stays intact");
        }
        {
            QTemporaryDir quotaRoot;
            infra::JsonStore store(quotaRoot.path());
            check(good(store.open()), "quota store");
            const QByteArray chunk(16 * 1024 * 1024, 'x');
            for (int i = 0; i < 5; ++i)
                check(
                    good(store.writeBytes(QStringLiteral("cache/downloads/%1.bin").arg(i), chunk)),
                    "quota cache");
            for (const auto* ref : {"data/profiles/active.yaml", "data/runtime/1/config.yaml",
                                    "data/transactions/pending.json", "data/drafts/draft.json",
                                    "data/core_state/providers/current.yaml",
                                    "backups/sensitive.json", "recovery/old/source.yaml"})
                check(good(store.writeBytes(ref, "protected")), "protected data fixture");
            check(good(store.writeBytes("logs/support_old.json", chunk)), "old log fixture");
            const auto before = infra::inspectStorage(store);
            check(good(before), "quota inspect");
            const auto trimmed =
                infra::trimStorage(store, std::get<infra::StorageInventory>(before));
            check(good(trimmed) && std::get<QJsonObject>(trimmed).value("budgetSatisfied").toBool(),
                  "quota trimming result");
            for (const auto* ref : {"data/profiles/active.yaml", "data/runtime/1/config.yaml",
                                    "data/transactions/pending.json", "data/drafts/draft.json",
                                    "data/core_state/providers/current.yaml",
                                    "backups/sensitive.json", "recovery/old/source.yaml"})
                check(std::get<QByteArray>(store.readBytes(ref)) == "protected",
                      "quota preserves protected namespaces");
            const auto after = infra::inspectStorage(store);
            check(good(after) &&
                      std::get<infra::StorageInventory>(after).cache <= infra::kCacheBudget &&
                      std::get<infra::StorageInventory>(after).logs <= infra::kLogBudget,
                  "cache and log budgets");
            check(good(store.write("config/settings.json", {})), "repair fixture");
            store.freeze();
            const auto first = infra::inspectStorage(store, true);
            check(good(first), "recovery inventory");
            rawWrite(quotaRoot.filePath("config/settings.json"), "{broken");
            check(std::holds_alternative<domain::Error>(store.archiveForRecovery(
                      std::get<infra::StorageInventory>(first).fingerprint)),
                  "stale archive refused");
            check(std::get<QByteArray>(store.readBytes("config/settings.json")) == "{broken",
                  "failed repair keeps original");
            const auto latest = infra::inspectStorage(store, true);
            const auto archive =
                store.archiveForRecovery(std::get<infra::StorageInventory>(latest).fingerprint);
            check(good(archive) && store.readOnly(), "archive leaves current process frozen");
            check(std::get<QByteArray>(store.readBytes(std::get<QString>(archive) +
                                                       "/config/settings.json")) == "{broken",
                  "archive preserves damaged bytes");
            check(std::holds_alternative<domain::Error>(store.writeBytes("data/new", "forbidden")),
                  "no writes before restart");
        }
        {
            QTemporaryDir interrupted;
            {
                infra::JsonStore store(interrupted.path());
                check(good(store.open()) &&
                          good(store.writeBytes("recovery_pending.json", "{broken")),
                      "interrupted recovery marker");
            }
            infra::JsonStore reopened(interrupted.path());
            check(good(reopened.open()) && reopened.readOnly(),
                  "incomplete archive restarts frozen");
        }
        {
            QTemporaryDir cacheRoot;
            infra::JsonStore cache(cacheRoot.path());
            check(good(cache.open()), "provider cache store opened");
            const auto inactive = "data/core_state/providers/" + QString(64, 'a') + ".yaml";
            check(good(cache.writeBytes(inactive, "old")), "inactive provider cache fixture");
            check(good(infra::prepareRuntimeProviderCache("proxies: []\n", cache,
                                                          "data/core_state")) &&
                      !QFile::exists(cacheRoot.filePath(inactive)),
                  "stopped core collection removes inactive generated cache");
            const auto unknown = "data/core_state/providers/unmanaged.yaml";
            check(good(cache.writeBytes(unknown, "retain")), "unmanaged provider fixture");
            check(std::holds_alternative<domain::Error>(infra::prepareRuntimeProviderCache(
                      "proxies: []\n", cache, "data/core_state")) &&
                      std::get<QByteArray>(cache.readBytes(unknown)) == "retain",
                  "unknown cache entry rejected without deleting arbitrary files");
            check(good(cache.remove(unknown)), "remove owned fixture");
            const auto directoryEntry = cacheRoot.filePath(inactive);
            check(QDir().mkpath(directoryEntry), "directory masquerading as provider file");
            check(std::holds_alternative<domain::Error>(infra::prepareRuntimeProviderCache(
                      "proxies: []\n", cache, "data/core_state")),
                  "provider cache never traverses nested directories");
        }
        {
            infra::JsonStore store(temporary.path());
            check(good(store.open()), "open writer");
            infra::JsonStore other(temporary.path());
            check(std::holds_alternative<domain::Error>(other.open()), "single writer lock");
            for (const auto* path : {"../escape", "data/../../escape", "C:/escape", "data/CON.json",
                                     "data/escape:", "data/ trailing ", "data//file"}) {
                check(std::holds_alternative<domain::Error>(store.path(QString::fromLatin1(path))),
                      "unsafe references rejected");
            }
            infra::ConfigRepository repository(store);
            check(good(repository.load()), "load first run");
            check(!std::get<std::optional<domain::ConfigSelection>>(repository.selection()),
                  "first run has no selection");
            check(!std::get<domain::ConfigHistory>(repository.history()).activeRevision,
                  "first run has no active history");
            check(good(store.writeBytes("data/runtime/1/config.yaml", "source", true)),
                  "immutable artifact created");
            check(std::holds_alternative<domain::Error>(
                      store.writeBytes("data/runtime/1/config.yaml", "replacement", true)),
                  "immutable artifact not overwritten");
            domain::ConfigSelection selection{1, "data/preferences/1.json", "data/resources/1.json",
                                              1};
            check(good(repository.selectForNextStart(selection, 0)), "stopped selection commit");
            check(
                std::holds_alternative<domain::Error>(repository.selectForNextStart(selection, 0)),
                "stale selection cannot overwrite");
            check(!std::get<domain::ConfigHistory>(repository.history()).activeRevision,
                  "selecting is not running");
            auto target = repository.manifest();
            target.insert("activeRevision", "1");
            target.insert("lastGoodRevision", "1");
            check(good(repository.prepare(target, "Apply", true)), "prepare running transaction");
            check(good(repository.phase("CoreApplying")), "record external intent first");
        }
        {
            infra::JsonStore store(temporary.path());
            check(good(store.open()), "reopen after interrupted apply");
            infra::ConfigRepository repository(store);
            check(good(repository.load()) && repository.recoveryRequired(),
                  "pre-manifest interruption requires external recovery");
            check(!std::get<domain::ConfigHistory>(repository.history()).activeRevision,
                  "old authoritative manifest remains");
            check(std::holds_alternative<domain::Error>(
                      repository.prepare(repository.manifest(), "Select", false)),
                  "pending transaction blocks later mutation");
            check(good(repository.rollback()), "caller can conclude compensated operation");
            auto target = repository.manifest();
            target.insert("activeRevision", "1");
            target.insert("lastGoodRevision", "1");
            check(good(repository.prepare(target, "Apply", false)), "prepare confirmed version");
            const auto interruptedJournal = repository.pending();
            check(good(repository.commit()), "atomic manifest commit");
            // Simulate a crash after manifest replacement, before journal finalization.
            const auto reference = QStringLiteral("data/transactions/%1.json")
                                       .arg(interruptedJournal.value("operationId").toString());
            check(good(store.write(reference, interruptedJournal)),
                  "restore pre-final log fixture");
        }
        {
            infra::JsonStore store(temporary.path());
            check(good(store.open()), "reopen committed manifest");
            infra::ConfigRepository repository(store);
            check(good(repository.load()) && !repository.recoveryRequired(),
                  "manifest commit wins over incomplete journal");
            check(std::get<domain::ConfigHistory>(repository.history()).activeRevision == 1,
                  "confirmed version retained");
            infra::SettingsRepository settings(store);
            check(std::get<infra::UiPreferences>(settings.load()).theme == "system",
                  "safe UI default");
            check(!std::get<infra::UiPreferences>(settings.load()).unloadWhenHidden,
                  "low-memory unload remains off by default");
            check(good(settings.save({"dark", true, true})), "preferences committed");
            auto dashboard = std::get<infra::UiPreferences>(settings.load());
            check(!dashboard.dashboard.value("resources").toBool() &&
                      dashboard.dashboard.value("ip").toBool(),
                  "dashboard safe defaults");
            dashboard.dashboard.insert("hideAddresses", true);
            check(good(settings.save(dashboard)) && std::get<infra::UiPreferences>(settings.load())
                                                        .dashboard.value("hideAddresses")
                                                        .toBool(),
                  "dashboard display options survive reload");
            check(std::get<infra::UiPreferences>(settings.load()).theme == "dark",
                  "preferences reload");
            check(std::get<infra::UiPreferences>(settings.load()).unloadWhenHidden,
                  "existing true unload preference retained without schema migration");
            check(good(settings.save({"dark", false, true})), "disable unload preference");
            check(!std::get<infra::UiPreferences>(settings.load()).unloadWhenHidden,
                  "existing false unload preference retained");
            check(std::holds_alternative<domain::Error>(settings.save({"invalid", false, false})),
                  "invalid preferences retain saved values");
#ifdef Q_OS_WIN
            const auto credentials = platform::makeCredentialStore(store);
            const auto reference = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QByteArray secret("https://example.invalid/private?token=sample");
            check(good(credentials->write(reference, secret)), "current-user DPAPI write");
            const auto encrypted = store.readBytes("data/credentials/" + reference + ".blob");
            check(good(encrypted) && !std::get<QByteArray>(encrypted).contains(secret),
                  "metadata contains no credential plaintext");
            check(std::get<QByteArray>(credentials->read(reference)) == secret, "DPAPI read");
            check(std::holds_alternative<domain::Error>(credentials->write(reference, {})),
                  "failed credential replacement");
            check(std::get<QByteArray>(credentials->read(reference)) == secret,
                  "old credential retained");
            check(good(store.writeBytes("data/credentials/" + reference + ".blob", "corrupt")),
                  "corrupt encrypted fixture");
            check(std::holds_alternative<domain::Error>(credentials->read(reference)),
                  "decryption failure explicit");
#endif
            check(QDir().mkpath(temporary.filePath("config/blocked.json")), "blocked fixture");
            check(
                std::holds_alternative<domain::Error>(store.writeBytes("config/blocked.json", "x")),
                "failed commit is reported");
            check(std::get<infra::UiPreferences>(settings.load()).theme == "dark",
                  "old settings intact");
        }
        {
            infra::JsonStore store(temporary.path());
            check(good(store.open()), "open recovery test");
            rawWrite(temporary.filePath("config/settings.json"), "{\"schemaVersion\":99}");
            check(std::holds_alternative<domain::Error>(store.read("config/settings.json")) &&
                      store.readOnly(),
                  "future schema is read-only");
            check(std::holds_alternative<domain::Error>(store.write("config/settings.json", {})),
                  "unknown schema never reset to defaults");
        }
        {
            rawWrite(temporary.filePath("config/settings.json"), "{broken");
            infra::JsonStore store(temporary.path());
            check(good(store.open()), "open damaged metadata");
            check(std::holds_alternative<domain::Error>(store.read("config/settings.json")) &&
                      store.readOnly(),
                  "corrupt metadata is read-only");
        }
        std::cout
            << "PASS: single writer, paths, atomic storage, recovery windows, settings and DPAPI\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
