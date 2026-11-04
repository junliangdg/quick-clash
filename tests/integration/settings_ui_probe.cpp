#include "settings_ui_probe.h"
#include "application_host.h"
#include "focus_layout_checks.h"
#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QPointingDevice>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <cstdio>
#include <memory>

namespace quickclash {
namespace {
template <typename Condition> bool waitFor(Condition condition) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (condition())
            return true;
        QTest::qWait(10);
    }
    return bool(condition());
}
} // namespace
int runSettingsUiProbe(ApplicationHost& host) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto& ui = host.state();
    const auto object = [&host](const char* name) {
        const auto visual = [name](auto&& self, QQuickItem* parent) -> QObject* {
            if (parent->objectName() == QLatin1String(name))
                return parent;
            for (auto* child : parent->childItems())
                if (auto* found = self(self, child))
                    return found;
            return nullptr;
        };
        if (auto* found = visual(visual, host.window()->contentItem()))
            return found;
        auto* found = host.window()->findChild<QObject*>(QLatin1String(name));
        return found && !qobject_cast<QQuickItem*>(found) ? found : nullptr;
    };
    const auto item = [&object](const char* name) {
        return qobject_cast<QQuickItem*>(object(name));
    };
    const auto fail = [](const char* message) {
        std::fprintf(stderr, "Settings probe: %s\n", message);
        return 1;
    };
    const auto session = [&ui] { return ui.settingsSession(); };
    QAccessible::setActive(true);
    const auto namedControls = [](auto&& self, QQuickItem* parent) -> bool {
        if (parent->isVisible() && parent->isEnabled() &&
            (parent->inherits("QQuickAbstractButton") || parent->inherits("QQuickTextField") ||
             parent->inherits("QQuickTextArea") || parent->inherits("QQuickComboBox"))) {
            auto* accessible = QAccessible::queryAccessibleInterface(parent);
            if (!accessible || accessible->text(QAccessible::Name).trimmed().isEmpty()) {
                std::fprintf(stderr, "Settings control has no accessible name: %s (%s)\n",
                             parent->metaObject()->className(), qPrintable(parent->objectName()));
                return false;
            }
        }
        for (auto* child : parent->childItems())
            if (!self(self, child))
                return false;
        return true;
    };
    const auto activate = [&object](const char* name) {
        auto* control = object(name);
        return control && control->property("enabled").toBool() &&
               QMetaObject::invokeMethod(control, "clicked");
    };
    if (!waitFor([&] { return ui.settingsReady(); }) || !ui.navigate("settings") ||
        !waitFor([&] { return object("settings_general"); })) {
        std::fprintf(stderr, "ready=%d page=%s category=%d errors=%d root=%p general=%p\n",
                     ui.settingsReady(), qPrintable(ui.currentPage()),
                     session().value("category").toInt(), host.qmlErrorCount(),
                     object("settings_page"), object("settings_general"));
        return fail("General is the first category");
    }
    const auto initialGeneration = host.engineGeneration();
    const auto originalFont = QApplication::font();
    const char* categories[]{"settings_general", "settings_network", "settings_resources",
                             "settings_data", "settings_about"};
    QString output;
    for (const auto& arg : QCoreApplication::arguments())
        if (arg.startsWith("--settings-output="))
            output = arg.mid(18);
    if (!output.isEmpty() && !QDir().mkpath(output))
        return fail("capture directory can be created");
    if (!output.isEmpty()) {
        host.window()->hide();
        host.window()->show();
        if (!QTest::qWaitForWindowExposed(host.window(), 5000))
            return fail("native captures require an exposed window");
        std::printf("Settings capture: Qt %s platform=%s graphicsApi=%d dpr=%.2f\n", qVersion(),
                    qPrintable(QGuiApplication::platformName()),
                    int(host.window()->rendererInterface()->graphicsApi()),
                    host.window()->devicePixelRatio());
    }

    // Inspect real category instances at all representative widths, fonts and themes.
    for (const auto pixels : {14, 21, 32}) {
        auto font = originalFont;
        font.setPixelSize(pixels);
        QApplication::setFont(font);
        for (const auto size :
             {QSize(360, 480), QSize(600, 640), QSize(1000, 640), QSize(1440, 900)}) {
            host.resizeWindow(size);
            for (const auto& theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
                ui.setThemeMode(theme);
                for (int category = 0; category < 5; ++category) {
                    ui.setSettingsCategory(category);
                    QTest::qWait(15);
                    auto* tabs = item("settings_tabs");
                    auto* viewport = item("settings_tabs_viewport");
                    const auto tabName = QStringLiteral("settings_tab_%1").arg(category).toLatin1();
                    auto* tab = item(tabName.constData());
                    auto* body = item("settings_body");
                    auto* content =
                        body ? body->property("contentItem").value<QQuickItem*>() : nullptr;
                    const auto tabVisible = [&] {
                        return viewport && tab &&
                               viewport->boundingRect()
                                   .adjusted(-0.75, -0.75, 0.75, 0.75)
                                   .contains(tab->mapRectToItem(viewport, tab->boundingRect()));
                    };
                    // Native rendering can polish font-dependent widths one frame after loading.
                    if (tab && viewport)
                        waitFor(tabVisible);
                    if (!tabs || !viewport || !tab || !body || !content ||
                        !object(categories[category]) || body->height() < 48 || !tabVisible() ||
                        content->property("contentWidth").toReal() > content->width() + 1 ||
                        !test::checkScrollLayouts(host.window())) {
                        std::fprintf(
                            stderr,
                            "Geometry font=%d size=%dx%d category=%d body=%.1f tab=%.1f "
                            "viewport=%.1f x=%.1f contentX=%.1f contentWidth=%.1f theme=%s\n",
                            pixels, size.width(), size.height(), category,
                            body ? body->height() : -1, tab ? tab->width() : -1,
                            viewport ? viewport->width() : -1,
                            tab ? tab->mapToItem(viewport, QPointF{}).x() : -1,
                            viewport ? viewport->property("contentX").toReal() : -1,
                            viewport ? viewport->property("contentWidth").toReal() : -1,
                            qPrintable(theme));
                        if (!output.isEmpty())
                            host.window()->grabWindow().save(QDir(output).filePath("failure.png"));
                        return fail("one-line tabs and vertical content fit the viewport");
                    }
                    for (int other = 0; other < 5; ++other)
                        if (other != category && object(categories[other]))
                            return fail("only the current category is instantiated");
                    if (!namedControls(namedControls, host.window()->contentItem()))
                        return fail("all enabled settings controls have accessible names");
                    if (item("settings_more_pages")->isVisible() != (size.width() < 600))
                        return fail("compact secondary routes stay reachable");
                    if (!output.isEmpty() &&
                        (pixels == 14 || (pixels == 32 && size.width() == 360))) {
                        const auto name = QStringLiteral("%1_%2_%3_%4.png")
                                              .arg(theme)
                                              .arg(pixels)
                                              .arg(size.width())
                                              .arg(category);
                        if (!host.window()->grabWindow().save(QDir(output).filePath(name)))
                            return fail("native category capture succeeds");
                    }
                }
            }
        }
    }
    // Compact navigation exposes readable text, keyboard dismissal and every secondary route.
    for (const auto pixels : {14, 32}) {
        auto font = originalFont;
        font.setPixelSize(pixels);
        QApplication::setFont(font);
        host.resizeWindow({360, 480});
        for (const auto* page : {"connections", "rules", "logs", "tests"}) {
            if (!ui.navigate("settings") || !waitFor([&] { return item("settings_more_pages"); }))
                return fail("compact settings navigation loads");
            auto* more = item("settings_more_pages");
            if (!waitFor([&] {
                    return more->property("text").toString() ==
                               QCoreApplication::translate("main", "Other") &&
                           more->width() + 0.5 >= more->implicitWidth();
                }))
                return fail("compact navigation shows the complete Other label");
            more->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(host.window(), Qt::Key_Return);
            if (!waitFor(
                    [&] { return object("settings_pages_menu")->property("visible").toBool(); }))
                return fail("Other opens from the keyboard");
            if (!output.isEmpty() && QLatin1String(page) == "connections" &&
                !host.window()->grabWindow().save(
                    QDir(output).filePath(QStringLiteral("other_menu_%1.png").arg(pixels))))
                return fail("the compact Other menu capture succeeds");
            QTest::keyClick(host.window(), Qt::Key_Escape);
            if (!waitFor([&] { return more->hasActiveFocus(); }))
                return fail("dismissing Other restores keyboard focus");
            if (!activate("settings_more_pages"))
                return fail("Other can reopen");
            const auto name = QByteArray("settings_page_") + page;
            if (!waitFor(
                    [&] { return item(name.constData()) && item(name.constData())->isVisible(); }))
                return fail("secondary menu items are visible");
            auto* entry = item(name.constData());
            if (entry->width() + 0.5 < entry->implicitWidth())
                return fail("secondary menu labels fit at large fonts");
            QTest::mouseClick(host.window(), Qt::LeftButton, Qt::NoModifier,
                              entry->mapToScene(entry->boundingRect().center()).toPoint());
            if (!waitFor([&] { return ui.currentPage() == QLatin1String(page); }))
                return fail("all compact secondary routes are reachable");
        }
    }
    ui.navigate("settings");
    QTest::qWait(25);
    // A long recovery warning must scroll with the form, even at the minimum window size.
    if (!waitFor([&] { return ui.settingsReady(); }))
        return fail("preference saves finish before the read-only fixture");
    const auto healthyControl = QJsonObject::fromVariantMap(ui.runtime());
    auto readOnlyControl = healthyControl;
    readOnlyControl.insert("readOnly", true);
    ui.applyControl(readOnlyControl);
    host.resizeWindow({360, 480});
    for (int category = 0; category < 5; ++category) {
        ui.setSettingsCategory(category);
        QTest::qWait(25);
        if (!item("settings_storage_warning")->isVisible() ||
            item("settings_body")->height() < 48 || !test::checkScrollLayouts(host.window()))
            return fail("read-only warnings preserve a usable scroll viewport");
    }
    if (!activate("settings_open_recovery") || !waitFor([&] { return object("settings_data"); }))
        return fail("read-only settings have a direct recovery route");
    if (!waitFor([&] { return !object("settings_page")->property("recoveryRequested").toBool(); }))
        return fail("recovery navigation waits for the laid-out native frame");
    auto* recoveryScroll = item("settings_body")->property("contentItem").value<QQuickItem*>();
    auto* recovery = item("settings_recovery");
    if (!recoveryScroll->boundingRect().intersects(
            recovery->mapRectToItem(recoveryScroll, recovery->boundingRect())))
        return fail("the recovery entry reveals its actual recovery group");
    if (!output.isEmpty() &&
        !host.window()->grabWindow().save(QDir(output).filePath("readonly_32_360.png")))
        return fail("read-only recovery capture succeeds");
    ui.applyControl(healthyControl);
    // All maintenance confirmations remain bounded, lazy and cancellable.
    struct DialogCase {
        int category;
        const char* loader;
    };
    for (const auto pixels : {14, 32}) {
        auto font = originalFont;
        font.setPixelSize(pixels);
        QApplication::setFont(font);
        host.resizeWindow({360, 480});
        for (const auto& entry : {DialogCase{2, "settings_loader_coreDialog"},
                                  DialogCase{2, "settings_loader_geoDownloadDialog"},
                                  DialogCase{2, "settings_loader_geoRollbackDialog"},
                                  DialogCase{2, "settings_loader_geoPendingDialog"},
                                  DialogCase{2, "settings_loader_geoBundledDialog"},
                                  DialogCase{1, "settings_loader_networkDialog"},
                                  DialogCase{3, "settings_loader_maintenanceDialog"},
                                  DialogCase{3, "settings_loader_backupDialog"},
                                  DialogCase{3, "settings_loader_restoreDialog"}}) {
            ui.setSettingsCategory(entry.category);
            QTest::qWait(20);
            auto* loader = object(entry.loader);
            if (!loader || !loader->setProperty("active", true))
                return fail("maintenance confirmation can be loaded on demand");
            QPointer<QObject> popup = loader->property("item").value<QObject*>();
            if (!popup || !QMetaObject::invokeMethod(popup, "open"))
                return fail("maintenance confirmation can be opened");
            QTest::qWait(25);
            if (!popup->property("visible").toBool() ||
                !namedControls(namedControls, host.window()->contentItem()) ||
                !test::checkVisibleFocusFrames(host.window()))
                return fail("maintenance confirmations fit narrow windows and have names");
            QMetaObject::invokeMethod(popup, "reject");
            QTest::qWait(20);
            if (popup)
                return fail("closed confirmation releases its QML instance");
        }
    }
    QApplication::setFont(originalFont);
    host.resizeWindow({360, 480});
    ui.setSettingsCategory(0);
    QTest::qWait(25);
    auto* tabs = item("settings_tabs");
    if (!tabs->property("overflowing").toBool() || !activate("settings_tabs_next") ||
        session().value("category").toInt() != 0)
        return fail("scroll arrows move the strip without selecting a category");
    auto* strip = item("settings_tabs_viewport");
    strip->setProperty("contentX", 0);
    const auto point =
        strip->mapToScene(QPointF(strip->width() * 0.8, strip->height() / 2)).toPoint();
    const std::unique_ptr<QPointingDevice> touch(QTest::createTouchDevice());
    QTest::touchEvent(host.window(), touch.get()).press(0, point, host.window());
    for (int step = 1; step <= 5; ++step) {
        QTest::touchEvent(host.window(), touch.get())
            .move(0, point - QPoint(step * 18, 0), host.window());
        QTest::qWait(15);
    }
    QTest::touchEvent(host.window(), touch.get()).release(0, point - QPoint(90, 0), host.window());
    QTest::qWait(20);
    if (session().value("category").toInt() != 0 || strip->property("contentX").toReal() <= 0)
        return fail("touch dragging scrolls the strip without activating a tab");
    ui.setSettingsCategory(0);
    item("settings_tab_0")->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(host.window(), Qt::Key_End);
    if (session().value("category").toInt() != 0 || !item("settings_tab_4")->hasActiveFocus())
        return fail("End focuses the final category without activating it");
    for (const auto width : {1000, 360}) {
        host.resizeWindow({width, 480});
        QTest::qWait(25);
        auto* focused = item("settings_tab_4");
        if (!strip->boundingRect().contains(focused->mapRectToItem(strip, focused->boundingRect())))
            return fail("resizing keeps the focused tab fully visible");
    }
    QTest::keyClick(host.window(), Qt::Key_Home);
    if (session().value("category").toInt() != 0 || !item("settings_tab_0")->hasActiveFocus())
        return fail("Home focuses the first category without activating it");
    QTest::keyClick(host.window(), Qt::Key_Right);
    if (session().value("category").toInt() != 0 || !item("settings_tab_1")->hasActiveFocus())
        return fail("arrow navigation moves focus without activating tabs");
    QTest::keyClick(host.window(), Qt::Key_Return);
    if (!waitFor([&] { return object("settings_network"); }))
        return fail("Enter activates the focused tab");
    QPointer<QObject> network = object("settings_network");
    ui.editSetting("mixedPort", "18765");
    ui.editSetting("target", "https://example.org/settings-probe");
    const auto confirmation = ui.prepareNetworkSettings();
    for (const auto width : {1000, 600, 599, 360, 1000, 360}) {
        host.resizeWindow({width, 640});
        QTest::qWait(20);
        if (!network || object("settings_network") != network ||
            session().value("network").toMap().value("mixedPort") != "18765" ||
            ui.prepareNetworkSettings() != confirmation)
            return fail("resizing retains category, draft and transaction identity");
    }
    auto* body = item("settings_body");
    auto* scroll = body->property("contentItem").value<QQuickItem*>();
    for (const auto* name : {"delay_target", "controller_port", "mixed_port"}) {
        auto* field = item(name);
        field->forceActiveFocus(Qt::TabFocusReason);
        QTest::qWait(25);
        if (!scroll->boundingRect().contains(field->mapRectToItem(scroll, field->boundingRect())))
            return fail("focusing a field reveals it inside the vertical viewport");
    }
    scroll->setProperty("contentY", 500);
    QTest::qWait(20);
    const auto position = ui.settingsPosition(1);
    ui.setSettingsCategory(4);
    QTest::qWait(20);
    if (network || object("mixed_port"))
        return fail("leaving Network releases its controls");
    ui.setSettingsCategory(1);
    QTest::qWait(30);
    if (session().value("network").toMap().value("mixedPort") != "18765" ||
        ui.settingsPosition(1) < position - 1 || !ui.navigate("home") || !ui.navigate("settings"))
        return fail("draft and scroll position survive category and page navigation");
    host.unloadWindow();
    if (!host.showWindow() || !waitFor([&] { return object("settings_network"); }) ||
        host.engineGeneration() != initialGeneration + 1 ||
        session().value("target") != "https://example.org/settings-probe" ||
        object("mixed_port")->property("text") != "18765")
        return fail("host drafts survive full QML unload and recreation");
    ui.editSetting("mixedPort", "80");
    if (object("apply_network_settings")->property("enabled").toBool() ||
        !ui.prepareNetworkSettings().isEmpty())
        return fail("invalid port prevents confirmation");
    ui.editSetting("mixedPort", "18765");
    const auto old = ui.prepareNetworkSettings();
    ui.editSetting("mixedPort", "18766");
    if (ui.submitNetworkSettings(old) || !session().value("networkDirty").toBool())
        return fail("stale confirmation is rejected and input retained");
    ui.editSetting("mode", "global");
    if (!activate("suggest_listener_ports") ||
        !waitFor([&] { return object("settings_port_confirmation"); }))
        return fail("port suggestion opens a review dialog");
    const auto savedBefore = ui.runtime().value("settingsState").toMap().value("saved");
    QMetaObject::invokeMethod(object("settings_port_confirmation"), "accept");
    QTest::qWait(40);
    if (ui.runtime().value("settingsState").toMap().value("saved") != savedBefore ||
        !session().value("networkDirty").toBool())
        return fail("accepting a port suggestion fills only the draft");
    if (!activate("apply_network_settings") || !object("network_settings_confirmation"))
        return fail("unique network action opens confirmation");
    QMetaObject::invokeMethod(object("network_settings_confirmation"), "accept");
    ui.navigate("home");
    if (!waitFor([&] { return session().value("networkResult") == "Saved"; }) ||
        session().value("networkDirty").toBool() || !session().value("targetDirty").toBool())
        return fail("stopped-core save completes off page and leaves target draft intact");
    if (!ui.submitDelayTarget() ||
        !waitFor([&] { return session().value("targetResult") == "Saved"; }) ||
        ui.hasSettingsDraft())
        return fail("independent target save reconciles its own draft");
    ui.editSetting("target", "https://example.org/unsaved");
    host.quit();
    if (!waitFor([&] { return object("settings_quit_confirmation"); }))
        return fail("actual quit asks about unsubmitted input");
    QMetaObject::invokeMethod(object("settings_quit_confirmation"), "reject");
    QTest::qWait(30);
    if (!ui.hasSettingsDraft() || session().value("target") != "https://example.org/unsaved")
        return fail("cancelling quit retains the draft");
    if (host.qmlErrorCount() != 0)
        return fail("settings produce no QML errors");
    host.quit();
    if (!waitFor([&] { return object("settings_quit_confirmation"); }))
        return fail("quit confirmation can be reopened");
    QPointer<QQuickWindow> closingWindow = host.window();
    QMetaObject::invokeMethod(object("settings_quit_confirmation"), "accept");
    if (!waitFor([&] { return closingWindow.isNull(); }) || ui.hasSettingsDraft())
        return fail("accepting quit discards unsubmitted input and unloads the window");
    std::puts("Settings: five lazy categories; 4 sizes x 3 fonts x 2 themes; manual tabs; host "
              "drafts; independent saves; accessible controls; lazy dialogs; quit paths passed");
    return 0;
}
} // namespace quickclash
