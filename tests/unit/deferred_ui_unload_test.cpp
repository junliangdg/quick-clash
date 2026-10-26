#include "deferred_ui_unload.h"
#include <QSignalSpy>
#include <QTest>

using namespace quickclash;
class DeferredUiUnloadTest final : public QObject {
    Q_OBJECT
  private slots:
    void delayRepeatAndSingleDelivery() {
        DeferredUiUnload policy(60);
        QSignalSpy due(&policy, &DeferredUiUnload::due);
        policy.update(true, 1);
        QTest::qWait(35);
        QVERIFY(due.isEmpty());
        policy.update(true, 1);
        QTRY_COMPARE_WITH_TIMEOUT(due.size(), 1, 80);
        QCOMPARE(due[0][0].toULongLong(), quint64(1));
        QVERIFY(!policy.pending());
        QTest::qWait(80);
        QCOMPARE(due.size(), 1);
    }
    void cancelDisableNoTrayAndRestore() {
        DeferredUiUnload policy(30);
        QSignalSpy due(&policy, &DeferredUiUnload::due);
        for (int reason = 0; reason < 4; ++reason) {
            policy.update(true, 1);
            policy.update(false, 1); // Visible, disabled, no recovery tray, or exit.
            QVERIFY(!policy.pending());
            QTest::qWait(50);
            QVERIFY(due.isEmpty());
        }
        policy.update(true, 1);
        policy.cancel();
        policy.update(true, 2);
        QTRY_COMPARE_WITH_TIMEOUT(due.size(), 1, 100);
        QCOMPARE(due[0][0].toULongLong(), quint64(2));
    }
    void newEngineReplacesOldLeaseAndDestructionCancels() {
        DeferredUiUnload policy(60);
        QSignalSpy due(&policy, &DeferredUiUnload::due);
        policy.update(true, 1);
        QTest::qWait(35);
        policy.update(true, 2);
        QTest::qWait(35);
        QVERIFY(due.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(due.size(), 1, 100);
        QCOMPARE(due[0][0].toULongLong(), quint64(2));
        int calls = 0;
        {
            DeferredUiUnload temporary(20);
            connect(&temporary, &DeferredUiUnload::due, this, [&calls] { ++calls; });
            temporary.update(true, 1);
        }
        QTest::qWait(50);
        QCOMPARE(calls, 0);
    }
};
QTEST_GUILESS_MAIN(DeferredUiUnloadTest)
#include "deferred_ui_unload_test.moc"
