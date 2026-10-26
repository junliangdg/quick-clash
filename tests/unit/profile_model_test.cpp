#include "presentation/profile_model.h"
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace quickclash::presentation;
namespace {
QJsonObject profile(const QString& id, const QString& name = "Fixture") {
    return {{"id", id}, {"name", name}, {"usage", QJsonObject{{"total", 1000}}}};
}
} // namespace
class ProfileModelTest final : public QObject {
    Q_OBJECT
  private slots:
    void stableIdentityAndRoles() {
        ProfileModel model;
        ProfileRowModel rows(&model);
        rows.setColumns(3);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester rowTester(&rows,
                                           QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy rowReset(&rows, &QAbstractItemModel::modelReset);
        QVERIFY(model.apply({profile("a"), profile("b"), profile("c"), profile("d")}));
        const QPersistentModelIndex stable(model.index(1));
        const QPersistentModelIndex removed(model.index(2));
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy rowChanged(&rows, &QAbstractItemModel::dataChanged);
        auto b = profile("b");
        b.insert("usage", QJsonObject{{"total", 2000}});
        QVERIFY(model.apply({profile("a"), b, profile("c"), profile("d")}));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed[0][0].value<QModelIndex>().row(), 1);
        const auto roles = qvariant_cast<QList<int>>(changed[0][2]);
        QVERIFY(roles.contains(ProfileModel::Usage));
        QVERIFY(!roles.contains(ProfileModel::Name));
        QCOMPARE(rowChanged.size(), 1);
        QCOMPARE(qvariant_cast<QList<int>>(rowChanged[0][2]), QList<int>{ProfileRowModel::Second});
        QVERIFY(model.apply({profile("d"), b, profile("a"), profile("c")}));
        QVERIFY(stable.isValid());
        QCOMPARE(stable.data(ProfileModel::Identifier).toString(), QString("b"));
        QCOMPARE(model.profile("b").value("usage").toMap().value("total").toInt(), 2000);
        QVERIFY(model.apply({b}));
        QCOMPARE(stable.row(), 0);
        QVERIFY(!removed.isValid());
        QCOMPARE(reset.size(), 0);
        QCOMPARE(rowReset.size(), 0);
    }
    void largeGroupingAndInvalidInput() {
        ProfileModel model;
        ProfileRowModel rows(&model);
        QJsonArray values;
        for (int i = 0; i < 1000; ++i)
            values.append(profile(QString::number(i)));
        QVERIFY(model.apply(values));
        for (int columns = 1; columns <= 3; ++columns) {
            rows.setColumns(columns);
            QCOMPARE(rows.rowCount(), (1000 + columns - 1) / columns);
            QCOMPARE(rows.index(0).data(ProfileRowModel::First).toMap().value("id").toString(),
                     QString("0"));
        }
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy projected(&rows, &QAbstractItemModel::dataChanged);
        QVERIFY(model.apply(values));
        QCOMPARE(changed.size(), 0);
        QCOMPARE(projected.size(), 0);
        QVERIFY(!model.apply({profile("duplicate"), profile("duplicate")}));
        QVERIFY(!model.apply({QJsonObject{{"name", "No identity"}}}));
        QCOMPARE(model.count(), 1000);
        QVERIFY(model.apply({}));
        QCOMPARE(rows.rowCount(), 0);
        QVERIFY(model.profile("0").isEmpty());
    }
};
QTEST_GUILESS_MAIN(ProfileModelTest)
#include "profile_model_test.moc"
