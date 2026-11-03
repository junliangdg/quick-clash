#include "subscription_ui_probe.h"
#include "application_host.h"
#include "domain/subscription_usage.h"
#include "focus_layout_checks.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QJSValue>
#include <QPointer>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <cstdio>

namespace quickclash {
namespace {
QList<QQuickItem*> cards(ApplicationHost& host) {
    QList<QQuickItem*> result;
    const auto visit = [&result](auto&& self, QQuickItem* item) -> void {
        if (item->objectName() == QStringLiteral("subscription_card"))
            result.append(item);
        for (auto* child : item->childItems())
            self(self, child);
    };
    visit(visit, host.window()->contentItem());
    return result;
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
    QString visualOutput;
    for (const auto& argument : QCoreApplication::arguments())
        if (argument.startsWith("--subscription-visual-output="))
            visualOutput = argument.mid(QStringLiteral("--subscription-visual-output=").size());
    if (!visualOutput.isEmpty()) {
        host.window()->hide();
        host.window()->show();
        if (!QDir().mkpath(visualOutput) || !QTest::qWaitForWindowExposed(host.window(), 5000))
            return 112;
    }
    ui.applyControl({{"publicationGeneration", "1"}, {"epoch", "fixture"}, {"selectedId", "p0"}});
    QJsonArray profiles;
    for (int i = 0; i < 1000; ++i)
        profiles.append(QJsonObject{{"id", QStringLiteral("p%1").arg(i)},
                                    {"name", QString(80, QLatin1Char('W'))},
                                    {"secretRef", "opaque"},
                                    {"pendingApply", true},
                                    {"usage", QJsonObject{{"total", 1000000}, {"download", 1234}}},
                                    {"usageStatus", domain::subscriptionUsageStatus(
                                                        {{"total", 1000000}, {"download", 1234}},
                                                        QDateTime::currentDateTimeUtc())}});
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    if (!ui.navigate("subscriptions"))
        return 80;
    host.resizeWindow({1500, 850});
    QTest::qWait(80);
    auto* page = host.window()->findChild<QObject*>(QStringLiteral("subscriptions_page"));
    if (!page || page->property("cardColumns").toInt() != 3)
        return 81;
    // Exercise import as a modal, including failed input preservation and the
    // independently delivered catalog/control notifications on success.
    auto* openImport = host.window()->findChild<QQuickItem*>("open_subscription_import");
    auto* importDialog = host.window()->findChild<QObject*>("subscription_import_dialog");
    auto* url = host.window()->findChild<QQuickItem*>("subscription_url");
    auto* importNameField = host.window()->findChild<QQuickItem*>("import_profile_name");
    auto* add = host.window()->findChild<QQuickItem*>("add_subscription");
    auto* importError = host.window()->findChild<QQuickItem*>("subscription_import_error");
    if (!openImport || !importDialog || !url || !importNameField || !add || !importError ||
        url->isVisible() || !click(host, openImport) ||
        !importDialog->property("visible").toBool() || !url->isVisible() || !url->hasActiveFocus())
        return 118;
    url->setProperty("text", "https://fixture.invalid/private-token");
    importNameField->setProperty("text", "Import fixture");
    if (!click(host, add))
        return 119;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"selectedId", "p0"},
                     {"error", "HttpsSubscriptionRequired"}});
    QTest::qWait(20);
    if (!importDialog->property("visible").toBool() || !importError->isVisible() ||
        url->property("text") != "https://fixture.invalid/private-token" ||
        importNameField->property("text") != "Import fixture")
        return 120;
    if (!click(host, add))
        return 121;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"selectedId", "p0"},
                     {"busy", true}});
    if (add->isEnabled())
        return 122;
    auto addedProfiles = profiles;
    addedProfiles.append(QJsonObject{{"id", "new_import"}, {"name", "New import"}});
    ui.applyData({1, "fixture", {{"profiles", addedProfiles}}});
    if (!importDialog->property("visible").toBool())
        return 123;
    // A saved profile followed by a later apply error is not safe to import again.
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"selectedId", "p0"},
                     {"error", "CoreConfigValidationFailed"}});
    QTest::qWait(20);
    if (!importDialog->property("visible").toBool() || !importError->isVisible() ||
        add->isEnabled() || url->property("text") != "https://fixture.invalid/private-token")
        return 125;
    ui.applyControl({{"publicationGeneration", "1"}, {"epoch", "fixture"}, {"selectedId", "p0"}});
    QTest::qWait(80);
    if (importDialog->property("visible").toBool() || !url->property("text").toString().isEmpty())
        return 124;
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    QMetaObject::invokeMethod(page, "scrollToRow", Q_ARG(QVariant, 0));
    QTest::qWait(50);
    auto initial = cards(host);
    if (initial.isEmpty() || initial.size() > 96 || initial.size() >= profiles.size()) {
        std::fprintf(stderr, "Subscription delegates=%lld pageHeight=%.0f contentHeight=%.0f\n",
                     static_cast<long long>(initial.size()), page->property("height").toDouble(),
                     page->property("contentHeight").toDouble());
        return 82;
    }
    QPointer<QQuickItem> first = initial.first();
    QList<QPair<QPointer<QQuickItem>, QString>> originalCards;
    for (auto* card : initial)
        originalCards.append({card, card->property("profileId").toString()});
    const auto managedId = first->property("profileId").toString();
    auto* progress = first->findChild<QQuickItem*>(QStringLiteral("subscription_usage_progress"));
    auto* alert = first->findChild<QQuickItem*>(QStringLiteral("subscription_usage_alert"));
    auto* collected = first->findChild<QQuickItem*>(QStringLiteral("subscription_usage_collected"));
    if (!progress || progress->isVisible() || !alert || alert->isVisible() || !collected ||
        !collected->isVisible() || collected->property("text").toString().isEmpty())
        return 98;
    const auto firstIndex = managedId.mid(1).toInt();
    auto warned = profiles[firstIndex].toObject();
    warned.insert("compatibilityNotices", QJsonArray{"ManagedGeo", "IgnoredProxyExtensions"});
    warned.insert("usageStatus",
                  domain::subscriptionUsageStatus(
                      domain::parseSubscriptionUsage("upload=10;download=90;total=100;expire=1"),
                      QDateTime::currentDateTimeUtc()));
    warned.insert("usageCollectedAt", "2026-10-28T00:00:00Z");
    warned.insert("providerMode", "runtime");
    warned.insert("managedProviders",
                  QJsonArray{QJsonObject{{"name", "nodes"}, {"mode", "runtime"}}});
    profiles[firstIndex] = warned;
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    QTest::qWait(30);
    if (!progress->isVisible() || progress->property("value").toDouble() != 1 ||
        !alert->isVisible() || alert->property("text").toString().isEmpty() ||
        !collected->property("text").toString().contains("2026"))
        return 99;
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
    auto* geoNotice =
        host.window()->findChild<QQuickItem*>(QStringLiteral("subscription_managed_geo_notice"));
    auto* extensionNotice = host.window()->findChild<QQuickItem*>(
        QStringLiteral("subscription_proxy_extension_notice"));
    if (!geoNotice || !extensionNotice || !geoNotice->isVisible() || !extensionNotice->isVisible())
        return 132;
    auto* savedUrl = host.window()->findChild<QObject*>(QStringLiteral("subscription_saved_url"));
    auto* showUrl = host.window()->findChild<QObject*>(QStringLiteral("subscription_show_url"));
    auto* copyUrl = host.window()->findChild<QObject*>(QStringLiteral("subscription_copy_url"));
    const auto fixtureUrl = QStringLiteral("https://fixture.invalid/sub?token=private%2Bvalue");
    const auto request = [&](QObject* button) {
        QMetaObject::invokeMethod(button, "clicked");
        return page->property("urlRequestToken").toString();
    };
    if (!savedUrl || !showUrl || !copyUrl || !savedUrl->property("text").toString().isEmpty())
        return 105;
    auto token = request(showUrl);
    ui.subscriptionUrlReady("another-profile", token, fixtureUrl);
    if (token.isEmpty() || !savedUrl->property("text").toString().isEmpty())
        return 106;
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (savedUrl->property("text") != fixtureUrl)
        return 107;
    QMetaObject::invokeMethod(showUrl, "clicked");
    token = request(copyUrl);
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (QGuiApplication::clipboard()->text() != fixtureUrl ||
        !savedUrl->property("text").toString().isEmpty() || page->property("urlResult") != "Copied")
        return 108;
    token = request(showUrl);
    ui.setUiVisible(false);
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (!savedUrl->property("text").toString().isEmpty() ||
        !page->property("urlRequestToken").toString().isEmpty())
        return 109;
    ui.setUiVisible(true);
    const auto baselineFont = QApplication::font();
    for (const auto pixels : {14, 21}) {
        auto font = baselineFont;
        font.setPixelSize(pixels);
        QApplication::setFont(font);
        for (const auto& theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
            ui.setThemeMode(theme);
            for (const auto& size : {QSize{1000, 760}, QSize{360, 640}}) {
                host.resizeWindow(size);
                QTest::qWait(50);
                if (!test::checkScrollLayouts(host.window()))
                    return 114;
                for (const auto* name :
                     {"subscription_name", "subscription_saved_url", "subscription_update_interval",
                      "subscription_provider_mode", "subscription_download_route",
                      "subscription_source_url", "subscription_source_headers"}) {
                    auto* field = host.window()->findChild<QQuickItem*>(QString::fromLatin1(name));
                    if (!test::focusInScroll(field)) {
                        std::fprintf(stderr, "Subscription focus: %s font=%d width=%d\n", name,
                                     pixels, size.width());
                        return 115;
                    }
                    if (!visualOutput.isEmpty() &&
                        (QString::fromLatin1(name) == "subscription_name" ||
                         QString::fromLatin1(name) == "subscription_source_headers")) {
                        const auto path = QStringLiteral("focus_%1_%2_font_%3_%4.png")
                                              .arg(theme)
                                              .arg(size.width())
                                              .arg(pixels)
                                              .arg(QString::fromLatin1(name));
                        if (!host.window()->grabWindow().save(QDir(visualOutput).filePath(path)))
                            return 116;
                    }
                }
            }
        }
    }
    QApplication::setFont(baselineFont);
    host.resizeWindow({1500, 850});
    QTest::qWait(50);
    if (!test::focusInScroll(host.window()->findChild<QQuickItem*>("subscription_name")))
        return 115;
    if (!visualOutput.isEmpty()) {
        for (const auto& theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
            ui.setThemeMode(theme);
            for (const auto& size : {QSize(1500, 850), QSize(360, 640)}) {
                host.resizeWindow(size);
                QTest::qWait(100);
                for (const bool reveal : {false, true}) {
                    if (reveal) {
                        token = request(showUrl);
                        ui.subscriptionUrlReady(managedId, token, fixtureUrl);
                    } else {
                        QMetaObject::invokeMethod(page, "clearSavedUrl");
                    }
                    QTest::qWait(60);
                    const auto name = QStringLiteral("%1_%2_%3.png")
                                          .arg(theme)
                                          .arg(size.width())
                                          .arg(reveal ? "shown" : "hidden");
                    if (!host.window()->isExposed() ||
                        !host.window()->grabWindow().save(QDir(visualOutput).filePath(name)))
                        return 113;
                }
            }
        }
        std::printf("Subscription visual: exposed=%d api=%d dpr=%.2f, 24 captures\n",
                    host.window()->isExposed(),
                    static_cast<int>(host.window()->rendererInterface()->graphicsApi()),
                    host.window()->devicePixelRatio());
        host.resizeWindow({1500, 850});
        QTest::qWait(50);
    }
    token = request(showUrl);
    QMetaObject::invokeMethod(dialog, "close");
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (!savedUrl->property("text").toString().isEmpty())
        return 110;
    QMetaObject::invokeMethod(dialog, "open");
    QTest::qWait(30);
    token = request(showUrl);
    ui.subscriptionUrlReady(managedId, token, {});
    if (page->property("urlResult") != "Failed")
        return 111;
    auto* downloadRoute =
        host.window()->findChild<QObject*>(QStringLiteral("subscription_download_route"));
    if (!downloadRoute || downloadRoute->property("count").toInt() != 3 ||
        downloadRoute->property("currentIndex").toInt() != 0)
        return 100;
    auto* providerMode =
        host.window()->findChild<QObject*>(QStringLiteral("subscription_provider_mode"));
    if (!providerMode || !providerMode->property("visible").toBool() ||
        providerMode->property("count").toInt() != 2 ||
        providerMode->property("currentIndex").toInt() != 1)
        return 101;
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
    changed.insert("downloadRoute", "RunningProxy");
    profiles[managedIndex] = changed;
    QSignalSpy reset(ui.profileModel(), &QAbstractItemModel::modelReset);
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    auto managedValue = page->property("managedProfile");
    if (managedValue.metaType() == QMetaType::fromType<QJSValue>())
        managedValue = managedValue.value<QJSValue>().toVariant();
    const auto managed = managedValue.toMap();
    if (managed.value("name") != "Updated while offscreen" || reset.size() ||
        downloadRoute->property("currentIndex").toInt() != 1)
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
    auto* editor = host.window()->findChild<QObject*>(QStringLiteral("profile_editor"));
    if (!editor || !QMetaObject::invokeMethod(editor, "edit", Q_ARG(QVariant, "p0"),
                                              Q_ARG(QVariant, "source")))
        return 101;
    QTest::qWait(30);
    ui.applyData({1,
                  "fixture",
                  {{"editor", QJsonObject{{"id", "p0"},
                                          {"kind", "source"},
                                          {"token", "editor_ui"},
                                          {"text", "# private draft"},
                                          {"name", "Fixture"}}}},
                  ui.dataDemands().generations});
    auto* content = host.window()->findChild<QObject*>(QStringLiteral("profile_editor_text"));
    if (!content || content->property("text") != "# private draft" ||
        !editor->property("ready").toBool())
        return 102;
    for (const auto width : {1000, 360}) {
        host.resizeWindow({width, 640});
        QTest::qWait(40);
        if (!test::checkScrollLayouts(host.window()) ||
            !test::focusInScroll(qobject_cast<QQuickItem*>(content)))
            return 117;
    }
    content->setProperty("text", "# changed just before hide");
    if (!editor->property("dirty").toBool())
        return 103;
    ui.setUiVisible(false);
    if (!content->property("text").toString().isEmpty() || !ui.editor().isEmpty() ||
        ui.dataDemands().active.contains("editor"))
        return 104;
    ui.setUiVisible(true);
    QMetaObject::invokeMethod(editor, "close");
    std::puts("Subscription UI: bounded/reused delegates, stable actions, persistent management, "
              "incremental metadata, responsive long text and keyboard passed");
    return 0;
}
} // namespace quickclash
