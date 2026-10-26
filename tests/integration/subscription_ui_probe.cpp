#include "subscription_ui_probe.h"
#include "application_host.h"
#include <QJSValue>
#include <QPointer>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>
#include <cstdio>

namespace quickclash {
namespace {
QList<QQuickItem*> cards(ApplicationHost& host) {
    return host.window()->findChildren<QQuickItem*>(QStringLiteral("subscription_card"));
}
bool click(ApplicationHost& host, QQuickItem* item) {
    if (!item || !item->isVisible() || !item->isEnabled())
        return false;
    QTest::mouseClick(host.window(), Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    QTest::qWait(20);
    return true;
}
} // namespace
int runSubscriptionUiProbe(ApplicationHost& host) {
    // Synthetic delivery only: no SettingsService, subscription, core or platform command
    // execution.
    auto& ui = host.state();
    ui.applyControl({{"publicationGeneration", "1"}, {"epoch", "fixture"}, {"selectedId", "p0"}});
    QJsonArray profiles;
    for (int i = 0; i < 1000; ++i)
        profiles.append(
            QJsonObject{{"id", QStringLiteral("p%1").arg(i)},
                        {"name", QString(80, QLatin1Char('W'))},
                        {"secretRef", "opaque"},
                        {"pendingApply", true},
                        {"usage", QJsonObject{{"total", 1000000}, {"download", 1234}}}});
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    if (!ui.navigate("subscriptions"))
        return 80;
    host.resizeWindow({1500, 850});
    QTest::qWait(80);
    auto* page = host.window()->findChild<QObject*>(QStringLiteral("subscriptions_page"));
    if (!page || page->property("cardColumns").toInt() != 3)
        return 81;
    QMetaObject::invokeMethod(page, "scrollToRow", Q_ARG(QVariant, 0));
    QTest::qWait(50);
    auto initial = cards(host);
    if (initial.isEmpty() || initial.size() > 96 || initial.size() >= profiles.size())
        return 82;
    QPointer<QQuickItem> first = initial.first();
    QList<QPair<QPointer<QQuickItem>, QString>> originalCards;
    for (auto* card : initial)
        originalCards.append({card, card->property("profileId").toString()});
    const auto managedId = first->property("profileId").toString();
    QSignalSpy action(first, SIGNAL(actionRequested(QString, QString)));
    QSignalSpy manage(first, SIGNAL(manageRequested(QString)));
    auto* title = first->findChild<QQuickItem*>(QStringLiteral("select_subscription"));
    if (!title)
        return 83;
    title->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(host.window(), Qt::Key_Space);
    if (action.size() != 1 || action[0][0].toString() != "select" || action[0][1] != managedId)
        return 84;
    QTest::keyClick(host.window(), Qt::Key_Down);
    QTest::qWait(40);
    if (page->property("currentIndex").toInt() < 1)
        return 85;
    QMetaObject::invokeMethod(page, "scrollToRow", Q_ARG(QVariant, 0));
    QTest::qWait(30);
    if (!click(host, first->findChild<QQuickItem*>(QStringLiteral("manage_subscription"))) ||
        manage.size() != 1 || action.size() != 1)
        return 86;
    QPointer<QObject> dialog =
        host.window()->findChild<QObject*>(QStringLiteral("subscription_management"));
    if (!dialog || !dialog->property("visible").toBool() ||
        page->property("managedId") != managedId)
        return 87;
    for (int row : {80, 160, 250, 320}) {
        QMetaObject::invokeMethod(page, "scrollToRow", Q_ARG(QVariant, row));
        QTest::qWait(40);
        if (cards(host).size() > 96 || !dialog || !dialog->property("visible").toBool())
            return 88;
    }
    bool reused = false;
    for (const auto& [card, id] : originalCards)
        if (card && card->property("profileId").toString() != id)
            reused = true;
    if (!reused)
        return 89;
    int managedIndex = -1;
    for (int i = 0; i < profiles.size(); ++i)
        if (profiles[i].toObject().value("id").toString() == managedId)
            managedIndex = i;
    if (managedIndex < 0)
        return 90;
    auto changed = profiles[managedIndex].toObject();
    changed.insert("name", "Updated while offscreen");
    profiles[managedIndex] = changed;
    QSignalSpy reset(ui.profileModel(), &QAbstractItemModel::modelReset);
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    auto managedValue = page->property("managedProfile");
    if (managedValue.metaType() == QMetaType::fromType<QJSValue>())
        managedValue = managedValue.value<QJSValue>().toVariant();
    const auto managed = managedValue.toMap();
    if (managed.value("name") != "Updated while offscreen" || reset.size())
        return 91;
    profiles.removeAt(managedIndex);
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    // Popup exit transitions may finish after close() has returned.
    for (int wait = 0; wait < 50 && dialog && dialog->property("visible").toBool(); ++wait)
        QTest::qWait(10);
    if (!dialog || dialog->property("visible").toBool() ||
        !page->property("managedId").toString().isEmpty())
        return 92;
    bool updateClicked = false;
    for (auto* card : cards(host)) {
        auto* update = card->findChild<QQuickItem*>(QStringLiteral("update_subscription"));
        if (!update)
            continue;
        const auto center = update->mapToScene(QPointF(update->width() / 2, update->height() / 2));
        if (!card->isVisible() || center.y() < 0 || center.y() >= host.window()->height())
            continue;
        QSignalSpy requested(card, SIGNAL(actionRequested(QString, QString)));
        const auto id = card->property("profileId").toString();
        if (!click(host, update) || requested.size() != 1 || requested[0][0] != "update" ||
            requested[0][1] != id)
            return 96;
        updateClicked = true;
        break;
    }
    if (!updateClicked)
        return 97;
    for (const auto& size : {QSize(900, 700), QSize(430, 640), QSize(1500, 850)}) {
        host.resizeWindow(size);
        QTest::qWait(60);
        const auto width = page->property("availableWidth").toReal();
        const int expected = width >= 900 ? 3 : width >= 580 ? 2 : 1;
        if (page->property("cardColumns").toInt() != expected ||
            ui.profileRows()->columns() != expected)
            return 93;
        QMetaObject::invokeMethod(page, "scrollToRow", Q_ARG(QVariant, 0));
        QTest::qWait(40);
        for (auto* card : cards(host)) {
            if (!card->isVisible())
                continue;
            if (card->height() < card->implicitHeight() || card->width() > width ||
                card->height() <= 0)
                return 94;
        }
    }
    if (host.qmlErrorCount() || !host.contentReady())
        return 95;
    std::puts("Subscription UI: bounded/reused delegates, stable actions, persistent management, "
              "incremental metadata, responsive long text and keyboard passed");
    return 0;
}
} // namespace quickclash
