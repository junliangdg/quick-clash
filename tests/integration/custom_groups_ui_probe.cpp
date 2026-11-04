#include "custom_groups_ui_probe.h"
#include "application_host.h"
#include <QDir>
#include <QJSValue>
#include <QPointer>
#include <QQuickItem>
#include <QSet>
#include <QTest>
#include <cstdio>
#include <functional>

namespace quickclash {
namespace {
QObject* find(QObject* root, const QString& name) {
    QSet<QObject*> seen;
    const std::function<QObject*(QObject*)> visit = [&](QObject* object) -> QObject* {
        if (!object || seen.contains(object))
            return nullptr;
        seen.insert(object);
        if (object->objectName() == name)
            return object;
        for (auto* child : object->children())
            if (auto* found = visit(child))
                return found;
        // Repeater delegates can have a different QObject owner than their visual parent.
        if (auto* item = qobject_cast<QQuickItem*>(object))
            for (auto* child : item->childItems())
                if (auto* found = visit(child))
                    return found;
        return nullptr;
    };
    return visit(root);
}

bool click(ApplicationHost& host, QObject* object) {
    auto* item = qobject_cast<QQuickItem*>(object);
    if (!item || !item->isVisible() || !item->isEnabled())
        return false;
    item->forceActiveFocus(Qt::OtherFocusReason);
    QTest::mouseClick(host.window(), Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    QTest::qWait(30);
    return true;
}
// Check every clipping ancestor, not just the outer dialog padding.
bool focusFits(QQuickItem* item, qreal extent) {
    const QRectF ring{-extent, -extent, item->width() + 2 * extent, item->height() + 2 * extent};
    for (auto* ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (ancestor->clip() && !ancestor->boundingRect()
                                     .adjusted(-0.5, -0.5, 0.5, 0.5)
                                     .contains(item->mapRectToItem(ancestor, ring))) {
            std::fprintf(stderr, "Clipped focus: %s by %s\n",
                         item->objectName().toUtf8().constData(),
                         ancestor->metaObject()->className());
            return false;
        }
    }
    return item->hasActiveFocus();
}

void reveal(QQuickItem* item, QQuickItem* viewport, qreal margin) {
    const auto y = item->mapToItem(viewport, QPointF{}).y();
    const auto current = viewport->property("contentY").toReal();
    const auto bottom =
        qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
    viewport->setProperty("contentY", qBound(0.0, current + y - margin, bottom));
    item->forceActiveFocus(Qt::TabFocusReason);
    QTest::qWait(15);
}
} // namespace
int runCustomGroupsUiProbe(ApplicationHost& host) {
    QString output;
    for (const auto& argument : QCoreApplication::arguments())
        if (argument.startsWith("--custom-group-visual-output="))
            output = argument.mid(QStringLiteral("--custom-group-visual-output=").size());
    if (!output.isEmpty()) {
        // Expose a SW_HIDE helper before acquiring page leases or opening the modal form.
        host.window()->hide();
        host.window()->show();
        if (!QTest::qWaitForWindowExposed(host.window(), 3000))
            return 10;
    }
    auto& ui = host.state();
    const auto check = [](bool ok, const char* message) {
        if (!ok)
            std::fprintf(stderr, "Custom group UI: %s\n", message);
        return ok;
    };
    ui.applyControl({{"publicationGeneration", "1"}, {"epoch", "fixture"}, {"selectedId", "p0"}});
    if (!ui.navigate("proxies"))
        return 1;
    host.resizeWindow({1000, 760});
    QTest::qWait(100);
    QJsonObject state{
        {"profileId", "p0"},
        {"revision", "r0"},
        {"definitions", QJsonArray{QJsonObject{{"id", "existing"}, {"name", "Existing HK"}}}},
        {"groups", QJsonArray{QJsonObject{{"id", "existing"}, {"count", 0}}}},
        {"parents", QJsonArray{"manual"}},
        {"providers", QJsonArray{"cloud"}},
        {"inlineCount", 3}};
    const auto deliver = [&] {
        application::RuntimeUpdate update{1, "fixture", {{"custom_groups", state}}};
        update.demandGenerations = ui.dataDemands().generations;
        ui.applyData(update);
        QTest::qWait(30);
    };
    deliver();
    auto* emptyWarning = find(host.window(), "custom_group_empty_warning");
    if (!check(emptyWarning && emptyWarning->property("visible").toBool(),
               "saved empty group warning visible"))
        return 15;
    auto* button = find(host.window(), "new_custom_group");
    if (!check(!find(host.window(), "custom_group_editor"), "closed editor is not instantiated"))
        return 27;
    if (!check(click(host, button), "new group opens"))
        return 2;
    QPointer<QObject> editor = find(host.window(), "custom_group_editor");
    if (!check(editor && editor->property("visible").toBool(), "editor visible"))
        return 3;
    if (!check(QMetaObject::invokeMethod(editor, "preset", Q_ARG(QVariant, 2)), "Hong Kong preset"))
        return 4;
    QTest::qWait(30);
    auto* include = find(editor, "custom_group_include");
    auto* name = find(editor, "custom_group_name");
    auto* apply = find(editor, "custom_group_apply");
    if (!check(include && include->property("text").toString().contains("Hong Kong") && name &&
                   !name->property("text").toString().isEmpty() && apply &&
                   !apply->property("enabled").toBool(),
               "preset fills filter; unpreviewed apply disabled"))
        return 5;
    editor->setProperty("dirty", false);
    state.insert(
        "preview",
        QJsonObject{{"token", "t0"},
                    {"validated", true},
                    {"target", "g0"},
                    {"empty", false},
                    {"groups", QJsonArray{QJsonObject{{"id", "g0"},
                                                      {"count", 2},
                                                      {"members", QJsonArray{"香港-A", "HK-B"}},
                                                      {"truncated", false}}}}});
    deliver();
    if (!check(apply->property("enabled").toBool(), "validated preview enables apply"))
        return 6;
    QMetaObject::invokeMethod(editor, "invalidate");
    if (!check(!apply->property("enabled").toBool(), "edits invalidate approval"))
        return 7;
    editor->setProperty("dirty", false);
    auto preview = state.value("preview").toObject();
    preview.insert("empty", true);
    preview.insert("validated", false);
    state.insert("preview", preview);
    deliver();
    if (!check(!apply->property("enabled").toBool(), "empty selection cannot apply"))
        return 8;
    preview.insert("empty", false);
    preview.insert("validated", true);
    state.insert("preview", preview);
    deliver();
    // Always assert interaction geometry; optional PNGs additionally exercise native rendering.
    if (!output.isEmpty() && !QDir().mkpath(output))
        return 9;
    auto* scroll = find(editor, "custom_group_scroll");
    auto* viewport =
        scroll ? qobject_cast<QQuickItem*>(scroll->property("contentItem").value<QObject*>())
               : nullptr;
    auto* tokens = editor->property("theme").value<QObject*>();
    if (!viewport || !tokens)
        return 16;
    const auto extent =
        tokens->property("focusGap").toReal() + tokens->property("focusWidth").toReal();
    for (const auto& theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        ui.setThemeMode(theme);
        for (const auto& size : {QSize{1000, 760}, QSize{360, 640}}) {
            host.resizeWindow(size);
            QTest::qWait(80);
            const auto capture = [&](const QString& suffix) {
                if (output.isEmpty())
                    return true;
                return QTest::qWaitForWindowExposed(host.window(), 3000) &&
                       host.window()->grabWindow().save(
                           output +
                           QStringLiteral("/%1_%2%3.png").arg(theme).arg(size.width()).arg(suffix));
            };
            viewport->setProperty("contentY", 0);
            qobject_cast<QQuickItem*>(name)->forceActiveFocus(Qt::TabFocusReason);
            QTest::qWait(20);
            if (!capture({}))
                return 10;
            for (const auto* objectName :
                 {"custom_group_template", "custom_group_name", "custom_group_strategy",
                  "custom_group_filter_mode", "custom_group_include", "custom_group_exclude",
                  "custom_group_parent", "custom_group_url", "custom_group_interval",
                  "custom_group_timeout", "custom_group_tolerance", "custom_group_preview"}) {
                auto* field = qobject_cast<QQuickItem*>(find(editor, objectName));
                if (!check(field != nullptr, "focus control exists"))
                    return 18;
                reveal(field, viewport, extent + 4);
                if (!check(focusFits(field, extent), "complete focus ring inside clipped viewport"))
                    return 19;
            }
            const auto bottom =
                qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
            viewport->setProperty("contentY", bottom);
            QTest::qWait(20);
            if (!capture(QStringLiteral("_bottom")))
                return 17;
            for (const auto* objectName :
                 {"custom_group_interval", "custom_group_timeout", "custom_group_tolerance"}) {
                auto* spin = qobject_cast<QQuickItem*>(find(editor, objectName));
                reveal(spin, viewport, extent + 4);
                const auto original = spin->property("value").toInt();
                for (const auto* surfaceName :
                     {"spin_box_increment_surface", "spin_box_decrement_surface"}) {
                    auto* surface = qobject_cast<QQuickItem*>(find(spin, surfaceName));
                    auto* target = surface ? surface->parentItem() : nullptr;
                    if (!check(target && target->width() >= 32 &&
                                   target->height() == spin->height(),
                               "stepper keeps full hit target"))
                        return 20;
                    const auto safeOutline = spin->boundingRect().adjusted(2, 2, -2, -2);
                    if (!check(safeOutline.contains(
                                   surface->mapRectToItem(spin, surface->boundingRect())),
                               "stepper feedback inset from outer border"))
                        return 21;
                    // Click in the transparent margin, not only the smaller painted surface.
                    const auto point =
                        target->mapToScene(QPointF{1, target->height() / 2}).toPoint();
                    // A native window resize/scroll can leave the OS cursor at the same pixel.
                    // Cross the target boundary so hover is observed even without a new global
                    // position.
                    QTest::mouseMove(
                        host.window(),
                        QPoint{host.window()->width() - 4, host.window()->height() - 4}, 5);
                    QTest::mouseMove(host.window(), point, 5);
                    if (!check(QTest::qWaitFor(
                                   [&] { return surface->property("hovered").toBool(); }, 500),
                               "stepper hover follows full target")) {
                        std::fprintf(stderr,
                                     "Hover target: %s %s, %s %dpx, point=%d,%d enabled=%d\n",
                                     objectName, surfaceName, theme.toUtf8().constData(),
                                     size.width(), point.x(), point.y(), int(target->isEnabled()));
                        return 22;
                    }
                    const bool increment = QString::fromLatin1(surfaceName).contains("increment");
                    const bool representative =
                        QString::fromLatin1(objectName) == "custom_group_interval";
                    if (representative && increment && !capture(QStringLiteral("_step_hover")))
                        return 23;
                    QTest::mousePress(host.window(), Qt::LeftButton, Qt::NoModifier, point);
                    QTest::qWait(20);
                    const bool pressed = surface->property("down").toBool();
                    const bool captured =
                        !representative || increment || capture(QStringLiteral("_step_pressed"));
                    QTest::mouseRelease(host.window(), Qt::LeftButton, Qt::NoModifier, point);
                    QTest::qWait(20);
                    if (!check(pressed && captured &&
                                   spin->property("value").toInt() ==
                                       original + (increment ? 1 : 0),
                               "stepper press/release changes value without reducing hit area"))
                        return 24;
                }
                QTest::keyClick(host.window(), Qt::Key_Up);
                if (!check(spin->property("value").toInt() == original + 1, "keyboard increment"))
                    return 25;
                QTest::keyClick(host.window(), Qt::Key_Down);
                if (!check(spin->property("value").toInt() == original, "keyboard decrement"))
                    return 26;
            }
        }
    }
    state.insert("revision", "r1");
    deliver();
    if (!check(editor->property("conflict").toBool() && !apply->property("enabled").toBool(),
               "subscription revision conflict"))
        return 11;
    QTest::keyClick(host.window(), Qt::Key_Escape);
    QTest::qWait(30);
    if (!check(QTest::qWaitFor([&] { return editor.isNull(); }, 1000) &&
                   host.window()->activeFocusItem() == button,
               "Escape cancels and returns focus"))
        return 13;
    if (!check(click(host, button), "closed editor can be recreated"))
        return 28;
    editor = find(host.window(), "custom_group_editor");
    if (!check(editor && editor->property("visible").toBool() &&
                   editor->property("baseRevision") == "r1",
               "recreated editor uses the current revision"))
        return 29;
    QMetaObject::invokeMethod(editor, "close");
    if (!check(QTest::qWaitFor([&] { return editor.isNull(); }, 1000), "reopened editor released"))
        return 30;
    application::RuntimeUpdate stale{1, "fixture", {{"custom_groups", state}}};
    stale.demandGenerations = ui.dataDemands().generations;
    ui.navigate("home");
    QTest::qWait(40);
    if (!check(ui.customGroups().isEmpty() && !ui.dataDemands().active.contains("custom_groups"),
               "leave page releases definitions and preview"))
        return 12;
    ui.applyData(stale);
    if (!check(ui.customGroups().isEmpty(), "late custom-group publications stay fenced"))
        return 14;
    std::puts("custom group UI probe passed");
    return 0;
}
} // namespace quickclash
