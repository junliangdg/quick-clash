#include "runtime_ui_probe.h"
#include "application_host.h"
#include "focus_layout_checks.h"
#include <QDir>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

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
bool waitForFilter(presentation::RuntimeListModel* model, const QString& query, int count) {
    if (QTest::qWaitFor([&] { return model->query() == query && model->count() == count; }, 1500))
        return true;
    qWarning() << "Filter did not settle: expected" << query << count << "actual" << model->query()
               << model->count();
    return false;
}
bool waitForClickable(ApplicationHost& host, const QString& name) {
    auto* item = host.window()->findChild<QQuickItem*>(name);
    if (!item)
        item = findVisualItem(host.window()->contentItem(), name);
    if (!item)
        return false;
    const bool ready = QTest::qWaitFor(
        [&] {
            return item->isVisible() && item->isEnabled() && item->width() > 0 &&
                   item->height() > 0;
        },
        1500);
    if (!ready) {
        qWarning() << "Control did not become clickable:" << name << item->isVisible()
                   << item->isEnabled() << item->size();
        return false;
    }
    // A newly visible RowLayout child can retain its previous non-zero geometry
    // until the next polish; deliver the frame before using coordinates for a click.
    host.window()->requestUpdate();
    if (host.window()->grabWindow().isNull()) {
        qWarning() << "Could not render clickable control:" << name;
        return false;
    }
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
    // Native layout rounds to physical pixels; accept at most one pixel of edge alignment.
    const auto edgeTolerance = std::max(0.5, 1.0 / host.window()->devicePixelRatio());
    if (!fits(title) || !fits(hint) || rect(title).bottom() > rect(editor).top() + 0.5 ||
        rect(editor).bottom() > rect(hint).top() + 0.5 ||
        rect(editor).bottom() > rect(cancel).top() + 0.5 || rect(hint).intersects(rect(cancel)) ||
        rect(hint).intersects(rect(confirm)) || rect(confirm).right() > rect(cancel).left() + 0.5 ||
        rect(hint).left() > rect(editor).left() + 0.5 ||
        rect(cancel).right() < rect(editor).right() - edgeTolerance ||
        rect(cancel).right() > rect(editor).right() + edgeTolerance ||
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

bool checkConnectionTargets(ApplicationHost& host, bool layoutChecks, const QString& capturePath) {
    auto& ui = host.state();
    auto* page = host.window()->findChild<QObject*>("connections_page");
    auto* details = host.window()->findChild<QObject*>("connection_details");
    const auto item = [&](const char* name) {
        return host.window()->findChild<QQuickItem*>(QString::fromLatin1(name));
    };
    if (!page || !details)
        return false;
    const QString sniffed = QCoreApplication::translate("connections_page", "Sniffed");
    const QString longDomain =
        QString(60, 'a') + '.' + QString(60, 'b') + '.' + QString(60, 'c') + ".example.test";
    struct TargetCase {
        QJsonObject metadata;
        QString target;
        bool sniffed;
    };
    const QList<TargetCase> cases{
        {{{"host", "target.example"}, {"destinationPort", "443"}}, "target.example:443", false},
        {{{"host", ""},
          {"sniffHost", "sniffed.example"},
          {"destinationIP", "192.0.2.42"},
          {"destinationPort", "443"}},
         "sniffed.example:443",
         true},
        {{{"host", "original.example"},
          {"sniffHost", "different.example"},
          {"destinationIP", "192.0.2.43"},
          {"destinationPort", "443"}},
         "original.example:443",
         false},
        {{{"destinationIP", "192.0.2.44"}, {"destinationPort", "80"}}, "192.0.2.44:80", false},
        {{{"destinationIP", "2001:db8::42"}, {"destinationPort", "443"}},
         "[2001:db8::42]:443",
         false},
        {{}, QString::fromUtf8("—"), false},
        {{{"host", longDomain},
          {"sniffHost", "different." + longDomain},
          {"destinationIP", "2001:db8::44"},
          {"destinationPort", "8443"}},
         longDomain + ":8443",
         false}};
    const auto originalSize = host.window()->size();
    for (const auto& fixture : cases) {
        auto connection = entry("target-fixture", {});
        connection.insert("metadata", fixture.metadata);
        ui.applyData({1,
                      "fixture",
                      {{"connections", QJsonArray{connection}}},
                      ui.dataDemands().generations});
        QString target;
        QString supporting;
        QString description;
        const QVariant argument = connection.toVariantMap();
        if (!QMetaObject::invokeMethod(page, "cellText", Q_RETURN_ARG(QString, target),
                                       Q_ARG(QVariant, argument), Q_ARG(QVariant, 0)) ||
            !QMetaObject::invokeMethod(page, "targetSupportingText",
                                       Q_RETURN_ARG(QString, supporting),
                                       Q_ARG(QVariant, argument)) ||
            !QMetaObject::invokeMethod(page, "describe", Q_RETURN_ARG(QString, description),
                                       Q_ARG(QVariant, argument)) ||
            target != fixture.target || supporting.startsWith(sniffed) != fixture.sniffed ||
            (fixture.sniffed && !description.contains(sniffed)) ||
            !QMetaObject::invokeMethod(page, "showDetails", Q_ARG(QVariant, argument))) {
            qWarning() << "Connection target presentation failed:" << fixture.metadata << target
                       << supporting;
            return false;
        }
        QTest::qWait(30);
        auto* targetLabel = item("connection_target");
        auto* noDomain = item("connection_domain_unavailable");
        if (!targetLabel || targetLabel->property("text") != fixture.target || !noDomain ||
            noDomain->isVisible() != (fixture.metadata.value("host").toString().isEmpty() &&
                                      fixture.metadata.value("sniffHost").toString().isEmpty()))
            return false;
        for (const auto& [name, field] : {std::pair{"connection_target_domain", "host"},
                                          std::pair{"connection_sniffed_domain", "sniffHost"},
                                          std::pair{"connection_target_ip", "destinationIP"}}) {
            auto* label = item(name);
            if (!label ||
                label->isVisible() != !fixture.metadata.value(field).toString().isEmpty() ||
                label->property("text") != fixture.metadata.value(field).toString())
                return false;
        }
        if (fixture.sniffed) {
            ui.liveConnections()->setQuery("sniffed.example");
            if (!waitForFilter(ui.liveConnections(), "sniffed.example", 1))
                return false;
            ui.liveConnections()->setQuery({});
        }
        if (fixture.metadata.value("host") == longDomain) {
            const QList<QSize> sizes =
                layoutChecks ? QList<QSize>{{1200, 800}, {360, 480}} : QList<QSize>{originalSize};
            for (const auto& size : sizes) {
                host.resizeWindow(size);
                QTest::qWait(60);
                auto* scroll = item("connection_details_scroll");
                auto* raw = item("connection_raw_json");
                if (!scroll || !raw || !raw->property("text").toString().isEmpty())
                    return false;
                for (const auto* name : {"connection_target", "connection_target_domain",
                                         "connection_sniffed_domain", "connection_target_ip"}) {
                    auto* label = item(name);
                    const auto bounds = label->mapRectToItem(scroll, label->boundingRect());
                    if (label->width() <= 0 || label->height() + 0.5 < label->implicitHeight() ||
                        bounds.left() < -0.5 || bounds.right() > scroll->width() + 0.5) {
                        qWarning() << "Connection address clipped:" << name << size << bounds;
                        return false;
                    }
                }
                auto* flickable = scroll->property("contentItem").value<QObject*>();
                auto* close = item("connection_close");
                if (!flickable || !close)
                    return false;
                flickable->setProperty(
                    "contentY", std::max(0.0, scroll->property("contentHeight").toReal() -
                                                  scroll->property("availableHeight").toReal()));
                QTest::qWait(20);
                const auto closeBounds = close->mapRectToItem(scroll, close->boundingRect());
                if (closeBounds.top() < -0.5 || closeBounds.bottom() > scroll->height() + 0.5)
                    return false;
                flickable->setProperty("contentY", 0);
                if (!capturePath.isEmpty() &&
                    !host.window()->grabWindow().save(
                        QDir(capturePath)
                            .filePath(
                                QStringLiteral("connection_targets_%1.png").arg(size.width()))))
                    return false;
                if (!page->setProperty("rawVisible", true) ||
                    !raw->property("text").toString().contains("sniffHost"))
                    return false;
                QTest::qWait(30);
                flickable->setProperty(
                    "contentY", std::max(0.0, scroll->property("contentHeight").toReal() -
                                                  scroll->property("availableHeight").toReal()));
                QTest::qWait(20);
                const auto expandedCloseBounds =
                    close->mapRectToItem(scroll, close->boundingRect());
                if (expandedCloseBounds.top() < -0.5 ||
                    expandedCloseBounds.bottom() > scroll->height() + 0.5 ||
                    !page->setProperty("rawVisible", false) ||
                    !raw->property("text").toString().isEmpty())
                    return false;
            }
            host.resizeWindow(originalSize);
        }
        QMetaObject::invokeMethod(details, "reject");
        QTest::qWait(30);
    }
    std::puts(
        "Connection target/domain/sniffed/IP, source marker, search and detail layout passed.");
    return true;
}
} // namespace
int runRuntimeListProbe(ApplicationHost& host) {
    const auto capturePath = qEnvironmentVariable("QUICKCLASH_RULE_LAYOUT_CAPTURE_DIR");
    const bool layoutChecks = !capturePath.isEmpty() || QCoreApplication::arguments().contains(
                                                            QStringLiteral("--ui-layout-checks"));
    // No SettingsService, core, network or platform actions: only synthetic GUI channel delivery.
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
        // SW_HIDE startup requires another visibility transition before native layout/rendering.
        host.window()->hide();
        host.window()->show();
        if (!QTest::qWaitForWindowExposed(host.window(), 5000)) {
            std::fputs("Native runtime list window was not exposed.\n", stderr);
            return 283;
        }
        const QJsonObject environment{
            {"qt", QString::fromLatin1(qVersion())},
            {"graphicsApi", static_cast<int>(host.window()->rendererInterface()->graphicsApi())},
            {"dpr", host.window()->devicePixelRatio()},
            {"exposed", host.window()->isExposed()}};
        std::printf("Runtime list environment: %s\n",
                    QJsonDocument(environment).toJson(QJsonDocument::Compact).constData());
    }
    auto& ui = host.state();
    ui.setReplay(false);
    ui.applyControl(
        {{"publicationGeneration", "1"},
         {"epoch", "fixture"},
         {"ready", true},
         {"status", "Ready"},
         {"features",
          QJsonObject{{"testHistory",
                       QJsonArray{QJsonObject{{"target", "https://fixture.example"},
                                              {"state", "Reachable"},
                                              {"status", 204},
                                              {"elapsedMs", 125},
                                              {"path", "Direct"},
                                              {"mode", "rule"},
                                              {"finalOrigin", "https://fixture.example"}}}}}}});
    if (QCoreApplication::arguments().contains(QStringLiteral("--connection-targets-only"))) {
        if (!ui.navigate("connections"))
            return 40;
        QTest::qWait(60);
        return checkConnectionTargets(host, true, capturePath) ? 0 : 300;
    }
    if (!ui.navigate("tests"))
        return 230;
    QTest::qWait(60);
    auto* targets = host.window()->findChild<QObject*>("website_targets");
    auto* run = host.window()->findChild<QQuickItem*>("run_website_test");
    auto* count = host.window()->findChild<QObject*>("website_target_count");
    auto* card = findVisualItem(host.window()->contentItem(), "website_result_card");
    auto* summary = findVisualItem(card, "website_result_summary");
    auto* toggle = findVisualItem(card, "website_result_details_toggle");
    auto* detailsText = findVisualItem(card, "website_result_details");
    if (!targets || !run || !count || !summary || !toggle || !detailsText ||
        !summary->property("text").toString().contains("204") ||
        !summary->property("text").toString().contains("125 ms") || detailsText->isVisible()) {
        qWarning() << "Website result fixture:" << targets << run << count << card << summary
                   << toggle << detailsText << (summary ? summary->property("text") : QVariant());
        return 231;
    }
    if (!QMetaObject::invokeMethod(toggle, "click") || !detailsText->isVisible() ||
        !detailsText->property("text").toString().contains("fixture.example") ||
        !QMetaObject::invokeMethod(toggle, "click") || detailsText->isVisible())
        return 232;
    auto initialTest = QJsonObject{{"publicationGeneration", "1"},
                                   {"epoch", "fixture"},
                                   {"ready", true},
                                   {"testTarget", "https://saved.example"}};
    ui.applyControl(initialTest);
    if (targets->property("text").toString() != "https://saved.example")
        return 312;
    const auto checkTargets = [&](const QString& text, int expectedCount, bool enabled) {
        return targets->setProperty("text", text) && run->isEnabled() == enabled &&
               count->property("text").toString().contains(QString::number(expectedCount));
    };
    QStringList eightTargets;
    for (int index = 0; index < 8; ++index)
        eightTargets.append(QStringLiteral("https://fixture%1.example").arg(index));
    if (!targets->setProperty("text", "  \n") || run->isEnabled() ||
        !count->property("text").toString().contains(QStringLiteral("1–8")) ||
        !checkTargets("https://fixture.example", 1, true) ||
        !checkTargets(eightTargets.join('\n'), 8, true) ||
        !checkTargets(eightTargets.join('\n') + "\nhttps://extra.example", 9, false))
        return 233;
    auto* targetInput = qobject_cast<QQuickItem*>(targets);
    if (!targetInput)
        return 301;
    const auto typeText = [&](const QByteArray& text) {
        for (const char character : text)
            QTest::keyClick(host.window(), character);
    };
    targetInput->forceActiveFocus();
    QTest::keyClick(host.window(), Qt::Key_A, Qt::ControlModifier);
    typeText("https://first.example");
    QTest::keyClick(host.window(), Qt::Key_Return);
    typeText("https://second.example");
    const QString batchDraft = "https://first.example\nhttps://second.example";
    if (targets->property("text").toString() != batchDraft || ui.websiteTestDraft() != batchDraft)
        return 302;
    QMetaObject::invokeMethod(run, "click");
    auto publishedTest =
        QJsonObject{{"publicationGeneration", "1"},
                    {"epoch", "fixture"},
                    {"ready", true},
                    {"testTarget", "https://first.example"},
                    {"features", QJsonObject{{"websiteTest", QJsonObject{{"state", "Running"}}}}}};
    ui.applyControl(publishedTest);
    QTest::qWait(30);
    if (targets->property("text").toString() != batchDraft)
        return 303;
    targetInput->forceActiveFocus();
    QTest::keyClick(host.window(), Qt::Key_End, Qt::ControlModifier);
    typeText("/edited");
    const QString editedDraft = batchDraft + "/edited";
    publishedTest.insert("testTarget", "https://second.example");
    publishedTest.insert("features",
                         QJsonObject{{"websiteTest", QJsonObject{{"state", "Timeout"}}}});
    ui.applyControl(publishedTest);
    QTest::qWait(30);
    if (targets->property("text").toString() != editedDraft ||
        ui.websiteTestDraft() != editedDraft) {
        qWarning() << "Website draft after edit and failed result:" << targets->property("text")
                   << ui.websiteTestDraft() << "expected" << editedDraft << "focus"
                   << host.window()->activeFocusItem();
        return 304;
    }
    if (!ui.navigate("home") || !ui.navigate("tests"))
        return 305;
    QTest::qWait(60);
    targets = host.window()->findChild<QObject*>("website_targets");
    if (!targets || targets->property("text").toString() != editedDraft)
        return 306;
    const auto websiteGeneration = host.engineGeneration();
    host.unloadWindow();
    if (!host.showWindow() || host.engineGeneration() != websiteGeneration + 1)
        return 307;
    QTest::qWait(60);
    targets = host.window()->findChild<QObject*>("website_targets");
    if (!targets || targets->property("text").toString() != editedDraft)
        return 308;
    auto* restoredInput = qobject_cast<QQuickItem*>(targets);
    if (!restoredInput)
        return 313;
    restoredInput->forceActiveFocus();
    QTest::keyClick(host.window(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(host.window(), Qt::Key_Backspace);
    ui.applyControl(initialTest);
    if (!ui.websiteTestDraftActive() || !ui.websiteTestDraft().isEmpty() ||
        !targets->property("text").toString().isEmpty())
        return 314;
    ui.setWebsiteTestDraft(editedDraft);
    if (host.window()->findChild<QObject*>("user_rule_binding_dialog") ||
        host.window()->findChild<QObject*>("close_confirmation"))
        return 218;
    if (!ui.navigate("connections"))
        return 40;
    QTest::qWait(60);
    if (!checkConnectionTargets(host, layoutChecks, capturePath))
        return 300;
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
    if (!waitForFilter(ui.liveConnections(), "alpha", 1) ||
        ui.liveConnections()->captureIds() != QStringList{"one"})
        return 43;
    if (!filter->setProperty("text", "no-such-connection"))
        return 234;
    if (!waitForFilter(ui.liveConnections(), "no-such-connection", 0) ||
        !waitForClickable(host, "connections_clear_filter"))
        return 235;
    if (!click(host, "connections_clear_filter") || !ui.liveConnections()->query().isEmpty() ||
        !filter->property("text").toString().isEmpty() || ui.liveConnections()->count() != 2) {
        auto* clear = host.window()->findChild<QQuickItem*>("connections_clear_filter");
        qWarning() << "Connection filter clear:" << ui.liveConnections()->query()
                   << ui.liveConnections()->count() << filter->property("text")
                   << (clear ? clear->mapRectToScene(clear->boundingRect()) : QRectF());
        return 235;
    }
    if (!filter->setProperty("text", "alpha") || !waitForFilter(ui.liveConnections(), "alpha", 1) ||
        ui.liveConnections()->captureIds() != QStringList{"one"} ||
        !waitForClickable(host, "close_filtered_connections") ||
        !click(host, QStringLiteral("close_filtered_connections")))
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
    if (!filter->setProperty("text", "new"))
        return 78;
    ui.applyData({1,
                  "fixture",
                  {{"connections", QJsonArray{entry("one", "changed"), entry("new", "alpha")}}},
                  ui.dataDemands().generations});
    const auto selectedHost = [&] {
        auto selected = page->property("selectedEntry");
        if (selected.metaType() == QMetaType::fromType<QJSValue>())
            selected = selected.value<QJSValue>().toVariant();
        return selected.toMap().value("metadata").toMap().value("host").toString();
    };
    if (!QTest::qWaitFor(
            [&] {
                return ui.liveConnections()->query() == "new" &&
                       ui.liveConnections()->captureIds() == QStringList{"new"} &&
                       selectedHost() == "changed";
            },
            1500) ||
        page->property("selectedEnded").toBool()) {
        qWarning() << "Selected connection did not update:" << ui.liveConnections()->query()
                   << ui.liveConnections()->captureIds() << selectedHost()
                   << page->property("selectedEnded");
        return 78;
    }
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
    auto* resourceEntry = host.window()->findChild<QQuickItem*>("open_resource_updates");
    if (!section || !view || view->property("count").toInt() != 2)
        return 48;
    if (!resourceEntry || resourceEntry->isVisible() || section->property("currentIndex") != 0 ||
        ui.dataDemands().active.contains("providers"))
        return 284;
    auto* ruleFilter = host.window()->findChild<QObject*>("runtime_filter");
    if (!ruleFilter || !ruleFilter->setProperty("text", "no-such-rule"))
        return 237;
    if (!waitForFilter(ui.liveRules(), "no-such-rule", 0) ||
        !waitForClickable(host, "rules_clear_filter") || !click(host, "rules_clear_filter") ||
        !ruleFilter->property("text").toString().isEmpty() || ui.liveRules()->count() != 2)
        return 238;
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
    if (layoutChecks && !checkHorizontalFocus())
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
    const QJsonArray managedResources{
        QJsonObject{{"name", "fixture"}, {"kind", "rule-providers"}, {"mode", "snapshot"}}};
    ui.applyData({1,
                  "fixture",
                  {{"profiles", QJsonArray{QJsonObject{{"id", "selected-fixture"},
                                                       {"name", "Fixture"},
                                                       {"managedProviders", managedResources}}}}},
                  ui.dataDemands().generations});
    QTest::qWait(30);
    if (!resourceEntry->isVisible() || section->property("currentIndex") != 0 ||
        ui.dataDemands().active.contains("providers"))
        return 285;
    ui.applyData({1,
                  "fixture",
                  {{"rule_status", QJsonObject{{"rulePage", QJsonObject{{"loading", true}}}}}},
                  ui.dataDemands().generations});
    auto* refreshRules = host.window()->findChild<QObject*>(QStringLiteral("refresh_rules"));
    if (!refreshRules || refreshRules->property("enabled").toBool())
        return 81;
    const auto beforeCustomRulesSize = host.window()->size();
    if (layoutChecks)
        host.resizeWindow({360, 640});
    QQuickItem* customRulesEntry = nullptr;
    // Native resize and its QML layout can finish in different event turns.
    if (!QTest::qWaitFor(
            [&] {
                customRulesEntry = host.window()->findChild<QQuickItem*>("open_custom_rules");
                if (!customRulesEntry || !customRulesEntry->isVisible())
                    return false;
                if (!layoutChecks)
                    return true;
                const auto position = customRulesEntry->mapToScene(QPointF(0, 0));
                return position.x() >= 0 &&
                       position.x() + customRulesEntry->width() <= host.window()->width();
            },
            1000)) {
        std::fprintf(stderr,
                     "Custom rule entry did not fit the native window: width=%d entry=%g x=%g\n",
                     host.window()->width(), customRulesEntry ? customRulesEntry->width() : -1,
                     customRulesEntry ? customRulesEntry->mapToScene(QPointF(0, 0)).x() : -1);
        return 199;
    }
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
    if (layoutChecks &&
        (!captureRules("wide") || !checkCustomRulesLayout(host) || !checkCustomRuleRow(host)))
        return 203;
    auto* ruleList = host.window()->findChild<QQuickItem*>("user_rule_list");
    auto* firstRow = findVisualItem(ruleList, "user_rule_row_0");
    if (!ruleList || !firstRow || ruleList->property("count").toInt() != globalRules.size() ||
        (layoutChecks && ruleList->height() + 4 < 6 * (firstRow->height() + 4))) {
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
    auto* ruleEditor = host.window()->findChild<QObject*>("user_rules_editor");
    if (!ruleEditor)
        return 207;
    if (layoutChecks) {
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
            rect(scopeControl).bottom() > rect(reloadControl).top() + 1 ||
            rect(scopeControl).bottom() > rect(addControl).top() + 1 ||
            std::abs(rect(reloadControl).top() - rect(addControl).top()) > 1 ||
            rect(scopeControl).width() < rect(reloadControl).width() + rect(addControl).width() ||
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
        if (!QMetaObject::invokeMethod(ruleEditor, "edit", Q_ARG(QVariant, QVariant(-1))))
            return 207;
        QTest::qWait(30);
        if (!ruleEditor->property("formOpen").toBool() || !checkCustomRulesLayout(host, false))
            return 208;
        QMetaObject::invokeMethod(ruleEditor, "cancelForm");
        host.window()->setProperty("uiFont", originalFont);
        host.resizeWindow({1000, 640});
        QTest::qWait(40);
    }
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
    if (layoutChecks) {
        const auto oldSize = host.window()->size();
        host.resizeWindow({360, 640});
        QTest::qWait(50);
        if (!test::checkVisibleFocusFrames(host.window()))
            return 188;
        host.resizeWindow(oldSize);
        QTest::qWait(30);
    }
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
    if (!captureRules("resource_entry"))
        return 296;
    if (!click(host, "open_resource_updates"))
        return 286;
    QTest::qWait(30);
    if (ui.liveRules()->count() != 0 || ui.dataDemands().active.contains("rules") ||
        !ui.dataDemands().active.contains("providers") || ui.telemetry().contains("rulePage"))
        return 60;
    ui.applyData(oldRules);
    const application::RuntimeUpdate oldProviders{
        1,
        "fixture",
        {{"providers", QJsonArray{QJsonObject{{"name", "fixture"},
                                              {"kind", "rules"},
                                              {"mode", "runtime"},
                                              {"count", 2},
                                              {"updatedAt", "2026-11-07T00:00:00Z"},
                                              {"lastResult", "Failed"},
                                              {"lastError", "ProviderDownloadFailed"}}}}},
        ui.dataDemands().generations};
    ui.applyData(oldProviders);
    if (view->property("count").toInt() != 1)
        return 49;
    rulesPage = host.window()->findChild<QObject*>("rules_page");
    auto* resourceHint = host.window()->findChild<QQuickItem*>("resource_updates_hint");
    refreshRules = host.window()->findChild<QObject*>("refresh_rules");
    if (!rulesPage || !resourceHint || !resourceHint->isVisible() || !refreshRules ||
        refreshRules->property("text") != QCoreApplication::translate("rules_page", "Refresh list"))
        return 287;
    QString resourceText;
    const QVariant resourceDescription =
        QVariantMap{{"name", "fixture"}, {"kind", "proxy-providers"}, {"mode", "snapshot"}};
    if (!QMetaObject::invokeMethod(rulesPage, "cellText", Q_RETURN_ARG(QString, resourceText),
                                   Q_ARG(QVariant, resourceDescription), Q_ARG(QVariant, 1)) ||
        resourceText != QCoreApplication::translate("rules_page", "Proxy collection") ||
        !QMetaObject::invokeMethod(rulesPage, "cellText", Q_RETURN_ARG(QString, resourceText),
                                   Q_ARG(QVariant, resourceDescription), Q_ARG(QVariant, 3)) ||
        resourceText != QString::fromUtf8("—"))
        return 288;
    const QVariant failedResource =
        oldProviders.values.value("providers").toArray().first().toObject().toVariantMap();
    if (!QMetaObject::invokeMethod(rulesPage, "cellText", Q_RETURN_ARG(QString, resourceText),
                                   Q_ARG(QVariant, failedResource), Q_ARG(QVariant, 5)) ||
        !resourceText.contains("ProviderDownloadFailed"))
        return 297;
    const auto resourceSize = host.window()->size();
    const QList<QSize> resourceSizes =
        layoutChecks ? QList<QSize>{{1200, 800}, {360, 640}} : QList<QSize>{{1200, 800}};
    for (const auto& size : resourceSizes) {
        host.resizeWindow(size);
        QTest::qWait(40);
        if (layoutChecks && (resourceHint->height() + 0.5 < resourceHint->implicitHeight() ||
                             view->property("height").toReal() < 80))
            return 289;
        if (layoutChecks && size.width() == 1200) {
            auto* viewport = qobject_cast<QQuickItem*>(view)->parentItem();
            while (viewport && !viewport->property("contentX").isValid())
                viewport = viewport->parentItem();
            if (!viewport || viewport->property("contentWidth").toReal() > viewport->width() + 0.5)
                return 298;
        }
        if (!captureRules(QStringLiteral("resources_%1").arg(size.width())))
            return 290;
    }
    host.resizeWindow(resourceSize);
    if (layoutChecks && !checkHorizontalFocus())
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
    if (!click(host, "back_to_rules"))
        return 291;
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
    if (!click(host, "open_resource_updates"))
        return 292;
    ui.applyData(
        {1,
         "fixture",
         {{"profiles", QJsonArray{QJsonObject{{"id", "selected-fixture"}, {"name", "Fixture"}}}}},
         ui.dataDemands().generations});
    QTest::qWait(30);
    resourceEntry = host.window()->findChild<QQuickItem*>("open_resource_updates");
    ui.applyData(currentProviders);
    if (!resourceEntry || resourceEntry->isVisible() || section->property("currentIndex") != 0 ||
        ui.liveProviders()->count() != 0 || ui.dataDemands().active.contains("providers") ||
        !ui.dataDemands().active.contains("rules"))
        return 293;
    ui.applyData(
        {1,
         "fixture",
         {{"profiles", QJsonArray{QJsonObject{{"id", "selected-fixture"},
                                              {"name", "Fixture"},
                                              {"managedProviders", managedResources}},
                                  QJsonObject{{"id", "plain-fixture"}, {"name", "Plain"}}}}},
         ui.dataDemands().generations});
    QTest::qWait(30);
    if (!click(host, "open_resource_updates"))
        return 294;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "fixture"},
                     {"ready", true},
                     {"selectedId", "plain-fixture"}});
    QTest::qWait(30);
    if (resourceEntry->isVisible() || section->property("currentIndex") != 0 ||
        ui.dataDemands().active.contains("providers"))
        return 295;
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
    auto* logFilter = host.window()->findChild<QObject*>("runtime_filter");
    if (!logFilter || !logFilter->setProperty("text", "no-such-log"))
        return 239;
    const auto logFilterSettled = waitForFilter(ui.liveLogs(), "no-such-log", 0);
    auto* clearLogs = findVisualItem(host.window()->contentItem(), "logs_clear_filter");
    if (!logFilterSettled || !clearLogs || !clearLogs->isVisible() ||
        !QMetaObject::invokeMethod(clearLogs, "click") ||
        !logFilter->property("text").toString().isEmpty() || ui.liveLogs()->count() != 100) {
        qWarning() << "Log filter clear:" << ui.liveLogs()->count() << clearLogs
                   << (clearLogs ? clearLogs->isVisible() : false) << logFilter->property("text");
        return 240;
    }
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
    view->setProperty("contentY", 0);
    QTest::qWait(30);
    if (!click(host, "logs_jump_latest") ||
        !QTest::qWaitFor([&] { return view->property("atYEnd").toBool(); }, 1000) ||
        !ui.dataDemands().active.contains("log_feed")) {
        qWarning() << "Log follow recovery:" << view->property("atYEnd")
                   << view->property("contentY") << view->property("contentHeight");
        return 236;
    }
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
                     {"ready", true},
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
                                                     {"current", "node_1"},
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
    auto* list = findVisualItem(host.window()->contentItem(), "proxy_group_list");
    const auto checkFocusRecycling = [&](bool delay) {
        if (!grid || !list || !QMetaObject::invokeMethod(grid, "focusMember", Q_ARG(QVariant, 0)))
            return false;
        QTest::qWait(40);
        auto* card = findVisualItem(host.window()->contentItem(), "proxy_member_manual_node_0");
        auto* focus = delay && card ? card->findChild<QQuickItem*>("test_node") : card;
        if (!focus)
            return false;
        focus->forceActiveFocus();
        if (!focus->hasActiveFocus())
            return false;
        QSignalSpy activated(focus, SIGNAL(clicked()));
        if (!activated.isValid())
            return false;
        const auto scroll = std::min(list->property("contentHeight").toDouble() - list->height(),
                                     grid->property("pageY").toDouble() +
                                         20 * grid->property("rowHeight").toDouble());
        list->setProperty("contentY", scroll);
        QTest::qWait(50);
        auto* current = host.window()->activeFocusItem();
        if (!current || current->objectName() != "proxy_node_search")
            return false;
        QTest::keyClick(host.window(), Qt::Key_Space);
        return activated.isEmpty() && current->property("text").toString() == " ";
    };
    if (!checkFocusRecycling(false))
        return 309;
    proxies->setProperty("searchText", "");
    if (!checkFocusRecycling(true))
        return 310;
    proxies->setProperty("searchText", "");
    ui.applyData({2,
                  "next",
                  {{"groups", QJsonArray{QJsonObject{{"name", "manual"},
                                                     {"type", "Selector"},
                                                     {"canSelect", true},
                                                     {"current", "node_499"},
                                                     {"members", members}}}}},
                  ui.dataDemands().generations});
    QTest::qWait(40);
    grid = findGrid(findGrid, host.window()->contentItem());
    if (!grid || !QMetaObject::invokeMethod(grid, "focusMember", Q_ARG(QVariant, 499)))
        return 75;
    QTest::qWait(60);
    if (grid->property("endIndex").toInt() != 500 ||
        controls(controls, host.window()->contentItem()) > 80)
        return 76;
    // Search traverses the data, not all 500 delegates, and keeps exact member names for actions.
    if (!proxies->setProperty("searchText", "NODE_49"))
        return 223;
    QTest::qWait(60);
    if (proxies->property("matchingCount").toInt() != 11 ||
        controls(controls, host.window()->contentItem()) > 11)
        return 224;
    auto* subtitle = findVisualItem(host.window()->contentItem(), "proxy_group_subtitle");
    if (!subtitle || !subtitle->property("text").toString().contains("11") ||
        !subtitle->property("text").toString().contains("500") ||
        !click(host, "proxy_clear_search") || !proxies->property("searchText").toString().isEmpty())
        return 241;
    proxies->setProperty("searchText", "NODE_49");
    QTest::qWait(30);
    if (!capturePath.isEmpty() &&
        !host.window()->grabWindow().save(QDir(capturePath).filePath("node_search_500.png")))
        return 280;
    proxies->setProperty("searchText", "MANUAL");
    QTest::qWait(60);
    if (proxies->property("matchingCount").toInt() != 500 ||
        controls(controls, host.window()->contentItem()) > 80)
        return 282;
    proxies->setProperty("searchText", "missing-node");
    QTest::qWait(40);
    auto* emptySearch = host.window()->findChild<QQuickItem*>("proxy_search_empty");
    if (proxies->property("matchingCount").toInt() != 0 || !emptySearch ||
        !emptySearch->isVisible() || controls(controls, host.window()->contentItem()) != 0)
        return 225;
    if (!click(host, "proxy_locate_current"))
        return 226;
    QTest::qWait(80);
    auto* located = findVisualItem(host.window()->contentItem(), "proxy_member_manual_node_499");
    if (!proxies->property("searchText").toString().isEmpty() || !located ||
        !located->hasActiveFocus() || grid->property("endIndex").toInt() != 500 ||
        controls(controls, host.window()->contentItem()) > 80)
        return 227;
    if (!capturePath.isEmpty() &&
        !host.window()->grabWindow().save(QDir(capturePath).filePath("node_located_500.png")))
        return 281;
    ui.applyData({2,
                  "next",
                  {{"groups", QJsonArray{QJsonObject{{"name", "manual"},
                                                     {"type", "Selector"},
                                                     {"canSelect", true},
                                                     {"current", "node_499"},
                                                     {"members", members}},
                                         QJsonObject{{"name", "empty-group"},
                                                     {"type", "Selector"},
                                                     {"canSelect", true},
                                                     {"members", QJsonArray{}}}}}},
                  ui.dataDemands().generations});
    proxies->setProperty("searchText", "empty-group");
    QTest::qWait(50);
    auto* emptyGroup = findVisualItem(host.window()->contentItem(), "proxy_group_empty-group");
    emptySearch = host.window()->findChild<QQuickItem*>("proxy_search_empty");
    if (!emptyGroup || !emptyGroup->isVisible() || !emptySearch || emptySearch->isVisible())
        return 311;
    proxies->setProperty("searchText", "NODE_49");
    if (!ui.navigate("home") || !ui.navigate("proxies"))
        return 228;
    QTest::qWait(60);
    loader = host.window()->findChild<QObject*>("active_page_host");
    proxies = loader ? loader->property("item").value<QObject*>() : nullptr;
    // Page destruction releases the heavy channels, while the in-session query survives.
    if (!proxies || proxies->property("searchText") != "NODE_49") {
        std::fputs("Node search was not restored after page reconstruction.\n", stderr);
        return 229;
    }
    proxies->setProperty("searchText", "");
    proxies->setProperty("expandedGroups", QStringList{});
    QTest::qWait(30);
    if (controls(controls, host.window()->contentItem()) != 0)
        return 78;
    if (!ui.navigate("settings"))
        return 81;
    ui.setSettingsCategory(1);
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
    auto* settingsPage = host.window()->findChild<QObject*>("settings_network");
    auto* settingsText = host.window()->findChild<QObject*>("network_settings_state");
    auto* settingsComparison = host.window()->findChild<QObject*>("network_settings_comparison");
    ui.editSetting("mixedPort", "7891");
    if (!settingsPage || !settingsText ||
        !settingsText->property("text").toString().contains("7891") || !settingsComparison ||
        !settingsComparison->property("text").toString().contains("7890") ||
        !QMetaObject::invokeMethod(settingsPage, "requestNetworkSettings"))
        return 82;
    QPointer<QObject> settingsDialog =
        host.window()->findChild<QObject*>("network_settings_confirmation");
    auto pending = settingsPage->property("pendingNetworkArgs");
    if (pending.metaType() == QMetaType::fromType<QJSValue>())
        pending = pending.value<QJSValue>().toVariant();
    const auto capturedSettings = pending.toList();
    if (!settingsDialog || !settingsDialog->property("visible").toBool() ||
        capturedSettings.size() != 6 || capturedSettings[1] != "7891" ||
        capturedSettings[4] != "next" || capturedSettings[5] != "7")
        return 83;
    QMetaObject::invokeMethod(settingsDialog, "reject");
    QTest::qWait(20);
    auto portControl = QJsonObject::fromVariantMap(ui.runtime());
    portControl.insert(
        "features", QJsonObject{{"listenerPortCandidate", QJsonObject{{"token", "port_preview"},
                                                                      {"mixedPort", 7893},
                                                                      {"controllerPort", 9092}}}});
    ui.applyControl(portControl);
    QTest::qWait(40);
    auto* portDialog = host.window()->findChild<QObject*>("settings_port_confirmation");
    if (settingsDialog || !portDialog || !portDialog->property("visible").toBool() ||
        ui.settingsSession().value("network").toMap().value("mixedPort") != "7891")
        return 181;
    QMetaObject::invokeMethod(portDialog, "accept");
    if (ui.settingsSession().value("network").toMap().value("mixedPort") != "7893" ||
        ui.settingsSession().value("network").toMap().value("controllerPort") != "9092")
        return 181;
    ui.setSettingsCategory(3);
    QTest::qWait(25);
    const QJsonObject maintenancePreview{
        {"token", "fixed-support"}, {"kind", "diagnostics"}, {"text", "{\"schemaVersion\":1}"}};
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"maintenancePreview", maintenancePreview}}}});
    QTest::qWait(40);
    QPointer<QObject> maintenanceDialog =
        host.window()->findChild<QObject*>("maintenance_confirmation");
    if (!maintenanceDialog || !maintenanceDialog->property("visible").toBool())
        return 139;
    host.window()->resize(360, 480);
    QTest::qWait(40);
    if (maintenanceDialog->property("width").toReal() > 328 ||
        maintenanceDialog->property("height").toReal() > 448)
        return 140;
    ui.setUiVisible(false);
    QTest::qWait(20); // Loader releases the hidden preview at the deferred deletion boundary.
    if (maintenanceDialog && maintenanceDialog->property("visible").toBool())
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
    QPointer<QObject> backupDialog =
        host.window()->findChild<QObject*>("backup_restore_confirmation");
    if (!backupDialog || !backupDialog->property("visible").toBool())
        return 84;
    host.window()->resize(360, 480);
    QTest::qWait(40);
    if (backupDialog->property("width").toReal() > 328 ||
        backupDialog->property("height").toReal() > 448)
        return 85;
    ui.setUiVisible(false);
    QTest::qWait(20);
    if (backupDialog && backupDialog->property("visible").toBool())
        return 86;
    ui.applyControl({{"publicationGeneration", "2"},
                     {"epoch", "next"},
                     {"features", QJsonObject{{"backupPreview", backupPreview}}}});
    QTest::qWait(20);
    if (backupDialog && backupDialog->property("visible").toBool())
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
    backupDialog = host.window()->findChild<QObject*>("backup_restore_confirmation");
    auto* restoreGlobal = host.window()->findChild<QQuickItem*>("backup_restore_global_rules");
    auto* restoreAccept = host.window()->findChild<QQuickItem*>("backup_restore_accept");
    if (!backupDialog || !backupDialog->property("visible").toBool() ||
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
    // Generating a rule only seeds the existing editor. The connection identity and
    // selected profile are checked again before a scoped rules baseline can arrive.
    host.resizeWindow({1000, 760});
    ui.setUserRulesDraft({});
    QJsonObject ruleControl{{"publicationGeneration", "3"},
                            {"epoch", "rule-flow"},
                            {"ready", true},
                            {"selectedId", "rule-profile"},
                            {"selectionGeneration", "1"},
                            {"preferences", QJsonObject{{"mode", "rule"}}}};
    ui.applyControl(ruleControl);
    if (!ui.navigate("connections"))
        return 260;
    QTest::qWait(40);
    auto connection = entry("rule-connection", "Exact.Example.com");
    connection.insert("epoch", "rule-flow");
    const auto publishConnection = [&] {
        ui.applyData({3,
                      "rule-flow",
                      {{"connections", QJsonArray{connection}}},
                      ui.dataDemands().generations});
    };
    const auto openConnection = [&] {
        auto* connectionPage = host.window()->findChild<QObject*>("connections_page");
        return connectionPage &&
               QMetaObject::invokeMethod(connectionPage, "showDetails",
                                         Q_ARG(QVariant, connection.toVariantMap()));
    };
    publishConnection();
    if (!openConnection())
        return 261;
    QTest::qWait(150);
    if (!click(host, "connection_create_rule"))
        return 262;
    QTest::qWait(40);
    auto* generatedDialog = host.window()->findChild<QObject*>("custom_rules_dialog");
    if (ui.currentPage() != "rules" || !generatedDialog ||
        !generatedDialog->property("visible").toBool() ||
        !ui.dataDemands().active.contains("user_rules"))
        return 263;
    const QJsonObject generatedView{
        {"revision", "generated-1"},     {"profileId", "rule-profile"},
        {"profileName", "Rule fixture"}, {"global", QJsonArray{}},
        {"local", QJsonArray{}},         {"groups", QJsonArray{"Proxy"}}};
    ui.applyData({3, "rule-flow", {{"user_rules", generatedView}}, ui.dataDemands().generations});
    QTest::qWait(30);
    auto* generatedEditor = host.window()->findChild<QObject*>("user_rules_editor");
    auto* generatedScope = host.window()->findChild<QObject*>("user_rule_scope");
    auto* generatedType = host.window()->findChild<QObject*>("user_rule_type");
    auto* generatedValue = host.window()->findChild<QObject*>("user_rule_value");
    if (!generatedEditor || !generatedEditor->property("formOpen").toBool() ||
        generatedEditor->property("dirty").toBool() || !generatedScope ||
        generatedScope->property("currentIndex").toInt() != 1 || !generatedType ||
        generatedType->property("currentText") != "DOMAIN" || !generatedValue ||
        generatedValue->property("text") != "exact.example.com" || !ui.connectionRule().isEmpty() ||
        generatedEditor->property("canConfirm").toBool())
        return 264;
    if (!capturePath.isEmpty() &&
        !host.window()->grabWindow().save(QDir(capturePath).filePath("connection_rule_review.png")))
        return 282;
    if (!QMetaObject::invokeMethod(generatedEditor, "saveRuleForm") ||
        !generatedEditor->property("localDirty").toBool() ||
        generatedEditor->property("globalDirty").toBool() ||
        !generatedEditor->property("canConfirm").toBool() || !click(host, "cancel_custom_rules") ||
        !ui.userRulesDraft().isEmpty())
        return 265;
    if (!ui.navigate("connections"))
        return 266;
    QTest::qWait(50);
    publishConnection();
    if (!openConnection())
        return 267;
    QTest::qWait(150);
    if (!click(host, "connection_create_rule"))
        return 268;
    QTest::qWait(30);
    ruleControl.insert("selectedId", "other-profile");
    ruleControl.insert("selectionGeneration", "2");
    ui.applyControl(ruleControl);
    ui.applyData({3, "rule-flow", {{"user_rules", generatedView}}, ui.dataDemands().generations});
    QTest::qWait(30);
    generatedEditor = host.window()->findChild<QObject*>("user_rules_editor");
    if (!ui.connectionRule().isEmpty() || !generatedEditor ||
        generatedEditor->property("formOpen").toBool() ||
        generatedEditor->property("dirty").toBool() || !click(host, "cancel_custom_rules") ||
        host.qmlErrorCount() != 0)
        return 269;
    std::puts("Runtime lists: leases, tab/hide/destruction release, late delivery, captured close, "
              "log pause/follow, 500-node search/locate and connection rule review passed");
    return 0;
}
} // namespace quickclash
