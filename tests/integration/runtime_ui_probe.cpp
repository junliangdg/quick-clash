#include "runtime_ui_probe.h"
#include "application_host.h"
#include "focus_layout_checks.h"
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace quickclash {
namespace {
QJsonObject entry(const QString& id, const QString& host) {
    return {
        {"id", id}, {"epoch", "fixture"}, {"metadata", QJsonObject{{"host", host}}}, {"start", id}};
}
QQuickItem* findVisualItem(QQuickItem* parent, const QString& name) {
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    for (auto* child : parent->childItems()) {
        if (auto* found = findVisualItem(child, name))
            return found;
    }
    return nullptr;
}
bool click(ApplicationHost& host, const QString& name) {
    auto* item = host.window()->findChild<QQuickItem*>(name);
    if (!item)
        item = findVisualItem(host.window()->contentItem(), name);
    if (!item || !item->isEnabled() || !item->isVisible())
        return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mouseClick(host.window(), Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(20);
    return true;
}
bool checkCustomRulesLayout(ApplicationHost& host, bool listVisible = true) {
    const auto item = [&](const char* name) {
        return host.window()->findChild<QQuickItem*>(QString::fromLatin1(name));
    };
    const auto rect = [](QQuickItem* target) {
        return QRectF(target->mapToScene(QPointF()), QSizeF(target->width(), target->height()));
    };
    auto* title = item("custom_rules_title");
    auto* editor = item("user_rules_editor");
    auto* scope = item("user_rules_scope_hint");
    auto* order = item("user_rules_order_hint");
    auto* mode = item("user_rules_mode_hint");
    auto* reorder = item("user_rules_reorder_hint");
    auto* hint = item("custom_rules_confirm_hint");
    auto* cancel = item("cancel_custom_rules");
    auto* confirm = item("confirm_custom_rules");
    auto* listPage = item("user_rules_list_page");
    if (!title || !editor || !scope || !order || !mode || !reorder || !hint || !cancel ||
        !confirm || !listPage || !title->isVisible() || !editor->isVisible() ||
        !hint->isVisible() || !cancel->isVisible() || !confirm->isVisible()) {
        qWarning("Custom rules layout items missing or hidden at %dx%d", host.window()->width(),
                 host.window()->height());
        return false;
    }
    const auto fits = [](QQuickItem* target) {
        return target->width() > 0 && target->height() + 0.5 >= target->implicitHeight();
    };
    if (!fits(title) || !fits(hint) || rect(title).bottom() > rect(editor).top() + 0.5 ||
        rect(editor).bottom() > rect(hint).top() + 0.5 ||
        rect(editor).bottom() > rect(cancel).top() + 0.5 || rect(hint).intersects(rect(cancel)) ||
        rect(hint).intersects(rect(confirm)) || rect(confirm).right() > rect(cancel).left() + 0.5 ||
        rect(hint).left() > rect(editor).left() + 0.5 ||
        rect(cancel).right() < rect(editor).right() - 0.5 ||
        rect(cancel).right() > rect(editor).right() + 0.5 ||
        rect(confirm).left() < rect(editor).left() - 0.5) {
        qWarning("Custom rules dialog frame overlaps at %dx%d", host.window()->width(),
                 host.window()->height());
        qWarning() << "title" << rect(title) << title->implicitHeight() << "editor" << rect(editor)
                   << "hint" << rect(hint) << hint->implicitHeight() << "confirm" << rect(confirm)
                   << "cancel" << rect(cancel);
        return false;
    }
    if (!listVisible)
        return true;
    auto* scopeControl = item("user_rule_scope");
    auto* add = item("user_rule_add");
    auto* reload = item("user_rule_reload");
    if (!scopeControl || !add || !reload || !scope->isVisible() || !order->isVisible() ||
        !reorder->isVisible() || !fits(scope) || !fits(order) || !fits(reorder) ||
        (mode->isVisible() && !fits(mode)) || rect(scope).intersects(rect(order)) ||
        (mode->isVisible() && (rect(order).bottom() > rect(mode).top() + 0.5 ||
                               rect(mode).bottom() > rect(reorder).top() + 0.5)) ||
        rect(order).bottom() > rect(reorder).top() + 0.5 ||
        rect(scopeControl).left() < rect(listPage).left() - 0.5 ||
        rect(scopeControl).right() > rect(listPage).right() + 0.5 ||
        rect(scope).left() < rect(listPage).left() - 0.5 ||
        rect(scope).right() > rect(listPage).right() + 0.5 ||
        rect(order).right() > rect(listPage).right() + 0.5 ||
        rect(reorder).right() > rect(listPage).right() + 0.5 ||
        rect(add).right() > rect(listPage).right() + 0.5 ||
        rect(reload).right() > rect(listPage).right() + 0.5 ||
        scopeControl->height() > listPage->height() + 0.5 ||
        add->height() > listPage->height() + 0.5 || rect(scopeControl).intersects(rect(add)) ||
        rect(scopeControl).intersects(rect(reload)) || rect(add).intersects(rect(reload))) {
        qWarning("Custom rules text overlaps or is clipped at %dx%d", host.window()->width(),
                 host.window()->height());
        qWarning() << "list page" << rect(listPage) << "scope hint" << rect(scope)
                   << scope->implicitHeight() << "order hint" << rect(order)
                   << order->implicitHeight() << "reorder hint" << rect(reorder)
                   << reorder->implicitHeight();
        if (scopeControl && add && reload)
            qWarning() << "scope" << rect(scopeControl) << "reload" << rect(reload) << "add"
                       << rect(add);
        return false;
    }
    const auto contentHeight = listPage->property("contentHeight").toReal();
    const auto availableHeight = listPage->property("availableHeight").toReal();
    if (contentHeight > availableHeight + 0.5) {
        auto* flickable = listPage->property("contentItem").value<QObject*>();
        if (!flickable || !flickable->setProperty("contentY", 0)) {
            qWarning("Custom rules content cannot be scrolled");
            return false;
        }
        QTest::qWait(30);
        const auto maxScroll = contentHeight - availableHeight;
        flickable->setProperty("contentY",
                               qBound(0.0, rect(reorder).top() - rect(listPage).top(), maxScroll));
        QTest::qWait(30);
        if (rect(reorder).top() < rect(listPage).top() - 0.5 ||
            rect(reorder).top() > rect(listPage).bottom() + 0.5) {
            qWarning("Custom rules reorder hint start cannot be scrolled into view");
            return false;
        }
        const auto bottomScroll = flickable->property("contentY").toReal() +
                                  rect(reorder).bottom() - rect(listPage).bottom();
        flickable->setProperty("contentY", qBound(0.0, bottomScroll, maxScroll));
        QTest::qWait(30);
        if (rect(reorder).bottom() < rect(listPage).top() - 0.5 ||
            rect(reorder).bottom() > rect(listPage).bottom() + 0.5) {
            qWarning("Custom rules reorder hint end cannot be scrolled into view");
            return false;
        }
        flickable->setProperty("contentY", 0);
        QTest::qWait(30);
    }
    return true;
}

bool checkCustomRuleRow(ApplicationHost& host) {
    const auto item = [&](const char* name) {
        return findVisualItem(host.window()->contentItem(), QString::fromLatin1(name));
    };
    auto* row = item("user_rule_row_0");
    const QList<QQuickItem*> parts{item("user_rule_drag_0"),    item("user_rule_description_0"),
                                   item("user_rule_enabled_0"), item("user_rule_up_0"),
                                   item("user_rule_down_0"),    item("user_rule_edit_0"),
                                   item("user_rule_delete_0")};
    if (!row) {
        qWarning("Custom rule row was not created");
        return false;
    }
    for (qsizetype i = 0; i < parts.size(); ++i) {
        auto* part = parts[i];
        if (!part || part->width() <= 0 || part->height() <= 0) {
            qWarning() << "Missing or empty custom rule control" << i
                       << (part ? part->objectName() : QString());
            return false;
        }
        const QRectF bounds(part->mapToItem(row, QPointF()), QSizeF(part->width(), part->height()));
        if (bounds.left() < -0.5 || bounds.top() < -0.5 || bounds.right() > row->width() + 0.5 ||
            bounds.bottom() > row->height() + 0.5) {
            qWarning() << "Custom rule control outside its row" << part->objectName() << bounds
                       << row->size();
            return false;
        }
        for (qsizetype j = 0; j < i; ++j) {
            auto* other = parts[j];
            const QRectF otherBounds(other->mapToItem(row, QPointF()),
                                     QSizeF(other->width(), other->height()));
            if (bounds.intersects(otherBounds)) {
                qWarning() << "Custom rule controls overlap" << part->objectName()
                           << other->objectName();
                return false;
            }
        }
    }
    return true;
}
} // namespace
int runRuntimeListProbe(ApplicationHost& host) {
    // No SettingsService, core, network or platform actions: only synthetic GUI channel delivery.
    auto& ui = host.state();
    ui.setReplay(false);
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"status", "Ready"}});
    if (host.window()->findChild<QObject*>("user_rule_binding_dialog") ||
        host.window()->findChild<QObject*>("close_confirmation"))
        return 218;
    if (!ui.navigate("connections"))
        return 40;
    QTest::qWait(60);
    const application::RuntimeUpdate oldConnections{
        1,
        "fixture",
        {{"connections", QJsonArray{entry("one", "alpha"), entry("two", "beta")}}},
        ui.dataDemands().generations};
    ui.applyData(oldConnections);
    auto* filter = host.window()->findChild<QObject*>(QStringLiteral("runtime_filter"));
    if (!filter || !filter->setProperty("text", "alpha"))
        return 41;
    QTest::qWait(60);
    if (ui.liveConnections()->count() != 2)
        return 42;
    QTest::qWait(140);
    if (ui.liveConnections()->captureIds() != QStringList{"one"})
        return 43;
    if (!click(host, QStringLiteral("close_filtered_connections")))
        return 44;
    auto* page = host.window()->findChild<QObject*>(QStringLiteral("connections_page"));
    auto* dialog =
        host.window()->findChild<QObject*>(QStringLiteral("filtered_connections_confirmation"));
    if (!page || !dialog || !dialog->property("visible").toBool() ||
        page->property("closeEpoch") != "fixture")
        return 45;
    ui.applyData({1,
                  "fixture",
                  {{"connections", QJsonArray{entry("one", "alpha"), entry("new", "alpha")}}},
                  ui.dataDemands().generations});
    if (page->property("closeIds").toStringList() != QStringList{"one"})
        return 46;
    QMetaObject::invokeMethod(dialog, "reject");
    QTest::qWait(150);
    if (!QMetaObject::invokeMethod(page, "showDetails",
                                   Q_ARG(QVariant, entry("one", "alpha").toVariantMap())))
        return 76;
    auto* details = host.window()->findChild<QObject*>(QStringLiteral("connection_details"));
    if (!details || !details->property("visible").toBool() || page->property("selectedId") != "one")
        return 77;
    filter->setProperty("text", "new");
    ui.applyData({1,
                  "fixture",
                  {{"connections", QJsonArray{entry("one", "changed"), entry("new", "alpha")}}},
                  ui.dataDemands().generations});
    QTest::qWait(180);
    auto selected = page->property("selectedEntry");
    if (selected.metaType() == QMetaType::fromType<QJSValue>())
        selected = selected.value<QJSValue>().toVariant();
    if (page->property("selectedEnded").toBool() ||
        selected.toMap().value("metadata").toMap().value("host") != "changed")
        return 78;
    ui.applyData({1,
                  "fixture",
                  {{"connections", QJsonArray{entry("new", "alpha")}}},
                  ui.dataDemands().generations});
    if (!page->property("selectedEnded").toBool() || page->property("selectedId") != "one")
        return 79;
    QMetaObject::invokeMethod(details, "reject");
    QTest::qWait(150);
    ui.applyData(
        {1,
         "fixture",
         {{"connection_status", QJsonObject{{"connectionsPaused", true},
                                            {"connectionsStale", true},
                                            {"connectionSample", "2026-10-28T00:00:00Z"}}}},
         ui.dataDemands().generations});
    QTest::qWait(20);
    auto* status = host.window()->findChild<QObject*>(QStringLiteral("connection_status"));
    if (!status ||
        status->property("text").toString() !=
            QCoreApplication::translate(
                "connections_page", "Connection data is too large or invalid; updates paused") ||
        ui.liveConnections()->count() != 1)
        return 80;
    if (!ui.navigate("rules"))
        return 47;
    QTest::qWait(60);
    if (ui.liveConnections()->count() != 0 || !ui.dataDemands().active.contains("rules"))
        return 59;
    const application::RuntimeUpdate oldRules{
        1,
        "fixture",
        {{"rules", QJsonArray{QJsonObject{{"type", "DOMAIN"}, {"payload", "alpha"}},
                              QJsonObject{{"type", "MATCH"}}}}},
        ui.dataDemands().generations};
    ui.applyData(oldRules);
    auto* section = host.window()->findChild<QObject*>(QStringLiteral("rules_section"));
    auto* view = host.window()->findChild<QObject*>(QStringLiteral("runtime_view"));
    if (!section || !view || view->property("count").toInt() != 2)
        return 48;
    const auto checkHorizontalFocus = [&] {
        const auto baselineSize = host.window()->size();
        host.resizeWindow({360, 640});
        QTest::qWait(40);
        auto* listItem = qobject_cast<QQuickItem*>(view);
        auto* viewport = listItem ? listItem->parentItem() : nullptr;
        while (viewport && !viewport->property("contentX").isValid())
            viewport = viewport->parentItem();
        if (!viewport)
            return false;
        const auto range =
            qMax(0.0, viewport->property("contentWidth").toReal() - viewport->width());
        for (const auto x : {0.0, range / 2, range}) {
            viewport->setProperty("contentX", x);
            QTest::qWait(20);
            if (!test::checkVisibleFocusFrames(host.window()))
                return false;
        }
        viewport->setProperty("contentX", 0);
        host.resizeWindow(baselineSize);
        QTest::qWait(40);
        return true;
    };
    if (!checkHorizontalFocus())
        return 87;
    auto* rulesPage = host.window()->findChild<QObject*>(QStringLiteral("rules_page"));
    QString description;
    const QVariant ruleDescription = QVariantMap{{"position", 7},
                                                 {"type", "RuleSet"},
                                                 {"payload", "fixture"},
                                                 {"providerName", "fixture"},
                                                 {"proxy", "DIRECT"}};
    if (!rulesPage ||
        !QMetaObject::invokeMethod(rulesPage, "describeEntry", Q_RETURN_ARG(QString, description),
                                   Q_ARG(QVariant, ruleDescription)) ||
        !description.contains("#7") ||
        !description.contains(QCoreApplication::translate("rules_page", "Unknown")) ||
        !description.contains(QCoreApplication::translate("rules_page", "Limited information")))
        return 83;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"selectedId", "selected-fixture"},
                     {"status", "Ready"}});
    ui.applyData({1,
                  "fixture",
                  {{"rule_status", QJsonObject{{"rulePage", QJsonObject{{"loading", true}}}}}},
                  ui.dataDemands().generations});
    auto* refreshRules = host.window()->findChild<QObject*>(QStringLiteral("refresh_rules"));
    if (!refreshRules || refreshRules->property("enabled").toBool())
        return 81;
    const auto beforeCustomRulesSize = host.window()->size();
    host.resizeWindow({360, 640});
    QTest::qWait(40);
    auto* customRulesEntry = host.window()->findChild<QQuickItem*>("open_custom_rules");
    const auto entryPosition =
        customRulesEntry ? customRulesEntry->mapToScene(QPointF(0, 0)) : QPointF(-1, -1);
    if (!customRulesEntry || !customRulesEntry->isVisible() || entryPosition.x() < 0 ||
        entryPosition.x() + customRulesEntry->width() > host.window()->width())
        return 199;
    if (!click(host, "open_custom_rules"))
        return 196;
    host.resizeWindow(beforeCustomRulesSize);
    QTest::qWait(30);
    auto* customRulesDialog = host.window()->findChild<QObject*>("custom_rules_dialog");
    if (!customRulesDialog || !customRulesDialog->property("visible").toBool())
        return 200;
    if (!ui.dataDemands().active.contains("user_rules") ||
        ui.dataDemands().active.contains("rules"))
        return 182;
    QJsonArray globalRules;
    for (int i = 0; i < 12; ++i) {
        globalRules.append(QJsonObject{{"id", QString("global-%1").arg(i)},
                                       {"type", "DOMAIN-SUFFIX"},
                                       {"value", QString("fixture-%1.example").arg(i)},
                                       {"target", "DIRECT"},
                                       {"enabled", true}});
    }
    const QJsonObject customRuleView{{"revision", "revision-1"},
                                     {"profileId", "selected-fixture"},
                                     {"profileName", "Fixture"},
                                     {"global", globalRules},
                                     {"local", QJsonArray{QJsonObject{{"id", "local"},
                                                                      {"type", "RULE-SET"},
                                                                      {"value", "second"},
                                                                      {"target", "DIRECT"},
                                                                      {"enabled", true}}}},
                                     {"groups", QJsonArray{"Alpha"}},
                                     {"providers", QJsonArray{"first", "second"}}};
    ui.applyData({1, "fixture", {{"user_rules", customRuleView}}, ui.dataDemands().generations});
    const auto capturePath = qEnvironmentVariable("QUICKCLASH_RULE_LAYOUT_CAPTURE_DIR");
    const auto captureRules = [&](const QString& name) {
        if (!capturePath.isEmpty()) {
            for (const auto* objectName :
                 {"user_rules_toolbar", "user_rules_hints", "custom_rules_footer"}) {
                auto* item = host.window()->findChild<QQuickItem*>(objectName);
                if (item)
                    qInfo() << name << objectName << item->width() << item->property("columns");
            }
        }
        return capturePath.isEmpty() ||
               host.window()->grabWindow().save(QDir(capturePath).filePath(name + ".png"));
    };
    host.resizeWindow({1000, 640});
    QTest::qWait(40);
    if (!captureRules("wide") || !checkCustomRulesLayout(host) || !checkCustomRuleRow(host))
        return 203;
    auto* ruleList = host.window()->findChild<QQuickItem*>("user_rule_list");
    auto* firstRow = findVisualItem(ruleList, "user_rule_row_0");
    const auto rect = [](QQuickItem* item) {
        return QRectF(item->mapToScene(QPointF()), QSizeF(item->width(), item->height()));
    };
    auto* scopeControl = host.window()->findChild<QQuickItem*>("user_rule_scope");
    auto* reloadControl = host.window()->findChild<QQuickItem*>("user_rule_reload");
    auto* addControl = host.window()->findChild<QQuickItem*>("user_rule_add");
    auto* confirmHint = host.window()->findChild<QQuickItem*>("custom_rules_confirm_hint");
    auto* confirmControl = host.window()->findChild<QQuickItem*>("confirm_custom_rules");
    if (!scopeControl || !reloadControl || !addControl || !confirmHint || !confirmControl ||
        std::abs(rect(scopeControl).top() - rect(reloadControl).top()) > 1 ||
        std::abs(rect(scopeControl).top() - rect(addControl).top()) > 1 ||
        rect(confirmHint).bottom() <= rect(confirmControl).top() ||
        rect(confirmHint).top() >= rect(confirmControl).bottom()) {
        qWarning("Custom rules toolbar or footer failed to use the available width");
        return 215;
    }
    if (!ruleList || !firstRow || ruleList->property("count").toInt() != globalRules.size() ||
        ruleList->height() + 4 < 6 * (firstRow->height() + 4)) {
        qWarning() << "Insufficient visible custom rules" << (ruleList ? ruleList->height() : 0)
                   << (firstRow ? firstRow->height() : 0);
        return 209;
    }
    const auto shortListHeight = ruleList->height();
    qInfo() << "Custom rule list viewport and row height:" << shortListHeight << firstRow->height();
    const auto maxScroll = ruleList->property("contentHeight").toReal() - ruleList->height();
    if (maxScroll <= 0 || !ruleList->setProperty("contentY", maxScroll))
        return 210;
    QTest::qWait(30);
    if (ruleList->property("contentY").toReal() < maxScroll - 1)
        return 211;
    ruleList->setProperty("contentY", 0);
    host.resizeWindow({1000, 900});
    QTest::qWait(40);
    if (!checkCustomRulesLayout(host) || !checkCustomRuleRow(host) ||
        ruleList->height() < shortListHeight + 100 || !captureRules("tall"))
        return 212;
    host.resizeWindow({360, 640});
    QTest::qWait(40);
    if (!checkCustomRulesLayout(host) || !checkCustomRuleRow(host) || !captureRules("narrow"))
        return 204;
    host.resizeWindow({420, 640});
    QTest::qWait(40);
    if (!checkCustomRulesLayout(host) || !checkCustomRuleRow(host) ||
        std::abs(rect(scopeControl).top() - rect(reloadControl).top()) > 1 ||
        std::abs(rect(scopeControl).top() - rect(addControl).top()) > 1 ||
        rect(confirmHint).bottom() <= rect(confirmControl).top() ||
        rect(confirmHint).top() >= rect(confirmControl).bottom()) {
        qWarning("Custom rules toolbar or footer failed to fit in the narrow window");
        return 204;
    }
    const auto originalFont = host.window()->property("uiFont");
    auto largeFont = originalFont.value<QFont>();
    largeFont.setPixelSize(32);
    host.window()->setProperty("uiFont", largeFont);
    host.resizeWindow({360, 480});
    QTest::qWait(60);
    if (!checkCustomRulesLayout(host) || !checkCustomRuleRow(host) || !captureRules("large"))
        return 205;
    auto* ruleEditor = host.window()->findChild<QObject*>("user_rules_editor");
    if (!ruleEditor ||
        !QMetaObject::invokeMethod(ruleEditor, "edit", Q_ARG(QVariant, QVariant(-1))))
        return 207;
    QTest::qWait(30);
    if (!ruleEditor->property("formOpen").toBool() || !checkCustomRulesLayout(host, false))
        return 208;
    QMetaObject::invokeMethod(ruleEditor, "cancelForm");
    host.window()->setProperty("uiFont", originalFont);
    host.resizeWindow({1000, 640});
    QTest::qWait(40);
    auto* ruleScope = host.window()->findChild<QObject*>("user_rule_scope");
    auto* confirmRules = host.window()->findChild<QObject*>("confirm_custom_rules");
    if (!click(host, QStringLiteral("user_rule_down_0")) ||
        !ruleEditor->property("globalDirty").toBool() || !click(host, "user_rule_reload"))
        return 213;
    auto* reloadDialog = host.window()->findChild<QObject*>("user_rule_reload_dialog");
    if (!reloadDialog || !reloadDialog->property("visible").toBool() ||
        !QMetaObject::invokeMethod(reloadDialog, "reject") ||
        !ruleEditor->property("globalDirty").toBool() || !click(host, "user_rule_reload") ||
        !QMetaObject::invokeMethod(reloadDialog, "accept") ||
        ruleEditor->property("globalDirty").toBool())
        return 214;
    auto* firstHandle = findVisualItem(ruleList, "user_rule_drag_0");
    auto* secondHandle = findVisualItem(ruleList, "user_rule_drag_1");
    if (!firstHandle || !secondHandle)
        return 216;
    const auto start =
        firstHandle->mapToScene(QPointF(firstHandle->width() / 2, firstHandle->height() / 2))
            .toPoint();
    const auto end =
        secondHandle->mapToScene(QPointF(secondHandle->width() / 2, secondHandle->height() / 2))
            .toPoint();
    QTest::mousePress(host.window(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(host.window(), start + QPoint(0, 15), 20);
    QTest::mouseMove(host.window(), end, 20);
    QTest::mouseRelease(host.window(), Qt::LeftButton, Qt::NoModifier, end);
    QTest::qWait(40);
    auto reorderedDraft = ruleEditor->property("globalDraft");
    if (reorderedDraft.metaType() == QMetaType::fromType<QJSValue>())
        reorderedDraft = reorderedDraft.value<QJSValue>().toVariant();
    if (!ruleEditor->property("globalDirty").toBool() ||
        reorderedDraft.toList().size() != globalRules.size() ||
        reorderedDraft.toList().first().toMap().value("id") != "global-1" ||
        !click(host, "user_rule_reload") || !QMetaObject::invokeMethod(reloadDialog, "accept") ||
        ruleEditor->property("globalDirty").toBool())
        return 217;
    host.resizeWindow(beforeCustomRulesSize);
    QTest::qWait(40);
    if (!ruleEditor || !ruleScope || !confirmRules || !click(host, QStringLiteral("user_rule_add")))
        return 183;
    auto* ruleValue = host.window()->findChild<QObject*>("user_rule_value");
    if (!ruleValue || !ruleEditor->property("formOpen").toBool())
        return 184;
    if (!checkCustomRulesLayout(host, false))
        return 206;
    ruleValue->setProperty("text", "example.com");
    QMetaObject::invokeMethod(ruleEditor, "saveRuleForm");
    QTest::qWait(40);
    if (!ruleEditor->property("globalDirty").toBool() ||
        !confirmRules->property("enabled").toBool())
        return 185;
    ruleScope->setProperty("currentIndex", 1);
    QTest::qWait(30);
    if (!confirmRules->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(ruleEditor, "edit", Q_ARG(QVariant, QVariant(0))))
        return 186;
    auto* providerChoice = host.window()->findChild<QObject*>("user_rule_provider");
    if (!providerChoice || providerChoice->property("currentIndex").toInt() != 1)
        return 187;
    QMetaObject::invokeMethod(ruleEditor, "cancelForm");
    ui.applyControl(
        {{"publicationGeneration", "1"},
         {"epoch", "fixture"},
         {"ready", true},
         {"selectedId", "selected-fixture"},
         {"status", "Ready"},
         {"features", QJsonObject{{"userRuleBinding", QJsonObject{{"profileId", "other"},
                                                                  {"profileName", "Other"},
                                                                  {"groups", QJsonArray{}}}}}}});
    QTest::qWait(30);
    auto* bindingDialog = host.window()->findChild<QObject*>("user_rule_binding_dialog");
    auto* bindingGroup = host.window()->findChild<QObject*>("user_rule_binding_group");
    if (!bindingDialog || !bindingDialog->property("visible").toBool() || !bindingGroup ||
        bindingGroup->property("count").toInt() != 1)
        return 194;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"selectedId", "selected-fixture"},
                     {"status", "Ready"},
                     {"features", QJsonObject{}}});
    QTest::qWait(30);
    if (bindingDialog->property("visible").toBool())
        return 195;
    // Exercise changes while the dialog has not finished opening or closing.
    // Without a transition, opened changes synchronously and misses this race.
    const auto previousEnter = bindingDialog->property("enter");
    const auto previousExit = bindingDialog->property("exit");
    auto enterTransition = std::make_unique<QQmlComponent>(qmlEngine(bindingDialog));
    enterTransition->setData(
        "import QtQuick; Transition { NumberAnimation { property: \"opacity\"; "
        "from: 0; to: 1; duration: 250 } }",
        QUrl{});
    auto exitTransition = std::make_unique<QQmlComponent>(qmlEngine(bindingDialog));
    exitTransition->setData("import QtQuick; Transition { NumberAnimation { property: \"opacity\"; "
                            "from: 1; to: 0; duration: 250 } }",
                            QUrl{});
    auto* enter = enterTransition->create();
    auto* exit = exitTransition->create();
    if (enter)
        enter->setParent(bindingDialog);
    if (exit)
        exit->setParent(bindingDialog);
    if (!enter || !exit || !bindingDialog->setProperty("enter", QVariant::fromValue(enter)) ||
        !bindingDialog->setProperty("exit", QVariant::fromValue(exit)))
        return 226;
    const QJsonObject bindingActive{
        {"publicationGeneration", "1"},
        {"epoch", "fixture"},
        {"ready", true},
        {"features", QJsonObject{{"userRuleBinding", QJsonObject{{"profileId", "rapid"},
                                                                 {"profileName", "Rapid"},
                                                                 {"groups", QJsonArray{"one"}}}}}}};
    const QJsonObject bindingCleared{{"publicationGeneration", "1"},
                                     {"epoch", "fixture"},
                                     {"ready", true},
                                     {"features", QJsonObject{}}};
    ui.applyControl(bindingActive);
    if (!bindingDialog->property("visible").toBool() || bindingDialog->property("opened").toBool())
        return 227;
    ui.applyControl(bindingCleared);
    QTest::qWait(350);
    if (bindingDialog->property("visible").toBool())
        return 228;
    ui.applyControl(bindingActive);
    QTest::qWait(350);
    if (!bindingDialog->property("opened").toBool())
        return 229;
    ui.applyControl(bindingCleared);
    ui.applyControl(bindingActive);
    QTest::qWait(350);
    if (!bindingDialog->property("opened").toBool())
        return 230;
    ui.applyControl(bindingCleared);
    QTest::qWait(350);
    if (bindingDialog->property("visible").toBool())
        return 231;
    // Restore the production transitions for the remaining interaction checks.
    if (!bindingDialog->setProperty("enter", previousEnter) ||
        !bindingDialog->setProperty("exit", previousExit))
        return 232;
    enterTransition.reset();
    exitTransition.reset();
    ui.applyControl(
        {{"publicationGeneration", "1"},
         {"epoch", "fixture"},
         {"ready", true},
         {"features",
          QJsonObject{{"userRuleBinding", QJsonObject{{"profileId", "other"},
                                                      {"profileName", "Other"},
                                                      {"groups", QJsonArray{"one", "two"}}}}}}});
    QTest::qWait(30);
    bindingGroup = host.window()->findChild<QObject*>("user_rule_binding_group");
    QSignalSpy bindingAccepted(bindingDialog, SIGNAL(accepted()));
    if (!bindingDialog->property("visible").toBool() || !bindingGroup ||
        bindingGroup->property("count").toInt() != 3 ||
        !bindingGroup->setProperty("currentIndex", 2) ||
        bindingGroup->property("currentText").toString() != "two" ||
        !QMetaObject::invokeMethod(bindingDialog, "accept") || bindingAccepted.size() != 1)
        return 219;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"features", QJsonObject{}}});
    QTest::qWait(30);
    if (bindingDialog->property("visible").toBool())
        return 220;
    const auto oldSize = host.window()->size();
    host.resizeWindow({360, 640});
    QTest::qWait(50);
    if (!test::checkVisibleFocusFrames(host.window()))
        return 188;
    host.resizeWindow(oldSize);
    QTest::qWait(30);
    // A tray close now tears down the engine on the next event-loop turn. Exercise the
    // same boundary with both a staged rule and an unfinished rule form.
    if (!ruleScope->setProperty("currentIndex", 0) ||
        !QMetaObject::invokeMethod(ruleEditor, "edit", Q_ARG(QVariant, QVariant(-1))))
        return 235;
    ruleValue = host.window()->findChild<QObject*>("user_rule_value");
    if (!ruleValue || !ruleValue->setProperty("text", "unfinished.example") ||
        !ruleEditor->property("formOpen").toBool() || confirmRules->property("enabled").toBool())
        return 236;
    const auto rulesGeneration = host.engineGeneration();
    const QPointer<QQuickWindow> rulesWindow = host.window();
    host.unloadWindow();
    const auto savedRulesDraft = ui.userRulesDraft();
    const auto savedGlobalRules = savedRulesDraft.value("global").toList();
    const auto savedRuleForm = savedRulesDraft.value("form").toMap();
    if (!rulesWindow.isNull() || !savedRulesDraft.value("globalDirty").toBool() ||
        savedGlobalRules.size() != globalRules.size() + 1 ||
        savedGlobalRules.constLast().toMap().value("value") != "example.com" ||
        savedRuleForm.value("value") != "unfinished.example") {
        qWarning() << "Custom rule draft was not preserved through engine destruction:"
                   << savedRulesDraft;
        return 233;
    }
    if (!host.showWindow() || host.engineGeneration() != rulesGeneration + 1)
        return 234;
    QTest::qWait(40);
    if (!ui.navigate("home"))
        return 189;
    QTest::qWait(40);
    if (!ui.userRulesDraft().value("globalDirty").toBool() || !ui.navigate("rules"))
        return 190;
    QTest::qWait(50);
    section = host.window()->findChild<QObject*>(QStringLiteral("rules_section"));
    view = host.window()->findChild<QObject*>(QStringLiteral("runtime_view"));
    if (!section || !view)
        return 191;
    if (!click(host, QStringLiteral("open_custom_rules")))
        return 201;
    QTest::qWait(30);
    ui.applyData({1, "fixture", {{"user_rules", customRuleView}}, ui.dataDemands().generations});
    QTest::qWait(30);
    ruleEditor = host.window()->findChild<QObject*>("user_rules_editor");
    if (!ruleEditor || !ruleEditor->property("globalDirty").toBool())
        return 192;
    auto restoredDraft = ruleEditor->property("globalDraft");
    if (restoredDraft.metaType() == QMetaType::fromType<QJSValue>())
        restoredDraft = restoredDraft.value<QJSValue>().toVariant();
    if (restoredDraft.toList().size() != globalRules.size() + 1 ||
        restoredDraft.toList().last().toMap().value("value") != "example.com")
        return 197;
    ruleValue = host.window()->findChild<QObject*>("user_rule_value");
    if (!ruleEditor->property("formOpen").toBool() || !ruleValue ||
        ruleValue->property("text") != "unfinished.example" ||
        !QMetaObject::invokeMethod(ruleEditor, "cancelForm") ||
        !ruleEditor->property("globalDirty").toBool())
        return 237;

    // Leaving the page with its modal open must still retain the staged rule.
    if (!ui.navigate("home"))
        return 245;
    QTest::qWait(40);
    if (!ui.userRulesDraft().value("globalDirty").toBool() || !ui.navigate("rules"))
        return 246;
    QTest::qWait(50);
    if (!click(host, "open_custom_rules"))
        return 247;
    QTest::qWait(30);
    ui.applyData({1, "fixture", {{"user_rules", customRuleView}}, ui.dataDemands().generations});
    QTest::qWait(30);
    ruleEditor = host.window()->findChild<QObject*>("user_rules_editor");
    if (!ruleEditor || !ruleEditor->property("globalDirty").toBool() ||
        ruleEditor->property("formOpen").toBool())
        return 248;

    // Also restore a non-default profile rule form, including its provider and target.
    ruleScope = host.window()->findChild<QObject*>("user_rule_scope");
    if (!ruleScope || !ruleScope->setProperty("currentIndex", 1) ||
        !QMetaObject::invokeMethod(ruleEditor, "edit", Q_ARG(QVariant, QVariant(0))))
        return 238;
    auto* ruleType = host.window()->findChild<QObject*>("user_rule_type");
    providerChoice = host.window()->findChild<QObject*>("user_rule_provider");
    auto* targetChoice = host.window()->findChild<QObject*>("user_rule_target");
    auto* enabledChoice = host.window()->findChild<QObject*>("user_rule_form_enabled");
    if (!ruleType || ruleType->property("currentText") != "RULE-SET" || !providerChoice ||
        !providerChoice->setProperty("currentIndex", 0) || !targetChoice ||
        !targetChoice->setProperty("currentIndex", 2) || !enabledChoice ||
        !enabledChoice->setProperty("checked", false))
        return 239;
    const auto nextRulesGeneration = host.engineGeneration();
    const QPointer<QQuickWindow> secondRulesWindow = host.window();
    host.unloadWindow();
    const auto savedLocalForm = ui.userRulesDraft().value("form").toMap();
    if (!secondRulesWindow.isNull() || savedLocalForm.value("scope").toInt() != 1 ||
        savedLocalForm.value("index").toInt() != 0 || savedLocalForm.value("type") != "RULE-SET" ||
        savedLocalForm.value("provider") != "first" || savedLocalForm.value("target") != "Alpha" ||
        savedLocalForm.value("enabled").toBool()) {
        qWarning() << "Profile rule form was not preserved through engine destruction:"
                   << savedLocalForm;
        return 240;
    }
    if (!host.showWindow() || host.engineGeneration() != nextRulesGeneration + 1)
        return 241;
    QTest::qWait(40);
    if (!click(host, "open_custom_rules"))
        return 242;
    QTest::qWait(30);
    ui.applyData({1, "fixture", {{"user_rules", customRuleView}}, ui.dataDemands().generations});
    QTest::qWait(30);
    ruleEditor = host.window()->findChild<QObject*>("user_rules_editor");
    ruleScope = host.window()->findChild<QObject*>("user_rule_scope");
    ruleType = host.window()->findChild<QObject*>("user_rule_type");
    providerChoice = host.window()->findChild<QObject*>("user_rule_provider");
    targetChoice = host.window()->findChild<QObject*>("user_rule_target");
    enabledChoice = host.window()->findChild<QObject*>("user_rule_form_enabled");
    confirmRules = host.window()->findChild<QObject*>("confirm_custom_rules");
    if (!ruleEditor || !ruleEditor->property("formOpen").toBool() ||
        ruleEditor->property("editingIndex").toInt() != 0 ||
        !ruleEditor->property("globalDirty").toBool() || !ruleScope ||
        ruleScope->property("currentIndex").toInt() != 1 || !ruleType ||
        ruleType->property("currentText") != "RULE-SET" || !providerChoice ||
        providerChoice->property("currentText") != "first" || !targetChoice ||
        targetChoice->property("currentText") != "Alpha" || !enabledChoice ||
        enabledChoice->property("checked").toBool() || !confirmRules ||
        confirmRules->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(ruleEditor, "cancelForm"))
        return 243;

    // A save can finish after the window is destroyed. Reattach to its result
    // without resubmitting, and preserve the draft if the operation fails.
    if (!QMetaObject::invokeMethod(ruleEditor, "confirm") ||
        !ruleEditor->property("saving").toBool())
        return 249;
    const auto firstAttempt = ruleEditor->property("savingAttempt").toString();
    const QPointer<QQuickWindow> failedSaveWindow = host.window();
    host.unloadWindow();
    auto pendingSave = ui.userRulesDraft();
    if (!failedSaveWindow.isNull() || !pendingSave.value("saving").toBool() ||
        pendingSave.value("savingAttempt").toString() != firstAttempt ||
        !pendingSave.value("globalDirty").toBool())
        return 250;
    if (!host.showWindow())
        return 251;
    QTest::qWait(40);
    if (!click(host, "open_custom_rules"))
        return 251;
    QTest::qWait(30);
    auto failedRuleView = customRuleView;
    failedRuleView.insert("attempt", "1");
    failedRuleView.insert("result", "Failed");
    ui.applyData({1, "fixture", {{"user_rules", failedRuleView}}, ui.dataDemands().generations});
    QTest::qWait(30);
    ruleEditor = host.window()->findChild<QObject*>("user_rules_editor");
    confirmRules = host.window()->findChild<QObject*>("confirm_custom_rules");
    customRulesDialog = host.window()->findChild<QObject*>("custom_rules_dialog");
    if (!ruleEditor || ruleEditor->property("saving").toBool() ||
        !ruleEditor->property("globalDirty").toBool() || !confirmRules ||
        !confirmRules->property("enabled").toBool() || !customRulesDialog ||
        !customRulesDialog->property("visible").toBool())
        return 252;
    if (!QMetaObject::invokeMethod(ruleEditor, "confirm") ||
        !ruleEditor->property("saving").toBool() || ruleEditor->property("savingAttempt") != "1")
        return 253;
    const QPointer<QQuickWindow> savedWindow = host.window();
    host.unloadWindow();
    pendingSave = ui.userRulesDraft();
    if (!savedWindow.isNull() || !pendingSave.value("saving").toBool() ||
        pendingSave.value("savingAttempt") != "1" || !pendingSave.value("globalDirty").toBool() ||
        !host.showWindow())
        return 254;
    QTest::qWait(40);
    if (!click(host, "open_custom_rules"))
        return 254;
    QTest::qWait(30);
    ruleEditor = host.window()->findChild<QObject*>("user_rules_editor");
    customRulesDialog = host.window()->findChild<QObject*>("custom_rules_dialog");
    if (!ruleEditor || !customRulesDialog)
        return 255;
    auto completedRuleView = customRuleView;
    completedRuleView.insert("global",
                             QJsonArray::fromVariantList(pendingSave.value("global").toList()));
    completedRuleView.insert("revision", "revision-2");
    completedRuleView.insert("attempt", "2");
    completedRuleView.insert("result", "Saved");
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"busy", true},
                     {"selectedId", "selected-fixture"}});
    ui.applyData({1, "fixture", {{"user_rules", completedRuleView}}, ui.dataDemands().generations});
    if (!ruleEditor->property("saving").toBool())
        return 255;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"busy", false},
                     {"selectedId", "selected-fixture"}});
    QTest::qWait(50);
    if (ruleEditor->property("saving").toBool() || ruleEditor->property("dirty").toBool() ||
        ruleEditor->property("baseRevision") != "revision-2" || !ui.userRulesDraft().isEmpty() ||
        customRulesDialog->property("visible").toBool())
        return 255;
    section = host.window()->findChild<QObject*>(QStringLiteral("rules_section"));
    view = host.window()->findChild<QObject*>(QStringLiteral("runtime_view"));
    if (!section || !view)
        return 244;
    section->setProperty("currentIndex", 1);
    QTest::qWait(30);
    if (ui.liveRules()->count() != 0 || ui.dataDemands().active.contains("rules") ||
        !ui.dataDemands().active.contains("providers") || ui.telemetry().contains("rulePage"))
        return 60;
    ui.applyData(oldRules);
    const application::RuntimeUpdate oldProviders{
        1,
        "fixture",
        {{"providers",
          QJsonArray{QJsonObject{{"name", "fixture"}, {"kind", "rules"}, {"count", 2}}}}},
        ui.dataDemands().generations};
    ui.applyData(oldProviders);
    if (view->property("count").toInt() != 1)
        return 49;
    if (!checkHorizontalFocus())
        return 88;
    ui.applyData(
        {1,
         "fixture",
         {{"provider_status",
           QJsonObject{{"providerPage",
                        QJsonObject{{"stale", true}, {"proxyError", "CoreRequestTimeout"}}}}}},
         ui.dataDemands().generations});
    auto* ruleStatus = host.window()->findChild<QObject*>(QStringLiteral("rules_status"));
    if (!ruleStatus || !ruleStatus->property("text").toString().contains("CoreRequestTimeout") ||
        ui.liveProviders()->count() != 1)
        return 82;
    ui.setUiVisible(false);
    ui.applyData(oldProviders);
    if (ui.liveProviders()->count() != 0 || ui.activeDemandCount() != 0)
        return 61;
    ui.setUiVisible(true);
    QTest::qWait(30);
    ui.applyData(oldProviders);
    if (ui.liveProviders()->count() != 0)
        return 62;
    auto currentProviders = oldProviders;
    currentProviders.demandGenerations = ui.dataDemands().generations;
    ui.applyData(currentProviders);
    if (ui.liveProviders()->count() != 1)
        return 63;
    section->setProperty("currentIndex", 0);
    QTest::qWait(30);
    ui.applyData(oldRules);
    ui.applyData(oldProviders);
    if (ui.liveRules()->count() != 0 || ui.liveProviders()->count() != 0 ||
        ui.dataDemands().active.contains("providers"))
        return 70;
    auto currentRules = oldRules;
    currentRules.demandGenerations = ui.dataDemands().generations;
    ui.applyData(currentRules);
    if (ui.liveRules()->count() != 2)
        return 71;
    QJsonArray logs;
    for (int i = 1; i <= 100; ++i)
        logs.append(QJsonObject{{"localSeq", QString::number(i)},
                                {"time", "now"},
                                {"source", "Core"},
                                {"level", "info"},
                                {"message", "fixture"}});
    if (!ui.navigate("logs"))
        return 50;
    QTest::qWait(60);
    if (ui.liveProviders()->count() != 0 || ui.dataDemands().active.contains("providers") ||
        ui.liveRules()->count() != 0 || ui.dataDemands().active.contains("rules"))
        return 64;
    ui.applyData({1, "fixture", {{"logs", logs}}, ui.dataDemands().generations});
    QTest::qWait(30);
    view = host.window()->findChild<QObject*>(QStringLiteral("runtime_view"));
    if (!view || !view->property("atYEnd").toBool())
        return 51;
    if (!click(host, QStringLiteral("pause_logs")))
        return 52;
    if (!ui.dataDemands().active.contains("logs") || ui.dataDemands().active.contains("log_feed"))
        return 65;
    logs.append(QJsonObject{
        {"localSeq", "101"}, {"source", "Core"}, {"level", "info"}, {"message", "later"}});
    ui.applyData({1, "fixture", {{"logs", logs}}, ui.dataDemands().generations});
    if (ui.liveLogs()->count() != 100)
        return 53;
    if (!click(host, QStringLiteral("pause_logs")) || ui.liveLogs()->count() != 101)
        return 54;
    QTest::qWait(30);
    if (!view->property("atYEnd").toBool())
        return 55;
    if (!click(host, QStringLiteral("preview_log_export")))
        return 84;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"status", "Ready"},
                     {"features", QJsonObject{{"logExportPreview",
                                               QJsonObject{{"token", "preview-token"},
                                                           {"messagesHidden", true},
                                                           {"text", "frozen redacted logs"}}}}}});
    QTest::qWait(30);
    auto* logDialog = host.window()->findChild<QObject*>("log_export_dialog");
    auto* logPreview = host.window()->findChild<QObject*>("log_export_preview");
    auto* sourceFilter = host.window()->findChild<QObject*>("log_source_filter");
    if (!logDialog || !logDialog->property("visible").toBool() || !logPreview ||
        logPreview->property("text") != "frozen redacted logs" || !sourceFilter ||
        sourceFilter->property("count").toInt() != 4)
        return 85;
    auto* hideMessages = host.window()->findChild<QObject*>("log_export_hide_messages");
    auto* logPage = host.window()->findChild<QObject*>("logs_page");
    if (!hideMessages || !logPage || !logPage->property("exportReady").toBool() ||
        !hideMessages->setProperty("checked", false) ||
        !QMetaObject::invokeMethod(hideMessages, "toggled") ||
        logPage->property("exportReady").toBool())
        return 87;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"status", "Ready"},
                     {"features", QJsonObject{{"logExportPreview",
                                               QJsonObject{{"token", "new-preview-token"},
                                                           {"messagesHidden", false},
                                                           {"text", "same frozen range"}}}}}});
    if (!logPage->property("exportReady").toBool())
        return 88;
    QMetaObject::invokeMethod(logDialog, "reject");
    // Moving away from the tail changes following only; reception remains active.
    if (!QMetaObject::invokeMethod(view, "movementStarted") ||
        !ui.dataDemands().active.contains("log_feed"))
        return 86;
    if (!click(host, QStringLiteral("pause_logs")))
        return 66;
    const application::RuntimeUpdate oldLogs{
        1, "fixture", {{"logs", logs}}, ui.dataDemands().generations};
    host.unloadWindow();
    ui.applyData(oldLogs);
    if (ui.liveLogs()->count() != 0 || ui.activeDemandCount() != 0 ||
        ui.diagnosticSnapshot()
                .value("dataLifecycle")
                .toObject()
                .value("pendingLogCount")
                .toInt() != 0)
        return 67;
    // Filter state intentionally survives page destruction; isolate the epoch assertion.
    ui.liveConnections()->setQuery({});
    if (!host.showWindow())
        return 57;
    QTest::qWait(50);
    if (!ui.navigate("connections"))
        return 68;
    QTest::qWait(60);
    ui.applyData(oldConnections);
    if (ui.liveConnections()->count() != 0)
        return 69;
    ui.applyData({2,
                  "next",
                  {{"connections", QJsonArray{QJsonObject{{"id", "next"}, {"epoch", "next"}}}}},
                  ui.dataDemands().generations});
    ui.applyData(oldConnections);
    if (ui.liveConnections()->captureIds() != QStringList{"next"})
        return 56;
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"selectedId", "proxy_fixture"},
                     {"outbound", QJsonObject{{"policy", "manual"}}}});
    ui.saveState("proxies:proxy_fixture", {{"groups", QStringList{}}});
    if (!ui.navigate("proxies"))
        return 72;
    QTest::qWait(60);
    auto* loader = host.window()->findChild<QObject*>("active_page_host");
    auto* proxies = loader ? loader->property("item").value<QObject*>() : nullptr;
    if (!proxies ||
        !proxies->property("expandedGroups").value<QJSValue>().toVariant().toList().isEmpty())
        return 73;
    QJsonArray members, nodes;
    for (int i = 0; i < 500; ++i) {
        const auto name = QStringLiteral("node_%1").arg(i);
        members.append(name);
        nodes.append(QJsonObject{{"name", name}, {"type", "HTTP"}});
    }
    ui.applyData({2,
                  "next",
                  {{"groups", QJsonArray{QJsonObject{{"name", "manual"},
                                                     {"type", "Selector"},
                                                     {"canSelect", true},
                                                     {"members", members}}}},
                   {"nodes", nodes},
                   {"delays", QJsonObject{{"node_0", "Timeout"}}}},
                  ui.dataDemands().generations});
    proxies->setProperty("expandedGroups", QStringList{"manual"});
    QTest::qWait(60);
    // Count visual children too: Repeater ownership differs from visual parenting.
    const auto controls = [&](auto&& self, QQuickItem* item) -> int {
        int count = item->objectName() == QStringLiteral("test_node") ? 1 : 0;
        for (auto* child : item->childItems())
            count += self(self, child);
        return count;
    };
    const auto instantiated = controls(controls, host.window()->contentItem());
    if (instantiated <= 0 || instantiated > 80)
        return 74;
    auto* firstMember = findVisualItem(host.window()->contentItem(), "proxy_member_manual_node_0");
    auto* firstDelay = firstMember ? firstMember->findChild<QObject*>("test_node") : nullptr;
    if (!firstDelay ||
        firstDelay->property("text") != ui.proxyState("node_0", "manual").value("text"))
        return 221;
    ui.applyData(
        {2, "next", {{"delays", QJsonObject{{"node_0", "42 ms"}}}}, ui.dataDemands().generations});
    QTest::qWait(30);
    if (firstDelay->property("text") != "42 ms")
        return 222;
    const auto findGrid = [&](auto&& self, QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == QStringLiteral("proxy_node_grid"))
            return item;
        for (auto* child : item->childItems())
            if (auto* found = self(self, child))
                return found;
        return nullptr;
    };
    auto* grid = findGrid(findGrid, host.window()->contentItem());
    if (!grid || !QMetaObject::invokeMethod(grid, "focusMember", Q_ARG(QVariant, 499)))
        return 75;
    QTest::qWait(60);
    if (grid->property("endIndex").toInt() != 500 ||
        controls(controls, host.window()->contentItem()) > 80)
        return 76;
    proxies->setProperty("expandedGroups", QStringList{});
    QTest::qWait(30);
    if (controls(controls, host.window()->contentItem()) != 0)
        return 78;
    if (!ui.navigate("settings"))
        return 81;
    const QJsonObject savedSettings{
        {"mode", "rule"}, {"mixedPort", 7890}, {"controllerPort", 9090}, {"dnsMode", "profile"}};
    auto requestedSettings = savedSettings;
    requestedSettings.insert("mixedPort", 7891);
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"ready", true},
                     {"selectionGeneration", "7"},
                     {"preferences", requestedSettings},
                     {"settingsState", QJsonObject{{"saved", savedSettings},
                                                   {"requested", requestedSettings},
                                                   {"effective", savedSettings},
                                                   {"running", true},
                                                   {"pending", true}}}});
    QTest::qWait(50);
    auto* settingsPage = host.window()->findChild<QObject*>("settings_page");
    auto* settingsText = host.window()->findChild<QObject*>("network_settings_state");
    if (!settingsPage || !settingsText ||
        !settingsText->property("text").toString().contains("7891") ||
        !settingsText->property("text").toString().contains("7890") ||
        !QMetaObject::invokeMethod(settingsPage, "requestNetworkSettings"))
        return 82;
    auto* settingsDialog = host.window()->findChild<QObject*>("network_settings_confirmation");
    auto pending = settingsPage->property("pendingNetworkArgs");
    if (pending.metaType() == QMetaType::fromType<QJSValue>())
        pending = pending.value<QJSValue>().toVariant();
    const auto capturedSettings = pending.toList();
    if (!settingsDialog || !settingsDialog->property("visible").toBool() ||
        capturedSettings.size() != 6 || capturedSettings[1] != "7891" ||
        capturedSettings[4] != "next" || capturedSettings[5] != "7")
        return 83;
    QMetaObject::invokeMethod(settingsDialog, "reject");
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"listenerPortCandidate",
                                               QJsonObject{{"token", "port_preview"},
                                                           {"mixedPort", 7893},
                                                           {"controllerPort", 9092}}}}}});
    QTest::qWait(40);
    pending = settingsPage->property("pendingNetworkArgs");
    if (pending.metaType() == QMetaType::fromType<QJSValue>())
        pending = pending.value<QJSValue>().toVariant();
    if (!settingsDialog->property("visible").toBool() || pending.toList().value(1) != "7893" ||
        pending.toList().value(2) != "9092" || pending.toList().value(4) != "next")
        return 181;
    QMetaObject::invokeMethod(settingsDialog, "reject");
    const QJsonObject maintenancePreview{
        {"token", "fixed-support"}, {"kind", "diagnostics"}, {"text", "{\"schemaVersion\":1}"}};
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"maintenancePreview", maintenancePreview}}}});
    QTest::qWait(40);
    auto* maintenanceDialog = host.window()->findChild<QObject*>("maintenance_confirmation");
    if (!maintenanceDialog || !maintenanceDialog->property("visible").toBool())
        return 139;
    host.window()->resize(360, 480);
    QTest::qWait(40);
    if (maintenanceDialog->property("width").toReal() > 328 ||
        maintenanceDialog->property("height").toReal() > 448)
        return 140;
    ui.setUiVisible(false);
    if (maintenanceDialog->property("visible").toBool())
        return 141;
    ui.setUiVisible(true);
    ui.applyControl(
        {{"publicationGeneration", "2"}, {"epoch", "next"}, {"features", QJsonObject{}}});
    const QJsonObject backupPreview{
        {"token", "fixed-preview"},
        {"scope", "source"},
        {"decodedBytes", "1234"},
        {"profiles", QJsonArray{QJsonObject{
                         {"name", QString(80, 'x')}, {"snapshot", true}, {"overrides", true}}}}};
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"backupPreview", backupPreview}}}});
    QTest::qWait(40);
    auto* backupDialog = host.window()->findChild<QObject*>("backup_restore_confirmation");
    if (!backupDialog || !backupDialog->property("visible").toBool())
        return 84;
    host.window()->resize(360, 480);
    QTest::qWait(40);
    if (backupDialog->property("width").toReal() > 328 ||
        backupDialog->property("height").toReal() > 448)
        return 85;
    ui.setUiVisible(false);
    if (backupDialog->property("visible").toBool())
        return 86;
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"backupPreview", backupPreview}}}});
    QTest::qWait(20);
    if (backupDialog->property("visible").toBool())
        return 87;
    ui.setUiVisible(true);
    ui.applyControl(
        {{"publicationGeneration", "2"}, {"epoch", "next"}, {"features", QJsonObject{}}});
    const QJsonObject globalOnlyPreview{{"token", "global-only"},
                                        {"scope", "offline"},
                                        {"decodedBytes", "100"},
                                        {"profiles", QJsonArray{}},
                                        {"hasGlobalRules", true}};
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"backupPreview", globalOnlyPreview}}}});
    QTest::qWait(40);
    auto* restoreGlobal = host.window()->findChild<QQuickItem*>("backup_restore_global_rules");
    auto* restoreAccept = host.window()->findChild<QQuickItem*>("backup_restore_accept");
    if (!backupDialog->property("visible").toBool() ||
        !backupDialog->property("globalOnly").toBool() || !restoreGlobal || !restoreAccept ||
        restoreGlobal->property("checked").toBool() || restoreAccept->isEnabled())
        return 193;
    restoreGlobal->setProperty("checked", true);
    QTest::qWait(20);
    if (!restoreAccept->isEnabled())
        return 198;
    QMetaObject::invokeMethod(backupDialog, "reject");
    QTest::qWait(150);
    if (host.qmlErrorCount() != 0 || !host.contentReady())
        return 58;
    // A binding may already be pending when an unloaded window is rebuilt.
    host.unloadWindow();
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"userRuleBinding",
                                               QJsonObject{{"profileId", "restore"},
                                                           {"profileName", "Restored"},
                                                           {"groups", QJsonArray{"one"}}}}}}});
    if (!host.showWindow())
        return 223;
    QTest::qWait(60);
    auto* restoredBinding = host.window()->findChild<QObject*>("user_rule_binding_dialog");
    if (!restoredBinding || !restoredBinding->property("visible").toBool())
        return 224;
    ui.applyControl(
        {{"publicationGeneration", "2"}, {"epoch", "next"}, {"features", QJsonObject{}}});
    QTest::qWait(30);
    if (restoredBinding->property("visible").toBool() || host.qmlErrorCount() != 0)
        return 225;
    std::puts("Runtime lists: leases, tab/hide/destruction release, late delivery, captured close, "
              "log pause/follow, empty expansion and 500-node viewport passed");
    return 0;
}
int runPageMemoryProbe(ApplicationHost& host) {
#ifndef Q_OS_WIN
    Q_UNUSED(host)
    return 77;
#else
    // Isolate Qt/QML from subscription IO, SettingsService, core and platform actions.
    // Keep the same fonts, icons, window and fixture for hardware/software comparisons.
    // A helper launched with SW_HIDE must transition visibility again before Windows can
    // expose it; otherwise a hidden scene graph is not representative of the foreground.
    host.window()->hide();
    host.window()->show();
    if (!QTest::qWaitForWindowExposed(host.window(), 5000))
        return 119;
    int fixtureNodes = 500;
    const auto arguments = QCoreApplication::arguments();
    for (const auto& argument : arguments) {
        if (argument.startsWith(QStringLiteral("--page-memory-nodes="))) {
            bool valid = false;
            const auto requested = argument.mid(20).toInt(&valid);
            if (!valid || requested < 1 || requested > 5000)
                return 120;
            fixtureNodes = requested;
        }
    }
    struct Counters : PROCESS_MEMORY_COUNTERS_EX {
        SIZE_T privateWorkingSetSize = 0;
        ULONGLONG sharedCommitUsage = 0;
    };
    struct Sample {
        quint64 privateWorkingSet = 0, workingSet = 0, privateCommit = 0, cpu100ns = 0;
        QJsonObject json() const {
            return {{"privateWorkingSet", QString::number(privateWorkingSet)},
                    {"workingSet", QString::number(workingSet)},
                    {"privateCommit", QString::number(privateCommit)}};
        }
    };
    const auto read = [](Sample& sample) {
        Counters counters;
        counters.cb = sizeof(counters);
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessMemoryInfo(GetCurrentProcess(),
                                  reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                  sizeof(counters)) ||
            !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            return false;
        const auto ticks = [](FILETIME time) {
            return (static_cast<quint64>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        };
        sample = {counters.privateWorkingSetSize, counters.WorkingSetSize, counters.PrivateUsage,
                  ticks(kernel) + ticks(user)};
        return true;
    };
    const auto page = [&host] {
        auto* loader = host.window()->findChild<QObject*>(QStringLiteral("active_page_host"));
        return loader ? loader->property("item").value<QObject*>() : nullptr;
    };
    // Repeater delegates may have a visual parent different from their QObject parent.
    const auto objects = [](QObject* root) {
        QSet<QObject*> found;
        QList<QObject*> pending{root};
        while (!pending.isEmpty()) {
            auto* object = pending.takeLast();
            if (!object || found.contains(object))
                continue;
            found.insert(object);
            pending.append(object->children());
            if (auto* item = qobject_cast<QQuickItem*>(object))
                for (auto* child : item->childItems())
                    pending.append(child);
        }
        return found;
    };
    const auto nodeControls = [&] {
        int count = 0;
        for (auto* object : objects(host.window()))
            if (object->objectName() == QStringLiteral("test_node"))
                ++count;
        return count;
    };
    QElapsedTimer elapsed;
    elapsed.start();
    // Diagnostic-only smooth scroll: a manual run can set QUICKCLASH_PROBE_SCROLL=smooth
    // to separate per-frame rendering cost from the row-jump cost of the default probe.
    const bool smoothScroll = qEnvironmentVariable("QUICKCLASH_PROBE_SCROLL") == "smooth";
    const bool traceScroll = qEnvironmentVariableIsSet("QUICKCLASH_PROBE_TRACE");
    const auto stage = [&](const char* name, int milliseconds, bool scroll = false) {
        Sample before, endpoint, peak;
        if (!read(before))
            return false;
        QElapsedTimer clock;
        clock.start();
        int samples = 0;
        QElapsedTimer frameClock;
        frameClock.start();
        QVector<qint64> frameTimes;
        const auto frameConnection =
            QObject::connect(host.window(), &QQuickWindow::frameSwapped, host.window(),
                             [&] { frameTimes.append(frameClock.nsecsElapsed()); });
        host.window()->requestUpdate();
        const int smoothMilliseconds = qEnvironmentVariableIntValue("QUICKCLASH_PROBE_SCROLL_MS");
        const int duration =
            scroll && smoothScroll
                ? std::max(milliseconds, smoothMilliseconds > 0 ? smoothMilliseconds : 6000)
                : milliseconds;
        qint64 nextTrace = 0;
        qint64 nextSample = 0;
        while (clock.elapsed() < duration) {
            if (scroll && page()) {
                auto* target =
                    host.window()->findChild<QQuickItem*>(QStringLiteral("proxy_group_list"));
                if (!target)
                    for (auto* object : objects(page())) {
                        auto* candidate = qobject_cast<QQuickItem*>(object);
                        if (candidate && candidate->property("contentY").isValid()) {
                            target = candidate;
                            break;
                        }
                    }
                if (target) {
                    const auto extent = std::max(0.0, target->property("contentHeight").toDouble() -
                                                          target->height());
                    if (smoothScroll) {
                        const double phase = std::fmod(clock.elapsed() / 6000.0, 2.0);
                        target->setProperty("contentY",
                                            extent * (phase < 1.0 ? phase : 2.0 - phase));
                    } else {
                        target->setProperty("contentY", extent * ((clock.elapsed() / 400) % 5) / 4);
                    }
                }
            }
            QTest::qWait(scroll ? 4 : 20);
            if (scroll && clock.elapsed() < nextSample)
                continue;
            nextSample = clock.elapsed() + 20;
            if (!read(endpoint))
                return false;
            if (traceScroll && scroll && clock.elapsed() >= nextTrace) {
                std::printf("Page memory trace: stage=%s elapsedMs=%lld pws=%llu commit=%llu\n",
                            name, static_cast<long long>(clock.elapsed()),
                            static_cast<unsigned long long>(endpoint.privateWorkingSet),
                            static_cast<unsigned long long>(endpoint.privateCommit));
                nextTrace = clock.elapsed() + 2000;
            }
            peak.privateWorkingSet = std::max(peak.privateWorkingSet, endpoint.privateWorkingSet);
            peak.workingSet = std::max(peak.workingSet, endpoint.workingSet);
            peak.privateCommit = std::max(peak.privateCommit, endpoint.privateCommit);
            ++samples;
        }
        QObject::disconnect(frameConnection);
        QJsonObject frameStats{{"swapped", static_cast<int>(frameTimes.size())}};
        if (frameTimes.size() > 1) {
            QVector<qint64> intervals;
            intervals.reserve(frameTimes.size() - 1);
            for (qsizetype i = 1; i < frameTimes.size(); ++i)
                intervals.append(frameTimes[i] - frameTimes[i - 1]);
            std::sort(intervals.begin(), intervals.end());
            const auto percentile = [&](double fraction) {
                return intervals[std::min(intervals.size() - 1,
                                          static_cast<qsizetype>(intervals.size() * fraction))] /
                       1.0e6;
            };
            const double median = percentile(0.5);
            int slowFrames = 0;
            for (const auto interval : intervals)
                slowFrames += interval > std::max<qint64>(1, static_cast<qint64>(median * 1.75e6));
            frameStats.insert("count", static_cast<int>(intervals.size()));
            frameStats.insert("medianMs", median);
            frameStats.insert("p95Ms", percentile(0.95));
            frameStats.insert("maxMs", static_cast<double>(intervals.last()) / 1.0e6);
            frameStats.insert("slowFrames", slowFrames);
        }
        const QJsonObject result{
            {"stage", QString::fromLatin1(name)},
            {"elapsedMs", elapsed.elapsed()},
            {"stageMs", clock.elapsed()},
            {"cpuMs", static_cast<double>(endpoint.cpu100ns - before.cpu100ns) / 10000},
            {"samples", samples},
            {"endpoint", endpoint.json()},
            {"sampledPeak", peak.json()},
            {"windowObjects", objects(host.window()).size()},
            {"pageObjects", objects(page()).size()},
            {"nodeControls", nodeControls()},
            {"exposed", host.window()->isExposed()},
            {"visible", host.window()->isVisible()},
            {"frames", frameStats},
            {"modelGroups", host.state().groups().size()},
            {"modelNodes", host.state().nodes().size()}};
        std::printf("Page memory: %s\n",
                    QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
        return host.contentReady() && host.qmlErrorCount() == 0;
    };
    auto& ui = host.state();
    QTest::qWait(600);
    const QJsonObject environment{
        {"qt", QString::fromLatin1(qVersion())},
        {"graphicsApi", static_cast<int>(host.window()->rendererInterface()->graphicsApi())},
        {"font", QFontInfo(QGuiApplication::font()).family()},
        {"width", host.window()->width()},
        {"height", host.window()->height()},
        {"dpr", host.window()->devicePixelRatio()},
        {"nodes", fixtureNodes},
        {"groups", 3}};
    std::printf("Page environment: %s\n",
                QJsonDocument(environment).toJson(QJsonDocument::Compact).constData());
    if (!stage("startup_home", 1600))
        return 100;
    QJsonArray members, nodes, groups;
    for (int i = 0; i < fixtureNodes; ++i) {
        const auto name = QStringLiteral("日本高速%1|CTCU|0.5x").arg(i, 2, 10, QLatin1Char('0'));
        members.append(name);
        nodes.append(QJsonObject{{"name", name}, {"type", "HTTP"}, {"udp", true}});
    }
    QStringList groupNames;
    for (int i = 0; i < 3; ++i) {
        const auto name = QStringLiteral("代理组%1").arg(i);
        groupNames.append(name);
        groups.append(QJsonObject{{"name", name},
                                  {"type", "Selector"},
                                  {"members", members},
                                  {"current", members.first()},
                                  {"canSelect", true}});
    }
    ui.applyControl(
        {{"publicationGeneration", "1"},
         {"epoch", "fixture"},
         {"ready", true},
         {"status", "Ready"},
         {"selectedId", "page_memory"},
         {"outbound", QJsonObject{{"policy", groupNames.first()}, {"node", members.first()}}}});
    ui.applyData(
        {1,
         "fixture",
         {{"groups", groups},
          {"nodes", nodes},
          {"profiles", QJsonArray{QJsonObject{{"id", "page_memory"}, {"name", "公开切页夹具"}}}}},
         ui.dataDemands().generations});
    if (arguments.contains(QStringLiteral("--page-memory-home-only"))) {
        if (!stage("home_with_nodes", 2500))
            return 121;
        auto* card = host.window()->findChild<QQuickItem*>(QStringLiteral("home_proxy_card"));
        auto* viewport = card ? card->parentItem() : nullptr;
        while (viewport && !viewport->inherits("QQuickFlickable"))
            viewport = viewport->parentItem();
        auto* selector = host.window()->findChild<QObject*>(QStringLiteral("home_proxy_selector"));
        auto* popup = selector ? selector->property("popup").value<QObject*>() : nullptr;
        if (!card || !viewport || !popup)
            return 122;
        viewport->setProperty("contentY", card->y());
        QTest::qWait(100);
        if (!QMetaObject::invokeMethod(popup, "open") || !stage("home_popup_open", 1800))
            return 123;
        if (!QMetaObject::invokeMethod(popup, "close") || !stage("home_popup_closed", 1800))
            return 124;
        return 0;
    }
    const QPointer<QObject> firstHome = page();
    if (!ui.navigate("proxies") || !stage("proxies_one_group", 2200, true))
        return 101;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!firstHome.isNull() || nodeControls() <= 0 || nodeControls() > 80)
        return 102;
    const QPointer<QObject> firstProxies = page();
    if (!page() || !page()->setProperty("expandedGroups", groupNames) ||
        !stage("proxies_three_groups", 2200, true) || nodeControls() <= 0 || nodeControls() > 80)
        return 103;
    if (!ui.navigate("subscriptions") || !stage("subscriptions", 2200))
        return 104;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!firstProxies.isNull() || nodeControls() != 0)
        return 105;
    auto* editor = host.window()->findChild<QObject*>(QStringLiteral("profile_editor"));
    if (!editor || !QMetaObject::invokeMethod(editor, "edit", Q_ARG(QVariant, "page_memory"),
                                              Q_ARG(QVariant, "source")))
        return 117;
    ui.applyData({1,
                  "fixture",
                  {{"editor", QJsonObject{{"id", "page_memory"},
                                          {"kind", "source"},
                                          {"token", "memory_editor"},
                                          {"text", QString(512 * 1024, QLatin1Char('#'))},
                                          {"name", "Public editor fixture"}}}},
                  ui.dataDemands().generations});
    if (!stage("editor_512k", 2200) || ui.editor().isEmpty())
        return 118;
    QMetaObject::invokeMethod(editor, "close");
    if (!stage("editor_released", 500) || !ui.editor().isEmpty())
        return 119;
    const QPointer<QObject> subscriptions = page();
    if (!ui.navigate("home") || !stage("home_after_pages", 3000))
        return 106;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!subscriptions.isNull())
        return 107;
    if (!ui.navigate("connections"))
        return 109;
    QJsonArray connectionRows;
    for (int i = 0; i < 1000; ++i)
        connectionRows.append(entry(QString::number(i), QStringLiteral("host%1.example").arg(i)));
    ui.applyData({1, "fixture", {{"connections", connectionRows}}, ui.dataDemands().generations});
    if (!stage("connections_1000", 2200, true) || ui.liveConnections()->count() != 1000)
        return 110;
    const QPointer<QObject> connections = page();
    if (!ui.navigate("home"))
        return 111;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!connections.isNull() || ui.liveConnections()->count() != 0)
        return 112;
    if (!ui.navigate("logs"))
        return 113;
    QJsonArray logRows;
    for (int i = 0; i < 2000; ++i)
        logRows.append(
            QJsonObject{{"localSeq", QString::number(i)},
                        {"time", "2026-10-28T00:00:00Z"},
                        {"source", "Core"},
                        {"level", "info"},
                        {"message", QStringLiteral("公开日志夹具%1 example.test").arg(i)}});
    ui.applyData({1, "fixture", {{"logs", logRows}}, ui.dataDemands().generations});
    if (!stage("logs_2000", 2200, true) || ui.liveLogs()->count() != 2000)
        return 114;
    const QPointer<QObject> logs = page();
    if (!ui.navigate("home"))
        return 115;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!logs.isNull() || ui.liveLogs()->count() != 0)
        return 116;
    if (!ui.navigate("proxies") || !stage("proxies_reentry", 1800, true) || !ui.navigate("home") ||
        !stage("home_after_reentry", 2200))
        return 108;
    std::puts("Page memory: old pages destroyed, node controls released, no core or settings IO");
    return 0;
#endif
}
} // namespace quickclash
