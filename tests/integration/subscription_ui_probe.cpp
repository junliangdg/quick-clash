#include "subscription_ui_probe.h"
#include "application_host.h"
#include "domain/subscription_usage.h"
#include "focus_layout_checks.h"
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QJSValue>
#include <QPointer>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <QVersionNumber>
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
bool checkUrlAccessibility(QObject* field, const QString& expectedLabel, bool protectedEdit) {
    auto* accessible = QAccessible::queryAccessibleInterface(field);
    if (!accessible || accessible->state().passwordEdit != protectedEdit ||
        accessible->text(QAccessible::Description) != expectedLabel) {
        std::fprintf(stderr,
                     "Subscription URL accessibility: description or protection mismatch\n");
        return false;
    }
    if (!protectedEdit)
        return accessible->text(QAccessible::Name) == expectedLabel;
    const auto secret = field->property("text").toString();
    const auto name = accessible->text(QAccessible::Name);
    const auto value = accessible->text(QAccessible::Value);
    const auto description = accessible->text(QAccessible::Description);
    if ((!secret.isEmpty() &&
         (name.contains(secret) || description.contains(secret) || value.contains(secret))) ||
        (!name.isEmpty() && name != expectedLabel)) {
        std::fprintf(stderr, "Subscription URL accessibility: unsafe name or value\n");
        return false;
    }
    // Qt 6.10 added a public label relation. Older supported versions retain
    // the description above, but do not expose this relationship.
    if (QVersionNumber::fromString(QString::fromLatin1(qVersion())) >= QVersionNumber(6, 10)) {
        // Query the direction Qt Quick exposes for this field's label.
        const auto relations = accessible->relations(QAccessible::Label);
        bool found = false;
        for (const auto& relation : relations)
            found |= relation.first && relation.first->text(QAccessible::Name) == expectedLabel;
        if (!found) {
            std::fprintf(stderr, "Subscription URL accessibility: label relation missing\n");
            return false;
        }
    }
    return true;
}
int checkSubscriptionLayout(ApplicationHost& host, QObject* savedUrl, const QString& visualOutput) {
    auto& ui = host.state();
    auto* managementScroll =
        host.window()->findChild<QQuickItem*>("subscription_management_scroll");
    auto* managementForm = host.window()->findChild<QQuickItem*>("subscription_management_form");
    auto* headers = host.window()->findChild<QObject*>("subscription_source_headers");
    if (!managementScroll || !managementForm || !headers)
        return 166;
    const auto originalUrl = savedUrl->property("text").toString();
    const auto originalHeaders = headers->property("text").toString();
    const auto longUrl =
        QStringLiteral("https://fixture.invalid/") + QString(512, QLatin1Char('W'));
    const auto longHeaders = QStringLiteral("{\"Authorization\":\"") +
                             QString(512, QLatin1Char('W')) + QStringLiteral("\"}");
    if (!savedUrl->setProperty("text", longUrl) || !headers->setProperty("text", longHeaders))
        return 166;
    const auto baselineFont = QApplication::font();
    struct LayoutCase {
        int pixels;
        const char* theme;
        QSize size;
    };
    QList<LayoutCase> cases{
        {14, "light", {1000, 760}}, {14, "dark", {360, 640}}, {21, "light", {360, 640}}};
    if (!visualOutput.isEmpty()) {
        cases.clear();
        for (const auto pixels : {14, 21})
            for (const auto* theme : {"light", "dark"})
                for (const auto& size : {QSize{1000, 760}, QSize{360, 640}})
                    cases.append({pixels, theme, size});
    }
    for (const auto& entry : cases) {
        const auto pixels = entry.pixels;
        const auto size = entry.size;
        const auto theme = QString::fromLatin1(entry.theme);
        auto font = baselineFont;
        font.setPixelSize(pixels);
        QApplication::setFont(font);
        ui.setThemeMode(theme);
        host.resizeWindow(size);
        QTest::qWait(50);
        if (!test::checkScrollLayouts(host.window()))
            return 114;
        auto* actions = host.window()->findChild<QQuickItem*>("subscription_editor_actions");
        auto* validate = host.window()->findChild<QQuickItem*>("subscription_validate_source");
        auto* editSource = host.window()->findChild<QQuickItem*>("subscription_edit_source");
        auto* editOverrides = host.window()->findChild<QQuickItem*>("subscription_edit_overrides");
        if (!actions || !validate || !editSource || !editOverrides)
            return 139;
        const QList<QQuickItem*> buttons{editSource, editOverrides};
        const auto spacing = actions->property("spacing").toReal();
        for (int index = 0; index < buttons.size(); ++index) {
            const auto* button = buttons[index];
            if (button->parentItem() != actions || button->x() < -0.5 ||
                button->x() + button->width() > actions->width() + 0.5)
                return 140;
            if (index == 0)
                continue;
            const auto* previous = buttons[index - 1];
            const bool shouldFit = previous->x() + previous->width() + spacing + button->width() <=
                                   actions->width() + 0.5;
            const bool sameRow = qAbs(previous->y() - button->y()) < 0.5;
            if (shouldFit != sameRow ||
                (sameRow && button->x() + 0.5 < previous->x() + previous->width()) ||
                (!sameRow && button->y() + 0.5 < previous->y() + previous->height())) {
                std::fprintf(stderr,
                             "Subscription actions overlap/wrap: font=%d width=%d "
                             "button=%d form=%.1f previous=%.1f..%.1f next=%.1f..%.1f\n",
                             pixels, size.width(), index, actions->width(), previous->x(),
                             previous->x() + previous->width(), button->x(),
                             button->x() + button->width());
                return 141;
            }
        }
        if (host.window()->grabWindow().isNull())
            return 116;
        for (auto* item : managementForm->childItems()) {
            if (item->isVisible() &&
                (item->x() < -0.5 || item->x() + item->width() > managementForm->width() + 0.5)) {
                std::fprintf(stderr,
                             "Subscription form child exceeds bounds: %s (%s) "
                             "x=%.1f width=%.1f form=%.1f font=%d window=%d\n",
                             item->objectName().toUtf8().constData(),
                             item->metaObject()->className(), item->x(), item->width(),
                             managementForm->width(), pixels, size.width());
                return 168;
            }
        }
        for (const auto* name :
             {"subscription_name", "subscription_saved_url", "subscription_update_interval",
              "subscription_provider_mode", "subscription_download_route",
              "subscription_source_headers", "subscription_versions"}) {
            auto* field = host.window()->findChild<QQuickItem*>(QString::fromLatin1(name));
            if (!test::focusInScroll(field)) {
                std::fprintf(stderr, "Subscription focus: %s font=%d width=%d\n", name, pixels,
                             size.width());
                return 115;
            }
            const auto formWidth = managementScroll->property("formWidth").toReal();
            if (field->width() > formWidth + 0.5) {
                std::fprintf(stderr,
                             "Subscription field exceeds form: %s width=%.1f "
                             "form=%.1f font=%d window=%d\n",
                             name, field->width(), formWidth, pixels, size.width());
                return 167;
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
    QApplication::setFont(baselineFont);
    if (!savedUrl->setProperty("text", originalUrl) ||
        !headers->setProperty("text", originalHeaders))
        return 166;
    std::puts("Subscription layout: representative wide/narrow and large-font cases passed");
    return 0;
}
} // namespace
int runSubscriptionUiProbe(ApplicationHost& host) {
    // Synthetic delivery only: no SettingsService, subscription, core or platform command
    // execution.
    auto& ui = host.state();
    QAccessible::setActive(true);
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
    // The same details component is used on the dashboard and in every card.
    // Only collapse times which actually render as the same local minute.
    const auto originalFirst = profiles[0].toObject();
    auto timestampFixture = originalFirst;
    const auto checkTimes = [&](const QString& updated, const QString& collected,
                                bool showCollected) {
        timestampFixture.insert("updatedAt", updated);
        timestampFixture.insert("usageCollectedAt", collected);
        profiles[0] = timestampFixture;
        ui.applyData({1, "fixture", {{"profiles", profiles}}});
        QCoreApplication::processEvents();
        auto* details = host.window()->findChild<QQuickItem*>("home_subscription_details");
        auto* update =
            details ? details->findChild<QQuickItem*>("subscription_updated_overview") : nullptr;
        auto* usage =
            details ? details->findChild<QQuickItem*>("subscription_usage_collected") : nullptr;
        if (!details || !details->isVisible() || !update || !update->isVisible() || !usage ||
            usage->isVisible() != showCollected) {
            std::fprintf(stderr, "Dashboard timestamps: updated=%s collected=%s expected=%d\n",
                         updated.toUtf8().constData(), collected.toUtf8().constData(),
                         showCollected);
            return false;
        }
        return true;
    };
    if (!checkTimes("2026-10-28T00:00:00Z", "2026-10-28T00:00:59Z", false) ||
        !checkTimes("2026-10-28T00:02:00Z", "2026-10-28T00:00:00Z", true) ||
        !checkTimes("2026-10-28T00:02:00Z", {}, true) ||
        !checkTimes("invalid", "2026-10-28T00:00:00Z", true))
        return 133;
    profiles[0] = originalFirst;
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
    const auto importLabel = url->property("placeholderText").toString();
    if (!checkUrlAccessibility(url, importLabel, true))
        return 164;
    // Flow positions its buttons during polish; wait for a rendered layout before clicking.
    if (!visualOutput.isEmpty() && host.window()->grabWindow().isNull())
        return 119;
    if (!QTest::qWaitFor(
            [&] {
                host.window()->update();
                return add->parentItem() && add->parentItem()->height() >= add->height();
            },
            2000)) {
        std::fprintf(stderr, "Import actions layout: height=%.2f implicit=%.2f button=%.2f\n",
                     add->parentItem() ? add->parentItem()->height() : -1,
                     add->parentItem() ? add->parentItem()->implicitHeight() : -1, add->height());
        return 119;
    }
    url->setProperty("text", "https://fixture.invalid/private-token");
    if (!checkUrlAccessibility(url, importLabel, true))
        return 164;
    importNameField->setProperty("text", "Import fixture");
    const auto submitImport = [&] { return test::revealInScroll(add) && click(host, add); };
    if (!submitImport())
        return 119;
    if (!checkUrlAccessibility(url, importLabel, true))
        return 164;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"selectedId", "p0"},
                     {"error", "HttpsSubscriptionRequired"}});
    QTest::qWait(20);
    if (!importDialog->property("visible").toBool() || !importError->isVisible() ||
        url->property("text") != "https://fixture.invalid/private-token" ||
        importNameField->property("text") != "Import fixture") {
        std::fprintf(stderr,
                     "Import failure state: dialog=%d error=%d attempted=%d active=%d url=%d "
                     "name=%d\n",
                     importDialog->property("visible").toBool(), importError->isVisible(),
                     importDialog->property("attempted").toBool(), host.window()->isActive(),
                     url->property("text") == "https://fixture.invalid/private-token",
                     importNameField->property("text") == "Import fixture");
        return 120;
    }
    if (!submitImport())
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
    warned.insert("history", QJsonArray{QJsonObject{{"updatedAt", "2026-10-28T00:00:00Z"}}});
    profiles[firstIndex] = warned;
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    QTest::qWait(30);
    if (!progress->isVisible() || progress->property("value").toDouble() != 1 ||
        !alert->isVisible() || alert->property("text").toString().isEmpty() ||
        !collected->property("text").toString().contains("2026"))
        return 99;
    auto* updatedCard = first->findChild<QQuickItem*>("subscription_updated_card");
    auto* usage = first->findChild<QQuickItem*>("subscription_usage");
    auto* expiry = first->findChild<QQuickItem*>("subscription_expiry");
    auto* status = first->findChild<QQuickItem*>("subscription_update_status");
    if (!updatedCard || !usage || !expiry || !status)
        return 134;
    for (const auto& theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        ui.setThemeMode(theme);
        warned.insert("updatedAt", "2026-10-28T00:00:00Z");
        warned.insert("usageCollectedAt", "2026-10-28T00:00:59Z");
        warned.insert("pendingApply", false);
        warned.insert("lastUpdateResult", "Updated");
        profiles[firstIndex] = warned;
        ui.applyData({1, "fixture", {{"profiles", profiles}}});
        QCoreApplication::processEvents();
        const auto primary = usage->property("color").value<QColor>();
        if (collected->isVisible() || !updatedCard->isVisible() ||
            updatedCard->property("color").value<QColor>() != primary ||
            expiry->property("color").value<QColor>() != primary ||
            status->property("color").value<QColor>() != primary ||
            collected->property("color").value<QColor>() == primary)
            return 135;
        warned.insert("updatedAt", "2026-10-28T00:02:00Z");
        warned.insert("lastUpdateResult", "Failed");
        profiles[firstIndex] = warned;
        ui.applyData({1, "fixture", {{"profiles", profiles}}});
        QCoreApplication::processEvents();
        if (!collected->isVisible() || status->property("color").value<QColor>() == primary)
            return 136;
    }
    warned.insert("pendingApply", true);
    warned.insert("lastApplyError", "CoreConfigValidationFailed");
    warned.insert("lastUpdateResult", "Failed");
    warned.insert("lastUpdateError", "SubscriptionDownloadFailed");
    warned.insert("lastUpdateSaved", false);
    profiles[firstIndex] = warned;
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    QCoreApplication::processEvents();
    auto* updateError = first->findChild<QQuickItem*>("subscription_update_error");
    auto* applyError = first->findChild<QQuickItem*>("subscription_apply_error");
    auto* pendingStatus = first->findChild<QQuickItem*>("subscription_pending_status");
    if (!updateError || !updateError->isVisible() || !applyError || !applyError->isVisible() ||
        !pendingStatus || !pendingStatus->isVisible())
        return 155;
    warned.remove("lastApplyError");
    warned.remove("lastUpdateError");
    warned.remove("lastUpdateSaved");
    warned.remove("lastUpdateResult");
    profiles[firstIndex] = warned;
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    QSignalSpy action(first, SIGNAL(actionRequested(QString, QString)));
    QSignalSpy manage(first, SIGNAL(manageRequested(QString)));
    auto* title = first->findChild<QQuickItem*>(QStringLiteral("select_subscription"));
    auto* useSubscription = first->findChild<QQuickItem*>(QStringLiteral("apply_subscription"));
    if (!title || !useSubscription || !useSubscription->isVisible() ||
        !QMetaObject::invokeMethod(useSubscription, "click") || action.size() != 1 ||
        action[0][0].toString() != "select" || action[0][1] != managedId)
        return 83;
    action.clear();
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
    auto* editUrl = host.window()->findChild<QObject*>(QStringLiteral("subscription_edit_url"));
    auto* saveUrl =
        host.window()->findChild<QObject*>(QStringLiteral("subscription_validate_source"));
    auto* closeManagement =
        host.window()->findChild<QObject*>(QStringLiteral("subscription_management_close"));
    auto* closeConfirmation = host.window()->findChild<QObject*>(
        QStringLiteral("subscription_source_close_confirmation"));
    const auto fixtureUrl = QStringLiteral("https://fixture.invalid/sub?token=private%2Bvalue");
    const auto request = [&](QObject* button) {
        QMetaObject::invokeMethod(button, "clicked");
        return page->property("urlRequestToken").toString();
    };
    if (!savedUrl || !showUrl || !copyUrl || !editUrl || !saveUrl || !closeManagement ||
        !closeConfirmation || !savedUrl->property("text").toString().isEmpty())
        return 105;
    const auto savedLabel = QCoreApplication::translate("subscriptions_page", "Subscription URL");
    if (!checkUrlAccessibility(savedUrl, savedLabel, true))
        return 165;
    auto localProfile = profiles[firstIndex].toObject();
    localProfile.remove("secretRef");
    profiles[firstIndex] = localProfile;
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    QMetaObject::invokeMethod(editUrl, "clicked");
    if (!page->property("editingSource").toBool() ||
        !savedUrl->property("text").toString().isEmpty())
        return 160;
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_cancel_source"),
                              "clicked");
    profiles[firstIndex] = warned;
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    auto token = request(showUrl);
    ui.subscriptionUrlReady("another-profile", token, fixtureUrl);
    if (token.isEmpty() || !savedUrl->property("text").toString().isEmpty())
        return 106;
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (savedUrl->property("text") != fixtureUrl ||
        savedUrl->property("cursorPosition").toInt() != 0)
        return 107;
    if (!checkUrlAccessibility(savedUrl, savedLabel, false))
        return 165;
    token = request(copyUrl);
    if (savedUrl->property("text") != fixtureUrl)
        return 137;
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (QGuiApplication::clipboard()->text() != fixtureUrl ||
        savedUrl->property("text") != fixtureUrl || page->property("urlResult") != "Copied")
        return 138;
    QMetaObject::invokeMethod(showUrl, "clicked");
    // Let the native clipboard release the preceding write before issuing another.
    if (!visualOutput.isEmpty())
        QTest::qWait(150);
    token = request(copyUrl);
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (!QTest::qWaitFor([&] { return QGuiApplication::clipboard()->text() == fixtureUrl; },
                         1000) ||
        !savedUrl->property("text").toString().isEmpty() ||
        page->property("urlResult") != "Copied") {
        std::fprintf(stderr, "Hidden URL copy: clipboard=%d fieldEmpty=%d result=%s\n",
                     QGuiApplication::clipboard()->text() == fixtureUrl,
                     savedUrl->property("text").toString().isEmpty(),
                     qPrintable(page->property("urlResult").toString()));
        return 108;
    }
    token = request(showUrl);
    ui.setUiVisible(false);
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (!savedUrl->property("text").toString().isEmpty() ||
        !page->property("urlRequestToken").toString().isEmpty())
        return 109;
    ui.setUiVisible(true);
    token = request(editUrl);
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    if (!page->property("editingSource").toBool() || savedUrl->property("readOnly").toBool() ||
        savedUrl->property("text") != fixtureUrl)
        return 145;
    if (!checkUrlAccessibility(savedUrl, savedLabel, false))
        return 165;
    QMetaObject::invokeMethod(showUrl, "clicked");
    if (!checkUrlAccessibility(savedUrl, savedLabel, true))
        return 165;
    savedUrl->setProperty("text", "https://fixture.invalid/new-token");
    if (ui.navigate("home") || !closeConfirmation->property("visible").toBool() ||
        savedUrl->property("text") != "https://fixture.invalid/new-token")
        return 151;
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_close_continue"),
                              "clicked");
    QMetaObject::invokeMethod(closeManagement, "clicked");
    if (!closeConfirmation->property("visible").toBool() || !page->property("sourceDirty").toBool())
        return 146;
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_close_continue"),
                              "clicked");
    QMetaObject::invokeMethod(saveUrl, "clicked");
    const auto operationToken = page->property("sourceOperationToken").toString();
    if (operationToken.isEmpty() || !page->property("sourceSaving").toBool())
        return 147;
    QMetaObject::invokeMethod(closeManagement, "clicked");
    if (!dialog->property("visible").toBool() || !ui.subscriptionAddressSaving())
        return 152;
    ui.subscriptionSourceFinished(managedId, "stale-token", false, "SubscriptionDownloadFailed");
    if (!page->property("sourceSaving").toBool())
        return 148;
    ui.subscriptionSourceFinished(managedId, operationToken, false, "SubscriptionDownloadFailed");
    if (page->property("sourceSaving").toBool() || ui.subscriptionAddressSaving() ||
        !dialog->property("visible").toBool() ||
        savedUrl->property("text") != "https://fixture.invalid/new-token" ||
        page->property("sourceError").toString().isEmpty() ||
        !page->property("sourceDirty").toBool()) {
        std::fprintf(
            stderr,
            "Source failure: saving=%d stateSaving=%d visible=%d input=%d error=%s dirty=%d\n",
            page->property("sourceSaving").toBool(), ui.subscriptionAddressSaving(),
            dialog->property("visible").toBool(),
            savedUrl->property("text") == "https://fixture.invalid/new-token",
            qPrintable(page->property("sourceError").toString()),
            page->property("sourceDirty").toBool());
        return 149;
    }
    QMetaObject::invokeMethod(saveUrl, "clicked");
    const auto retryToken = page->property("sourceOperationToken").toString();
    ui.subscriptionSourceFinished(managedId, retryToken, true, "CoreConfigValidationFailed");
    if (page->property("editingSource").toBool() ||
        page->property("urlResult") != "SavedApplyFailed" ||
        !savedUrl->property("text").toString().isEmpty())
        return 150;
    token = request(editUrl);
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    savedUrl->setProperty("text", "https://fixture.invalid/close-after-save");
    QMetaObject::invokeMethod(closeManagement, "clicked");
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_close_save"),
                              "clicked");
    const auto closeToken = page->property("sourceOperationToken").toString();
    if (!dialog->property("visible").toBool() || closeToken.isEmpty())
        return 153;
    ui.subscriptionSourceFinished(managedId, closeToken, true, {});
    if (dialog->property("visible").toBool() || ui.subscriptionAddressSaving())
        return 154;
    QMetaObject::invokeMethod(dialog, "open");
    token = request(editUrl);
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    savedUrl->setProperty("text", "https://fixture.invalid/delete-draft");
    auto* deleteButton = host.window()->findChild<QObject*>("subscription_delete");
    auto* deleteConfirmation =
        host.window()->findChild<QObject*>("subscription_delete_confirmation");
    if (!deleteButton || !deleteConfirmation || !ui.subscriptionAddressDirty())
        return 161;
    QMetaObject::invokeMethod(deleteButton, "clicked");
    if (!deleteConfirmation->property("visible").toBool())
        return 162;
    QMetaObject::invokeMethod(deleteConfirmation, "accept");
    if (dialog->property("visible").toBool() || closeConfirmation->property("visible").toBool() ||
        ui.subscriptionAddressDirty())
        return 163;
    QMetaObject::invokeMethod(dialog, "open");
    QTest::qWait(30);
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_advanced"),
                              "clicked");
    token = request(editUrl);
    ui.subscriptionUrlReady(managedId, token, fixtureUrl);
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_headers_toggle"),
                              "clicked");
    if (!visualOutput.isEmpty()) {
        const auto layoutResult = checkSubscriptionLayout(host, savedUrl, visualOutput);
        if (layoutResult)
            return layoutResult;
    }
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_cancel_source"),
                              "clicked");
    host.resizeWindow({1500, 850});
    QTest::qWait(50);
    if (!visualOutput.isEmpty()) {
        if (!test::focusInScroll(host.window()->findChild<QQuickItem*>("subscription_name")))
            return 115;
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
    if (host.window()->findChild<QObject*>(QStringLiteral("profile_editor")) ||
        !QMetaObject::invokeMethod(page, "editProfile", Q_ARG(QVariant, "p0"),
                                   Q_ARG(QVariant, "source")))
        return 101;
    QPointer<QObject> editor = host.window()->findChild<QObject*>(QStringLiteral("profile_editor"));
    if (!editor)
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
    if (!test::revealInScroll(qobject_cast<QQuickItem*>(content)))
        return 117;
    content->setProperty("text", "# changed just before hide");
    if (!editor->property("dirty").toBool())
        return 103;
    ui.setUiVisible(false);
    if (!content->property("text").toString().isEmpty() || !ui.editor().isEmpty() ||
        ui.dataDemands().active.contains("editor"))
        return 104;
    ui.setUiVisible(true);
    QMetaObject::invokeMethod(editor, "close");
    if (!QTest::qWaitFor([&] { return editor.isNull(); }, 1000) ||
        !QMetaObject::invokeMethod(page, "editProfile", Q_ARG(QVariant, "p0"),
                                   Q_ARG(QVariant, "overlay")))
        return 118;
    editor = host.window()->findChild<QObject*>(QStringLiteral("profile_editor"));
    if (!editor || editor->property("kind") != "overlay" ||
        !editor->property("loadedToken").toString().isEmpty())
        return 119;
    QMetaObject::invokeMethod(editor, "close");
    if (!QTest::qWaitFor([&] { return editor.isNull(); }, 1000)) {
        std::fprintf(stderr, "Closing overlay editor did not release it\n");
        return 120;
    }
    if (!ui.profileModel()->profile("p0").contains("id"))
        profiles.prepend(originalFirst);
    else
        profiles[0] = originalFirst;
    ui.applyData({1, "fixture", {{"profiles", profiles}}});
    QMetaObject::invokeMethod(page, "manage", Q_ARG(QVariant, "p0"));
    token = request(editUrl);
    ui.subscriptionUrlReady("p0", token, fixtureUrl);
    savedUrl->setProperty("text", "https://fixture.invalid/navigate-away");
    if (ui.navigate("home") || !closeConfirmation->property("visible").toBool())
        return 156;
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_close_discard"),
                              "clicked");
    if (!QTest::qWaitFor([&] { return !ui.pageSource().toString().contains("subscriptions_page"); },
                         1000))
        return 157;
    std::puts("Subscription UI: bounded/reused delegates, stable actions, persistent management, "
              "incremental metadata, privacy and keyboard passed");
    return 0;
}

int runSubscriptionLayoutProbe(ApplicationHost& host) {
    auto& ui = host.state();
    ui.applyControl({{"publicationGeneration", "1"}, {"epoch", "layout"}, {"selectedId", "p0"}});
    ui.applyData(
        {1,
         "layout",
         {{"profiles",
           QJsonArray{QJsonObject{
               {"id", "p0"},
               {"name", QString(80, QLatin1Char('W'))},
               {"secretRef", "opaque"},
               {"managedProviders",
                QJsonArray{QJsonObject{{"name", "nodes"}, {"mode", "runtime"}}}},
               {"history", QJsonArray{QJsonObject{{"updatedAt", "2026-10-28T00:00:00Z"}}}}}}}}});
    if (!ui.navigate("subscriptions"))
        return 80;
    host.resizeWindow({1000, 760});
    auto* page = host.window()->findChild<QObject*>("subscriptions_page");
    if (!page || !QMetaObject::invokeMethod(page, "manage", Q_ARG(QVariant, "p0")))
        return 87;
    auto* savedUrl = host.window()->findChild<QObject*>("subscription_saved_url");
    auto* dialog = host.window()->findChild<QObject*>("subscription_management");
    auto* advanced = host.window()->findChild<QObject*>("subscription_advanced");
    auto* headersToggle = host.window()->findChild<QObject*>("subscription_headers_toggle");
    if (!savedUrl || !dialog || !advanced || !headersToggle ||
        !QMetaObject::invokeMethod(advanced, "clicked") ||
        !QMetaObject::invokeMethod(page, "beginSourceEdit",
                                   Q_ARG(QVariant, "https://fixture.invalid")) ||
        !QMetaObject::invokeMethod(headersToggle, "clicked"))
        return 166;
    const auto result = checkSubscriptionLayout(host, savedUrl, {});
    if (result)
        return result;
    QMetaObject::invokeMethod(host.window()->findChild<QObject*>("subscription_cancel_source"),
                              "clicked");
    host.resizeWindow({1500, 850});
    QTest::qWait(50);
    if (!test::focusInScroll(host.window()->findChild<QQuickItem*>("subscription_name")))
        return 115;
    QMetaObject::invokeMethod(dialog, "close");
    if (!QMetaObject::invokeMethod(page, "editProfile", Q_ARG(QVariant, "p0"),
                                   Q_ARG(QVariant, "source")))
        return 101;
    ui.applyData({1,
                  "layout",
                  {{"editor", QJsonObject{{"id", "p0"},
                                          {"kind", "source"},
                                          {"token", "layout"},
                                          {"text", "# fixture"},
                                          {"name", "Fixture"}}}},
                  ui.dataDemands().generations});
    auto* content = host.window()->findChild<QQuickItem*>("profile_editor_text");
    if (!content)
        return 102;
    for (const auto width : {1000, 360}) {
        host.resizeWindow({width, 640});
        QTest::qWait(40);
        if (!test::checkScrollLayouts(host.window()) || !test::focusInScroll(content))
            return 117;
    }
    return host.qmlErrorCount() == 0 ? 0 : 95;
}
} // namespace quickclash
