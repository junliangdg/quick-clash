#include "infra/storage/config_repository.h"
#include "infra/storage/settings_repository.h"
#include "platform/windows/credential_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

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
            platform::CredentialStore credentials(store);
            const auto reference = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QByteArray secret("https://example.invalid/private?token=sample");
            check(good(credentials.write(reference, secret)), "current-user DPAPI write");
            const auto encrypted = store.readBytes("data/credentials/" + reference + ".blob");
            check(good(encrypted) && !std::get<QByteArray>(encrypted).contains(secret),
                  "metadata contains no credential plaintext");
            check(std::get<QByteArray>(credentials.read(reference)) == secret, "DPAPI read");
            check(std::holds_alternative<domain::Error>(credentials.write(reference, {})),
                  "failed credential replacement");
            check(std::get<QByteArray>(credentials.read(reference)) == secret,
                  "old credential retained");
            check(good(store.writeBytes("data/credentials/" + reference + ".blob", "corrupt")),
                  "corrupt encrypted fixture");
            check(std::holds_alternative<domain::Error>(credentials.read(reference)),
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
