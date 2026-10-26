#include "application/desktop_runtime.h"
#include "infra/storage/latest_mailbox.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
        for (int i = 0; i < 1100; ++i)
            runtime.featureCommand("not_ready", {});
        QTRY_VERIFY(!updates.isEmpty());
        QCOMPARE(runtime.snapshot().value("logs").toArray().size(), qsizetype{1000});
        QCOMPARE(runtime.snapshot().value("dataLifecycle").toObject().value("appLogCount").toInt(),
                 100);
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
        QVERIFY(
            released.value("dataLifecycle").toObject().value("pendingReads").toArray().isEmpty());
        QTRY_VERIFY(!updates.isEmpty());
        const auto invalidated =
            qvariant_cast<quickclash::application::RuntimeUpdate>(updates.last()[0]);
        QVERIFY(invalidated.invalidated.contains("logs"));
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
