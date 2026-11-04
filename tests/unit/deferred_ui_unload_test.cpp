#include "deferred_ui_unload.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

using namespace quickclash;
class DeferredUiUnloadTest final : public QObject {
    Q_OBJECT
  private slots:
    void queuedOnceAfterCurrentEvent() {
        DeferredUiUnload policy;
        QSignalSpy due(&policy, &DeferredUiUnload::due);
        policy.update(true, 1);
        policy.update(true, 1);
        QVERIFY(policy.pending());
        QVERIFY(due.isEmpty()); // Never destroy QML on the initiating event's stack.
        QTRY_COMPARE_WITH_TIMEOUT(due.size(), 1, 1000);
        QCOMPARE(due[0][0].toULongLong(), quint64(1));
        QVERIFY(!policy.pending());
        QCoreApplication::processEvents();
        QCOMPARE(due.size(), 1);
    }
    void cancelDisableNoTrayMinimizeAndRestore() {
        DeferredUiUnload policy;
        QSignalSpy due(&policy, &DeferredUiUnload::due);
        for (int reason = 0; reason < 5; ++reason) {
            policy.update(true, 1);
            policy.update(false, 1); // Visible, minimized, disabled, no tray, or exit.
            QVERIFY(!policy.pending());
            QCoreApplication::processEvents();
            QVERIFY(due.isEmpty());
        }
        policy.update(true, 1);
        policy.cancel();
        policy.update(true, 2);
        QTRY_COMPARE_WITH_TIMEOUT(due.size(), 1, 1000);
        QCOMPARE(due[0][0].toULongLong(), quint64(2));
    }
    void newEngineReplacesOldLeaseAndDestructionCancels() {
        DeferredUiUnload policy;
        QSignalSpy due(&policy, &DeferredUiUnload::due);
        policy.update(true, 1);
        policy.update(true, 2);
        QVERIFY(due.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(due.size(), 1, 1000);
        QCOMPARE(due[0][0].toULongLong(), quint64(2));
        int calls = 0;
        {
            DeferredUiUnload temporary;
            connect(&temporary, &DeferredUiUnload::due, this, [&calls] { ++calls; });
            temporary.update(true, 1);
        }
        QCoreApplication::processEvents();
        QCOMPARE(calls, 0);
    }
};
QTEST_GUILESS_MAIN(DeferredUiUnloadTest)
#include "deferred_ui_unload_test.moc"
