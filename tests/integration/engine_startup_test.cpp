#include "infra/config/config_preflight.h"
#include "infra/core/engine_session.h"
#include "infra/core/listener_ports.h"
#include "platform/desktop_services.h"
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

namespace {
class DelayedProcess final : public quickclash::application::IDesktopProcess {
  public:
    DelayedProcess() : process(quickclash::platform::makeDesktopProcess()) {}
    quickclash::domain::CommandResult start(const QString& executable, const QStringList& arguments,
                                            const QString& directory, bool isolated) override {
        ++starts;
        return process->start(executable, arguments, directory, isolated);
    }
    void stop() override {
        process->stop();
    }
    bool running() const override {
        return process->running();
    }
    quint32 pid() const override {
        return process->pid();
    }
    QJsonObject memory() const override {
        return process->memory();
    }
    bool ownsListener(quint16 port) const override {
        ++checks;
        return checks > delayedChecks && process->ownsListener(port);
    }
    std::unique_ptr<IDesktopProcess> process;
    int delayedChecks = 4;
    int starts = 0;
    mutable int checks = 0;
};
quint16 freePort() {
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost) ? server.serverPort() : 0;
}
} // namespace
class EngineStartupTest final : public QObject {
    Q_OBJECT
  private slots:
    void unavailablePort_data() {
        QTest::addColumn<bool>("controller");
        QTest::newRow("mixed_port_occupied") << false;
        QTest::newRow("controller_port_occupied") << true;
    }
    void unavailablePort() {
        QFETCH(bool, controller);
        QTcpServer occupied;
        QVERIFY(occupied.listen(QHostAddress::LocalHost));
        const auto available = freePort();
        QVERIFY(available >= 1024);
        auto process = std::make_unique<DelayedProcess>();
        auto* observed = process.get();
        quickclash::infra::EngineSession engine(std::move(process), nullptr);
        QSignalSpy ready(&engine, &quickclash::infra::EngineSession::becameReady);
        QSignalSpy failed(&engine, &quickclash::infra::EngineSession::failed);
        engine.start(QStringLiteral(QUICKCLASH_SOURCE_DIR "/.cache/mihomo/mihomo.exe"), {}, {},
                     QByteArray(32, 'x'), controller ? available : occupied.serverPort(),
                     controller ? occupied.serverPort() : available, QStringLiteral("rule"));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("CoreListenerAddressInUse"));
        QCOMPARE(engine.listenerIssue().value("port").toInt(), occupied.serverPort());
        QCOMPARE(engine.listenerIssue().value("role").toString(),
                 controller ? "controller" : "mixed");
        const auto suggestion =
            quickclash::infra::suggestListenerPorts(occupied.serverPort(), available);
        QVERIFY(!suggestion.isEmpty());
        QVERIFY(suggestion.value("mixedPort").toInt() != occupied.serverPort());
        QVERIFY(suggestion.value("mixedPort") != suggestion.value("controllerPort"));
        QVERIFY(ready.isEmpty());
        QCOMPARE(observed->starts, 0);
        QVERIFY(!engine.running());
    }
    void startup_data() {
        QTest::addColumn<int>("delay");
        QTest::addColumn<bool>("succeeds");
        QTest::newRow("listeners_start_after_controller") << 4 << true;
        QTest::newRow("unowned_listeners_never_ready") << 100000 << false;
    }
    void startup() {
        QFETCH(int, delay);
        QFETCH(bool, succeeds);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        quickclash::domain::RuntimePreferences preferences;
        preferences.mixedPort = freePort();
        do {
            preferences.controllerPort = freePort();
        } while (preferences.controllerPort == preferences.mixedPort);
        QByteArray input = "proxies: []\nproxy-groups: []\nrules: ['MATCH,DIRECT']\n";
        const auto prepared = quickclash::infra::preflightConfig(input, preferences);
        QVERIFY(std::holds_alternative<quickclash::infra::PreflightConfig>(prepared));
        const QByteArray secret(32, 'x');
        const auto materialized = quickclash::infra::materializeSession(
            std::get<quickclash::infra::PreflightConfig>(prepared), secret);
        QVERIFY(std::holds_alternative<QByteArray>(materialized));
        const auto config = directory.filePath(QStringLiteral("config.yaml"));
        QFile file(config);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(std::get<QByteArray>(materialized)),
                 std::get<QByteArray>(materialized).size());
        file.close();
        auto process = std::make_unique<DelayedProcess>();
        auto* observed = process.get();
        process->delayedChecks = delay;
        quickclash::infra::EngineSession engine(std::move(process), nullptr);
        QSignalSpy ready(&engine, &quickclash::infra::EngineSession::becameReady);
        QSignalSpy failed(&engine, &quickclash::infra::EngineSession::failed);
        engine.start(QStringLiteral(QUICKCLASH_SOURCE_DIR "/.cache/mihomo/mihomo.exe"), config,
                     directory.path(), secret, preferences.mixedPort, preferences.controllerPort,
                     QStringLiteral("rule"));
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(), 12000);
        if (succeeds) {
            QVERIFY2(failed.isEmpty(),
                     qPrintable(failed.isEmpty() ? QString{} : failed.first().first().toString()));
            QVERIFY(engine.ready());
            QVERIFY(observed->checks > delay);
        } else {
            QVERIFY(ready.isEmpty());
            QCOMPARE(failed.first().first().toString(), QStringLiteral("CoreListenersNotOwned"));
            QVERIFY(!engine.running());
        }
        engine.stop();
        QVERIFY(!engine.ready());
        QVERIFY(!engine.running());
    }
};
QTEST_GUILESS_MAIN(EngineStartupTest)
#include "engine_startup_test.moc"
