#include "presentation/runtime_list_model.h"
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace quickclash::presentation;
namespace {
QJsonObject connection(const QString& id, const QString& host, double rate,
                       const QString& start = "2026-10-26T00:00:00Z") {
    return {{"id", id},
            {"epoch", "a"},
            {"metadata", QJsonObject{{"host", host}, {"process", "browser"}, {"network", "tcp"}}},
            {"chains", QJsonArray{"proxy"}},
            {"rule", "Domain"},
            {"start", start},
            {"downloadRate", rate},
            {"upload", 100}};
}
QString firstId(const RuntimeListModel& model) {
    return model.index(0, 0).data(RuntimeRows::Identifier).toString();
}
} // namespace
class RuntimeListModelTest final : public QObject {
    Q_OBJECT
  private slots:
    void releaseInvalidatesRowsAndDeferredSort() {
        RuntimeRows rows(RuntimeRows::Kind::Connections);
        RuntimeListModel view(&rows, true);
        auto a = connection("a", "alpha", 1);
        rows.apply({a});
        view.setQuery("alpha");
        view.setSortMode(1);
        const QPersistentModelIndex index(view.index(0, 0));
        a.insert("downloadRate", 10);
        rows.apply({a});
        view.release();
        QCOMPARE(view.count(), 0);
        QCOMPARE(rows.rowCount(), 0);
        QVERIFY(!index.isValid());
        QSignalSpy layout(&view, &QAbstractItemModel::layoutChanged);
        QTest::qWait(1100);
        QCOMPARE(layout.size(), 0);
        QCOMPARE(view.query(), QString("alpha"));
        rows.apply({a});
        QCOMPARE(view.captureIds(), QStringList{"a"});
    }
    void incrementalRolesAndIdentity() {
        RuntimeRows rows(RuntimeRows::Kind::Connections);
        RuntimeListModel view(&rows, true);
        QAbstractItemModelTester sourceTester(
            &rows, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester proxyTester(
            &view, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy reset(&rows, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&rows, &QAbstractItemModel::dataChanged);
        QSignalSpy inserted(&rows, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&rows, &QAbstractItemModel::rowsRemoved);
        auto a = connection("a", "alpha", 1);
        auto b = connection("b", "beta", 2);
        rows.apply({a, b});
        QCOMPARE(inserted.size(), 1);
        const QPersistentModelIndex stable(rows.index(1));
        rows.apply({a, b});
        QCOMPARE(changed.size(), 0);
        a.insert("downloadRate", 3);
        rows.apply({a, b});
        QCOMPARE(changed.size(), 1);
        const auto roles = qvariant_cast<QList<int>>(changed[0][2]);
        QVERIFY(roles.contains(RuntimeRows::DownloadRate));
        QVERIFY(roles.contains(RuntimeRows::Entry));
        QVERIFY(!roles.contains(RuntimeRows::Metadata));
        QVERIFY(!roles.contains(RuntimeRows::SearchText));
        rows.apply({b, connection("c", "gamma", 4)});
        QCOMPARE(removed.size(), 1);
        QCOMPARE(inserted.size(), 2);
        QVERIFY(stable.isValid());
        QCOMPARE(stable.data(RuntimeRows::Identifier).toString(), QString("b"));
        QCOMPARE(reset.size(), 0);
        QCOMPARE(view.count(), 2);
    }
    void filteringSortingAndCapture() {
        RuntimeRows rows(RuntimeRows::Kind::Connections);
        RuntimeListModel view(&rows, true);
        auto a = connection("a", "alpha", 1);
        auto b = connection("b", "beta", 5, "2026-10-26T01:00:00Z");
        QSignalSpy totalChanged(&view, &RuntimeListModel::totalCountChanged);
        rows.apply({a, b});
        QCOMPARE(view.totalCount(), 2);
        QCOMPARE(totalChanged.size(), 1);
        QCOMPARE(firstId(view), QString("b"));
        view.setQuery("ALPHA");
        QCOMPARE(view.count(), 1);
        QCOMPARE(view.totalCount(), 2);
        QCOMPARE(totalChanged.size(), 1);
        QCOMPARE(view.captureIds(), QStringList{"a"});
        QCOMPARE(view.connection("a", "b").value("id").toString(), QString("b"));
        QVERIFY(view.connection("old", "b").isEmpty());
        view.setQuery("browser");
        QCOMPARE(view.count(), 2);
        view.setQuery("proxy");
        QCOMPARE(view.count(), 2);
        view.setQuery("Domain");
        QCOMPARE(view.count(), 2);
        view.setQuery("");
        view.setSortMode(1);
        QCOMPARE(firstId(view), QString("b"));
        a.insert("downloadRate", 10);
        rows.apply({a, b});
        QCOMPARE(firstId(view), QString("b"));
        QTRY_COMPARE_WITH_TIMEOUT(firstId(view), QString("a"), 2000);
        const auto captured = view.captureIds();
        rows.apply({a, b, connection("c", "new", 20)});
        QCOMPARE(view.totalCount(), 3);
        QCOMPARE(totalChanged.size(), 2);
        QCOMPARE(captured.size(), 2);
        QVERIFY(!captured.contains("c"));
        QVERIFY(view.captureIds(2).isEmpty());
        view.setQuery("no-match");
        QCOMPARE(view.count(), 0);
        b.insert("metadata", QJsonObject{{"host", "no-match"}});
        rows.apply({a, b});
        QCOMPARE(view.count(), 1);
        QCOMPARE(firstId(view), QString("b"));
    }
    void selectionSurvivesThousandRowChanges() {
        RuntimeRows rows(RuntimeRows::Kind::Connections);
        RuntimeListModel view(&rows, true);
        QJsonArray values;
        for (int i = 0; i < 1000; ++i)
            values.append(connection(QString::number(i), QString("host%1").arg(i), i));
        rows.apply(values);
        QSignalSpy reset(&rows, &QAbstractItemModel::modelReset);
        QSignalSpy updated(&view, &RuntimeListModel::entriesChanged);
        view.setQuery("host999");
        auto selected = values[0].toObject();
        selected.insert("upload", 200);
        values[0] = selected;
        rows.apply(values);
        QCOMPARE(view.connection("a", "0").value("upload").toInt(), 200);
        QCOMPARE(view.count(), 1);
        QVERIFY(!updated.isEmpty());
        values.removeFirst();
        rows.apply(values);
        QVERIFY(view.connection("a", "0").isEmpty());
        QCOMPARE(reset.size(), 0);
    }
    void rulesProvidersLogs() {
        RuntimeRows rules(RuntimeRows::Kind::Rules);
        RuntimeListModel ruleView(&rules, false);
        QSignalSpy reset(&rules, &QAbstractItemModel::modelReset);
        const QJsonObject match{{"type", "MATCH"}, {"proxy", "direct"}};
        const QJsonObject domain{
            {"type", "DOMAIN"}, {"payload", "example.test"}, {"proxy", "proxy"}};
        rules.apply({domain, match});
        const QPersistentModelIndex stableDomain(rules.index(0));
        ruleView.setQuery("direct");
        QCOMPARE(ruleView.index(0, 0).data(RuntimeRows::SourceOrder).toInt(), 1);
        ruleView.setQuery("");
        rules.apply({match, domain});
        QCOMPARE(ruleView.index(0, 0).data(RuntimeRows::Type).toString(), QString("MATCH"));
        QVERIFY(stableDomain.isValid());
        QCOMPARE(stableDomain.data(RuntimeRows::Type).toString(), QString("DOMAIN"));
        const auto domainKey = stableDomain.data(RuntimeRows::Key);
        QSignalSpy ruleInserted(&rules, &QAbstractItemModel::rowsInserted);
        rules.apply(
            {match, QJsonObject{{"type", "DOMAIN"}, {"payload", "new.test"}}, domain, domain});
        QCOMPARE(ruleInserted.size(), 1);
        QCOMPARE(ruleView.count(), 4);
        QCOMPARE(stableDomain.data(RuntimeRows::Key), domainKey);
        QCOMPARE(ruleView.index(2, 0).data(RuntimeRows::Key), domainKey);
        QVERIFY(ruleView.index(3, 0).data(RuntimeRows::Key) != domainKey);
        rules.apply({domain});
        QVERIFY(stableDomain.isValid());
        QCOMPARE(ruleView.count(), 1);
        QCOMPARE(ruleView.index(0, 0).data(RuntimeRows::Key), domainKey);
        QCOMPARE(reset.size(), 0);
        RuntimeRows providers(RuntimeRows::Kind::Providers);
        providers.apply({QJsonObject{{"name", "same"}, {"kind", "rules"}, {"count", 1}},
                         QJsonObject{{"name", "same"}, {"kind", "proxies"}, {"count", 2}}});
        QCOMPARE(providers.rowCount(), 2);
        QVERIFY(providers.index(0).data(RuntimeRows::Key) !=
                providers.index(1).data(RuntimeRows::Key));
        QSignalSpy providerChange(&providers, &QAbstractItemModel::dataChanged);
        providers.apply({QJsonObject{{"name", "same"}, {"kind", "rules"}, {"count", 3}},
                         QJsonObject{{"name", "same"}, {"kind", "proxies"}, {"count", 2}}});
        QCOMPARE(providerChange.size(), 1);
        QVERIFY(qvariant_cast<QList<int>>(providerChange[0][2]).contains(RuntimeRows::Count));
        RuntimeListModel providerView(&providers, false);
        providerView.setQuery("Failed");
        QCOMPARE(providerView.count(), 0);
        providers.apply(
            {QJsonObject{
                 {"name", "same"}, {"kind", "rules"}, {"count", 3}, {"lastResult", "Failed"}},
             QJsonObject{{"name", "same"}, {"kind", "proxies"}, {"count", 2}}});
        QCOMPARE(providerView.count(), 1);
        QCOMPARE(providerView.index(0, 0).data(RuntimeRows::KindRole).toString(), QString("rules"));
        RuntimeRows logs(RuntimeRows::Kind::Logs);
        RuntimeListModel logView(&logs, false);
        const auto log = [](int sequence, const QString& source, const QString& level) {
            return QJsonObject{{"localSeq", QString::number(sequence)},
                               {"source", source},
                               {"level", level},
                               {"message", "same"}};
        };
        logs.apply({log(1, "Core", "info"), log(2, "App", "error")});
        logView.setSourceFilter("App");
        logView.setLevelFilter("error");
        QCOMPARE(logView.count(), 1);
        const auto stableKey = logView.index(0, 0).data(RuntimeRows::Key);
        logs.apply({log(2, "App", "error"), log(3, "Core", "info")});
        QCOMPARE(logView.index(0, 0).data(RuntimeRows::Key), stableKey);
        logs.clear();
        QCOMPARE(logView.count(), 0);
        logs.apply({log(4, "App", "error warn")});
        logView.setSourceFilter("App");
        logView.setLevelFilter("error warn");
        QCOMPARE(logView.count(), 1);
        // The joined search text is unchanged, but exact source/level matching must refresh.
        logs.apply({log(4, "App error", "warn")});
        QCOMPARE(logView.count(), 0);
        logs.apply({log(5, "Core", "warning"), log(6, "Service", "warning")});
        logView.setSourceFilter("Service");
        logView.setLevelFilter("warning");
        QCOMPARE(logView.count(), 1);
        QCOMPARE(logView.index(0, 0).data(RuntimeRows::Entry).toMap().value("localSeq").toString(),
                 QStringLiteral("6"));
    }
};
QTEST_GUILESS_MAIN(RuntimeListModelTest)
#include "runtime_list_model_test.moc"
