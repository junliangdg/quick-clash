#include "infra/storage/latest_mailbox.h"
#include <QJsonArray>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <memory>

using quickclash::application::RuntimeUpdate;
using quickclash::infra::LatestMailbox;

class MailboxSource final : public QObject {
    Q_OBJECT
  signals:
    void control(int sequence);
};
class LatestMailboxTest final : public QObject {
    Q_OBJECT
  private slots:
    void burstAndReliableControl() {
        LatestMailbox mailbox;
        quickclash::application::RuntimeDemands demands;
        demands.setActive({"rules"});
        mailbox.setDemands(demands);
        QSignalSpy deliveries(&mailbox, &LatestMailbox::delivered);
        MailboxSource source;
        QList<int> controls;
        connect(
            &source, &MailboxSource::control, this,
            [&controls](int value) { controls.append(value); }, Qt::QueuedConnection);
        const auto publish = mailbox.publisher();
        auto producer = std::unique_ptr<QThread>(QThread::create([&] {
            for (int i = 0; i < 200; ++i) {
                publish({1, "a", {{"samples", QJsonObject{{"value", i}}}}});
                emit source.control(i);
            }
            publish({1,
                     "a",
                     {{"rules", QJsonArray{QJsonObject{{"type", "MATCH"}}}}},
                     demands.generations});
        }));
        producer->start();
        QVERIFY(producer->wait(5000));
        QCOMPARE(deliveries.size(), 0);
        QTRY_COMPARE(deliveries.size(), 1);
        QTRY_COMPARE(controls.size(), 200);
        const auto update = qvariant_cast<RuntimeUpdate>(deliveries[0][0]);
        QCOMPARE(update.values.value("samples").toObject().value("value").toInt(), 199);
        QVERIFY(update.values.contains("rules"));
        for (int i = 0; i < controls.size(); ++i)
            QCOMPARE(controls[i], i);
        publish({1, "a", {{"samples", QJsonObject{{"value", 201}}}}});
        QTRY_COMPARE(deliveries.size(), 2);
        QVERIFY(!qvariant_cast<RuntimeUpdate>(deliveries[1][0]).values.contains("rules"));
    }
    void generationAndClose() {
        LatestMailbox mailbox;
        quickclash::application::RuntimeDemands demands;
        demands.setActive({"rules", "logs"});
        mailbox.setDemands(demands);
        QSignalSpy deliveries(&mailbox, &LatestMailbox::delivered);
        const auto publish = mailbox.publisher();
        publish({1, "old", {{"rules", QJsonArray{1}}}, demands.generations});
        publish({2, "new", {{"samples", QJsonObject{{"value", 2}}}}});
        publish({1, "old", {{"samples", QJsonObject{{"value", 99}}}}});
        publish({2, "wrong", {{"logs", QJsonArray{1}}}});
        QTRY_COMPARE(deliveries.size(), 1);
        const auto update = qvariant_cast<RuntimeUpdate>(deliveries[0][0]);
        QCOMPARE(update.generation, quint64{2});
        QCOMPARE(update.epoch, QString("new"));
        QCOMPARE(update.values.size(), 1);
        publish({2, "new", {{"logs", QJsonArray{1}}}, demands.generations});
        mailbox.close();
        QCoreApplication::processEvents();
        QCOMPARE(deliveries.size(), 1);
        publish({3, "later", {{"logs", QJsonArray{2}}}});
        QCoreApplication::processEvents();
        QCOMPARE(deliveries.size(), 1);
    }
    void perStreamRevocationAndLatePublisher() {
        LatestMailbox mailbox;
        QSignalSpy deliveries(&mailbox, &LatestMailbox::delivered);
        quickclash::application::RuntimeDemands demands;
        demands.setActive({"connections", "rules"});
        mailbox.setDemands(demands);
        const auto publish = mailbox.publisher();
        const RuntimeUpdate old{1,
                                "same",
                                {{"connections", QJsonArray{1}}, {"rules", QJsonArray{2}}},
                                demands.generations};
        publish(old);
        demands.setActive({"rules"});
        mailbox.setDemands(demands);
        publish(old);
        QTRY_COMPARE(deliveries.size(), 1);
        auto update = qvariant_cast<RuntimeUpdate>(deliveries[0][0]);
        QVERIFY(!update.values.contains("connections"));
        QVERIFY(update.values.contains("rules"));
        demands.setActive({"rules", "connections"});
        mailbox.setDemands(demands);
        publish(old);
        publish({1, "same", {{"connections", QJsonArray{3}}}, demands.generations});
        publish(old);
        QTRY_COMPARE(deliveries.size(), 2);
        update = qvariant_cast<RuntimeUpdate>(deliveries[1][0]);
        QCOMPARE(update.values.value("connections").toArray(), QJsonArray{3});
        demands.setActive({"rules"});
        mailbox.setDemands(demands);
        publish({1, "same", {}, demands.generations, {"connections"}});
        publish(old);
        QTRY_COMPARE(deliveries.size(), 3);
        update = qvariant_cast<RuntimeUpdate>(deliveries[2][0]);
        QVERIFY(update.invalidated.contains("connections"));
        QVERIFY(!update.values.contains("connections"));
        // With no surviving stream, revocation also erases a value already waiting for a wakeup.
        publish({1, "same", {{"rules", QJsonArray{4}}}, demands.generations});
        demands.setActive({});
        mailbox.setDemands(demands);
        QCoreApplication::processEvents();
        QCOMPARE(deliveries.size(), 3);
    }
    void publisherSurvivesReceiver() {
        auto mailbox = std::make_unique<LatestMailbox>();
        const auto publish = mailbox->publisher();
        QSemaphore started;
        auto producer = std::unique_ptr<QThread>(QThread::create([&] {
            started.release();
            for (int i = 0; i < 2000; ++i)
                publish({1, "a", {{"samples", QJsonObject{{"value", i}}}}});
        }));
        producer->start();
        QVERIFY(started.tryAcquire(1, 5000));
        mailbox.reset();
        QVERIFY(producer->wait(5000));
        publish({2, "b", {{"samples", QJsonObject{}}}});
        QCoreApplication::processEvents();
    }
};
QTEST_GUILESS_MAIN(LatestMailboxTest)
#include "latest_mailbox_test.moc"
