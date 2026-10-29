#include "infra/core/service_client.h"
#include "infra/storage/json_store.h"
#include "platform/desktop_services.h"
#include "platform/process_memory.h"
#include <QDirIterator>
#include <QLocalServer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class UnsupportedPlatformTest final : public QObject {
    Q_OBJECT
  private slots:
    void rejectsMutationsWithoutSideEffects() {
        QTemporaryDir directory;
        quickclash::infra::JsonStore store(directory.path());
        QVERIFY(std::holds_alternative<std::monostate>(store.open()));
        const auto inventory = [&directory] {
            QStringList entries;
            QDirIterator iterator(directory.path(), QDir::AllEntries | QDir::NoDotAndDotDot,
                                  QDirIterator::Subdirectories);
            while (iterator.hasNext())
                entries.append(QDir(directory.path()).relativeFilePath(iterator.next()));
            entries.sort();
            return entries;
        };
        const auto files = inventory();
        using namespace quickclash;
        const auto info = platform::desktopPlatformInfo();
        QVERIFY(info.coreSha256.isEmpty());
        for (const auto& value : info.capabilities)
            QCOMPARE(value.toString(), QStringLiteral("Unsupported"));
        auto credentials = platform::makeCredentialStore(store);
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCOMPARE(std::get<domain::Error>(credentials->write(id, "secret")).code,
                 QStringLiteral("CredentialStoreUnsupported"));
        QVERIFY(std::holds_alternative<domain::Error>(credentials->read(id)));
        QVERIFY(std::holds_alternative<domain::Error>(credentials->remove(id)));
        auto process = platform::makeDesktopProcess();
        QSignalSpy exited(process.get(), &application::IDesktopProcess::finished);
        QCOMPARE(std::get<domain::Error>(
                     process->start(QCoreApplication::applicationFilePath(), {}, directory.path()))
                     .code,
                 QStringLiteral("CorePlatformUnsupported"));
        process->stop();
        QVERIFY(!process->running() && !process->pid() && !process->ownsListener(7890));
        QVERIFY(process->memory().isEmpty() && exited.isEmpty());
        auto lease = platform::makeSystemProxyLease(store);
        QVERIFY(std::holds_alternative<domain::Error>(lease->acquire(7890, "session")));
        QVERIFY(std::holds_alternative<domain::Error>(lease->reapply(7890, "session")));
        QVERIFY(std::holds_alternative<domain::Error>(lease->release()));
        QVERIFY(std::holds_alternative<domain::Error>(lease->suspend()));
        QVERIFY(std::holds_alternative<domain::Error>(lease->matches()));
        QVERIFY(std::holds_alternative<domain::Error>(lease->safeToArchive()));
        QVERIFY(!lease->active() && !lease->conflict());
        auto resources = platform::makeDesktopResources();
        QVERIFY(resources->host().isEmpty());
        QVERIFY(!resources->service().value("complete").toBool());
        QVERIFY(platform::currentProcessMemory().isEmpty());
        QVERIFY(!platform::installedServiceMatches(1));
        QCOMPARE(inventory(), files);
    }
    void serviceNeverConnectsAndCancelsWithOwner() {
        using namespace quickclash;
        // A live endpoint cannot turn Unsupported into a service handshake.
        QLocalServer server;
        const bool listening = server.listen(QStringLiteral("QuickClash.Service.v1"));
        infra::ServiceClient client;
        bool completed = false;
        client.command("hello", {}, [&completed](domain::Result<QJsonObject> result) {
            completed = true;
            QCOMPARE(std::get<domain::Error>(result).code,
                     QStringLiteral("ServicePlatformUnsupported"));
        });
        QVERIFY(!completed);
        QTRY_VERIFY(completed);
        QVERIFY(!client.connected());
        if (listening)
            QVERIFY(!server.hasPendingConnections());
        bool late = false;
        {
            infra::ServiceClient temporary;
            temporary.command("start", {}, [&late](domain::Result<QJsonObject>) { late = true; });
        }
        QCoreApplication::processEvents();
        QVERIFY(!late);
        client.command("start", {}, [&late](domain::Result<QJsonObject>) { late = true; });
        client.close();
        QCoreApplication::processEvents();
        QVERIFY(!late);
    }
};
QTEST_GUILESS_MAIN(UnsupportedPlatformTest)
#include "unsupported_platform_test.moc"
