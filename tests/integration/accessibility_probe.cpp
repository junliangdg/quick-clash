#include "application_host.h"
#include "focus_layout_checks.h"
#include "host_probe.h"
#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <cstdio>
#ifdef Q_OS_WIN
#include "platform/process_memory.h"
#endif

namespace quickclash {
namespace {
bool tabTo(QQuickWindow* window, const QString& name) {
    for (int attempt = 0; attempt < 160; ++attempt) {
        if (window->activeFocusItem() && window->activeFocusItem()->objectName() == name)
            return true;
        QTest::keyClick(window, Qt::Key_Tab);
        QTest::qWait(1);
    }
    return false;
}
QQuickItem* findItem(QQuickWindow* window, const QString& name) {
    QQuickItem* result = nullptr;
    const auto visit = [&](auto&& self, QQuickItem* item) -> void {
        if (item->objectName() == name)
            result = item;
        if (!result)
            for (auto* child : item->childItems())
                self(self, child);
    };
    visit(visit, window->contentItem());
    return result;
}
bool auditNames(QQuickWindow* window, QJsonArray& checked) {
    bool passed = true;
    const auto visit = [&](auto&& self, QQuickItem* item) -> void {
        const QByteArray type(item->metaObject()->className());
        QString kind;
        for (const auto* base : {"QQuickAbstractButton", "QQuickTextField", "QQuickTextArea",
                                 "QQuickComboBox", "QQuickSpinBox"})
            if (item->inherits(base)) {
                kind = QString::fromLatin1(base);
                break;
            }
        if (item->isVisible() && item->isEnabled() && !kind.isEmpty()) {
            auto* accessible = QAccessible::queryAccessibleInterface(item);
            const auto name = accessible ? accessible->text(QAccessible::Name) : QString{};
            if (name.trimmed().isEmpty()) {
                std::fprintf(stderr, "Missing accessible name: %s (%s), text=%s\n",
                             type.constData(), item->objectName().toUtf8().constData(),
                             item->property("text").toString().toUtf8().constData());
                passed = false;
            }
            checked.append(QJsonObject{{"type", QString::fromUtf8(type)},
                                       {"kind", kind},
                                       {"object", item->objectName()},
                                       {"name", name},
                                       {"role", accessible ? int(accessible->role()) : -1}});
        }
        for (auto* child : item->childItems())
            self(self, child);
    };
    visit(visit, window->contentItem());
    return passed;
}
} // namespace
int runAccessibilityProbe(ApplicationHost& host) {
    const auto option = [](const QString& key) {
        for (const auto& argument : QCoreApplication::arguments())
            if (argument.startsWith(key + '='))
                return argument.mid(key.size() + 1);
        return QString{};
    };
    const auto output = option(QStringLiteral("--accessibility-output"));
    if (!output.isEmpty() && !QDir().mkpath(output))
        return 150;
    const auto require = [](bool condition, const char* message) {
        if (!condition)
            std::fprintf(stderr, "Accessibility probe: %s\n", message);
        return condition;
    };
    auto& ui = host.state();
    ui.setReplay(false);
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "accessibility"},
                     {"selectedId", "accessible_profile"},
                     {"status", "Stopped"},
                     {"preferences", QJsonObject{{"mixedPort", 7890}, {"controllerPort", 9090}}}});
    const auto longName =
        QString::fromUtf8("香港 Tokyo · Long subscription and proxy name ").repeated(3);
    ui.applyData({1,
                  "accessibility",
                  {{"profiles", QJsonArray{QJsonObject{{"id", "accessible_profile"},
                                                       {"name", longName},
                                                       {"secretRef", "opaque"},
                                                       {"pendingApply", true}}}},
                   {"groups", QJsonArray{QJsonObject{{"name", "Accessible group"},
                                                     {"type", "Selector"},
                                                     {"canSelect", true},
                                                     {"current", longName},
                                                     {"members", QJsonArray{longName, "DIRECT"}}}}},
                   {"nodes", QJsonArray{QJsonObject{
                                 {"name", longName}, {"type", "VLESS"}, {"udp", true}}}}}});
    ui.saveState("proxies:accessible_profile", {{"groups", QStringList{"Accessible group"}}});
    QAccessible::setActive(true);
    auto* window = host.window();
    window->hide();
    window->show();
    if (!QTest::qWaitForWindowExposed(window, 5000))
        return 151;
    const auto generation = host.engineGeneration();
    auto* tokens = window->findChild<QObject*>(QStringLiteral("theme_tokens"));
    if (!tokens)
        return 152;
    QJsonArray names, captures;
    bool focusSafe = true;
    const auto capture = [&](const QString& name) {
        if (output.isEmpty())
            return true;
        const auto frame = window->grabWindow();
        captures.append(QJsonObject{{"name", name},
                                    {"width", window->width()},
                                    {"height", window->height()},
                                    {"dpr", window->devicePixelRatio()},
                                    {"frameWidth", frame.width()},
                                    {"frameHeight", frame.height()}});
        return !frame.isNull() && frame.save(QDir(output).filePath(name + ".png"));
    };
    const auto baselineFont = QApplication::font();
    for (const auto pixels : {14, 21, 32}) {
        auto font = baselineFont;
        font.setPixelSize(pixels);
        QApplication::setFont(font);
        for (const auto width : {1000, 360}) {
            host.resizeWindow({width, width == 360 ? 640 : 800});
            ui.setThemeMode(width == 360 ? "dark" : "light");
            for (const auto& page : {"home", "proxies", "subscriptions", "connections", "rules",
                                     "logs", "tests", "settings"}) {
                if (!ui.navigate(QString::fromLatin1(page)))
                    return 153;
                std::printf("Checking font=%d width=%d page=%s\n", pixels, width, page);
                QTest::qWait(35);
                if (QString::fromLatin1(page) == "subscriptions") {
                    auto* subscriptions =
                        window->findChild<QObject*>(QStringLiteral("subscriptions_page"));
                    if (!subscriptions || !QMetaObject::invokeMethod(subscriptions, "scrollToRow",
                                                                     Q_ARG(QVariant, 0)))
                        return 172;
                    QTest::qWait(30);
                }
                if (!require(host.contentReady() && host.engineGeneration() == generation,
                             "font change preserves the engine and loads all routes") ||
                    !auditNames(window, names))
                    return 154;
                if (!require(tokens->property("bodySize").toInt() == pixels &&
                                 tokens->property("controlHeight").toInt() >= pixels * 2,
                             "shared typography and control geometry follow the host font"))
                    return 155;
                if (QString::fromLatin1(page) == "settings" &&
                    !capture(QStringLiteral("font_%1_width_%2_settings").arg(pixels).arg(width)))
                    return 156;
                focusSafe = test::checkVisibleFocusFrames(window) && focusSafe;
                if (QString::fromLatin1(page) == "proxies") {
                    auto* hint = findItem(window, "custom_group_hint");
                    if (!hint || !hint->parentItem() ||
                        hint->width() > hint->parentItem()->width() + 0.5 ||
                        hint->height() + 0.5 < hint->implicitHeight())
                        return 198;
                }
                if (QString::fromLatin1(page) == "subscriptions") {
                    auto* popup = window->findChild<QObject*>("subscription_import_dialog");
                    if (!popup || !QMetaObject::invokeMethod(popup, "open"))
                        return 195;
                    QTest::qWait(40);
                    if (!popup->property("visible").toBool() || !auditNames(window, names) ||
                        !test::checkVisibleFocusFrames(window) ||
                        !test::focusInScroll(findItem(window, "subscription_url")) ||
                        !test::focusInScroll(findItem(window, "import_local_file")))
                        return 196;
                    if ((pixels == 14 || width == 360) &&
                        !capture(QStringLiteral("import_font_%1_width_%2").arg(pixels).arg(width)))
                        return 197;
                    QMetaObject::invokeMethod(popup, "close");
                    QTest::qWait(25);
                }
                if (QString::fromLatin1(page) == "settings") {
                    for (const auto* name :
                         {"core_update_confirmation", "network_settings_confirmation",
                          "maintenance_confirmation", "backup_confirmation",
                          "backup_restore_confirmation"}) {
                        auto* popup = window->findChild<QObject*>(QString::fromLatin1(name));
                        if (!popup || !QMetaObject::invokeMethod(popup, "open"))
                            return 187;
                        QTest::qWait(25);
                        if (!popup->property("visible").toBool())
                            return 188;
                        focusSafe = test::checkVisibleFocusFrames(window) && focusSafe;
                        if (width == 360 && QString::fromLatin1(name) == "backup_confirmation" &&
                            !capture(
                                QStringLiteral("backup_font_%1_width_%2").arg(pixels).arg(width)))
                            return 156;
                        QMetaObject::invokeMethod(popup, "reject");
                        QTest::qWait(25);
                    }
                }
                if (pixels == 32 && width == 360 &&
                    (QString::fromLatin1(page) == "subscriptions" ||
                     QString::fromLatin1(page) == "proxies") &&
                    !capture(QStringLiteral("font_32_width_360_%1").arg(QString::fromLatin1(page))))
                    return 156;
            }
        }
    }
    if (!require(focusSafe, "all route focus frames fit their clipping ancestors and window"))
        return 185;
    if (!require(names.size() >= 100, "name audit visits controls from all routes"))
        return 174;
    for (const auto* expected :
         {"appearance_dark", "website_route", "website_targets", "mixed_port", "controller_port"}) {
        bool found = false;
        for (const auto& value : names)
            found |= value.toObject().value("object").toString() == QString::fromLatin1(expected);
        if (!require(found, expected))
            return 175;
    }
    QApplication::setFont(baselineFont);
    host.resizeWindow({1000, 800});
    const auto supportedControl = QJsonObject::fromVariantMap(ui.runtime());
    auto unsupportedControl = supportedControl;
    unsupportedControl.insert("capabilities",
                              QJsonObject{{"systemProxy", "Unsupported"}, {"tun", "Unsupported"}});
    ui.applyControl(unsupportedControl);
    for (const auto* page : {"home", "settings"}) {
        if (!ui.navigate(QString::fromLatin1(page)))
            return 185;
        QTest::qWait(30);
        for (const auto* feature : {"system_proxy", "tun"}) {
            auto* control = findItem(host.window(), QString::fromLatin1(page) + '_' + feature);
            if (!require(control && !control->isEnabled(),
                         "unsupported platform disables network switches"))
                return 186;
        }
    }
    ui.applyControl(supportedControl);
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window, 5000))
        return 173;
    if (!ui.navigate("tests"))
        return 157;
    QTest::qWait(80);
    if (!require(tabTo(window, "website_route"), "tab reaches request path"))
        return 158;
    QPointer<QQuickItem> route = findItem(window, "website_route");
    if (!route)
        return 159;
    QTest::keyClick(window, Qt::Key_Down);
    if (!require(route->property("currentIndex").toInt() == 1, "arrow changes combo selection"))
        return 160;
    QTest::keyClick(window, Qt::Key_Space);
    QTest::qWait(30);
    auto* popup = route->property("popup").value<QObject*>();
    if (!require(popup && popup->property("visible").toBool(), "space opens combo popup"))
        return 161;
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(30);
    if (!require(!popup->property("visible").toBool() && route->hasActiveFocus(),
                 "escape restores combo focus"))
        return 162;
    QTest::keyClick(window, Qt::Key_Tab);
    if (!require(window->activeFocusItem() &&
                     window->activeFocusItem()->objectName() == "website_targets",
                 "tab reaches multiline targets"))
        return 163;
    QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier);
    if (!require(route->hasActiveFocus(), "shift-tab leaves multiline input"))
        return 164;
    auto* targets = findItem(window, "website_targets");
    targets->setProperty("tabInsertsText", true);
    QTest::keyClick(window, Qt::Key_Tab);
    QTest::keyClick(window, Qt::Key_Tab);
    if (!require(targets->hasActiveFocus() && targets->property("text").toString().contains('\t'),
                 "editor mode retains literal tab input"))
        return 170;
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
    if (!require(route->hasActiveFocus(), "control-shift-tab leaves editor mode"))
        return 171;
    QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier);
    QTest::qWait(50);
    auto* dialog = window->findChild<QObject*>(QStringLiteral("close_confirmation"));
    if (!require(dialog && dialog->property("visible").toBool() && window->activeFocusItem() &&
                     window->activeFocusItem()->objectName() == "close_cancel",
                 "confirmation defaults to cancel"))
        return 165;
    for (int index = 0; index < 8; ++index) {
        QTest::keyClick(window, Qt::Key_Tab);
        if (!require(window->activeFocusItem() && window->activeFocusItem() != route &&
                         !window->activeFocusItem()->objectName().startsWith("navigation_"),
                     "modal dialog contains keyboard focus"))
            return 166;
    }
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(50);
    if (!require(!dialog->property("visible").toBool() && route && route->hasActiveFocus(),
                 "closing modal restores the triggering control"))
        return 167;
    if (!ui.navigate("logs"))
        return 176;
    ui.applyControl({{"publicationGeneration", "1"},
                     {"epoch", "accessibility"},
                     {"status", "Stopped"},
                     {"features", QJsonObject{{"logExportPreview",
                                               QJsonObject{{"token", "probe"},
                                                           {"messagesHidden", true},
                                                           {"text", "Redacted preview"}}}}}});
    QTest::qWait(40);
    auto* exportDialog = window->findChild<QObject*>(QStringLiteral("log_export_dialog"));
    QPointer<QQuickItem> source = findItem(window, "log_source_filter");
    if (!exportDialog || !source)
        return 177;
    source->forceActiveFocus(Qt::TabFocusReason);
    QSignalSpy accepted(exportDialog, SIGNAL(accepted()));
    QSignalSpy rejected(exportDialog, SIGNAL(rejected()));
    if (!QMetaObject::invokeMethod(exportDialog, "open"))
        return 178;
    QTest::qWait(50);
    if (!auditNames(window, names))
        return 179;
    std::printf("Standard dialog initial focus: %s\n",
                window->activeFocusItem()
                    ? window->activeFocusItem()->property("text").toString().toUtf8().constData()
                    : "none");
    QTest::keyClick(window, Qt::Key_Return);
    QTest::qWait(50);
    std::printf("Standard dialog result: accepted=%lld rejected=%lld restored=%d\n",
                static_cast<long long>(accepted.size()), static_cast<long long>(rejected.size()),
                source->hasActiveFocus());
    if (!require(rejected.size() == 1 && accepted.isEmpty() && source->hasActiveFocus(),
                 "standard dialog enter cancels and restores focus"))
        return 180;
    QMetaObject::invokeMethod(exportDialog, "open");
    QTest::qWait(50);
    auto* preview = findItem(window, "log_export_preview");
    if (!preview)
        return 181;
    for (const auto pixels : {14, 21}) {
        auto font = baselineFont;
        font.setPixelSize(pixels);
        QApplication::setFont(font);
        for (const auto width : {1000, 360}) {
            host.resizeWindow({width, 800});
            QTest::qWait(40);
            if (!test::checkVisibleFocusFrames(window) || !test::focusInScroll(preview))
                return 189;
        }
    }
    QApplication::setFont(baselineFont);
    host.resizeWindow({1000, 800});
    QTest::qWait(40);
    if (!test::focusInScroll(preview))
        return 189;
    QTest::keyClick(window, Qt::Key_Tab);
    if (!require(!preview->hasActiveFocus() && !source->hasActiveFocus(),
                 "tab leaves read-only preview inside modal"))
        return 182;
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(50);
    auto enlargedFont = baselineFont;
    enlargedFont.setPixelSize(21);
    QApplication::setFont(enlargedFont);
    host.unloadWindow();
    if (!host.showWindow())
        return 183;
    window = host.window();
    tokens = window->findChild<QObject*>(QStringLiteral("theme_tokens"));
    if (!require(tokens && tokens->property("bodySize").toInt() == 21 && host.contentReady(),
                 "unloaded window rebuilds using the current host font"))
        return 184;
    QApplication::setFont(baselineFont);
    QJsonArray screens;
    for (auto* screen : QGuiApplication::screens())
        screens.append(QJsonObject{{"name", screen->name()},
                                   {"dpr", screen->devicePixelRatio()},
                                   {"logicalDpi", screen->logicalDotsPerInch()}});
    QJsonArray memory;
    if (QCoreApplication::arguments().contains(QStringLiteral("--accessibility-memory"))) {
        QElapsedTimer elapsed;
        elapsed.start();
        for (int index = 0; index < 3; ++index) {
            if (index)
                QTest::qWait(5000);
#ifdef Q_OS_WIN
            memory.append(QJsonObject{{"elapsedMs", elapsed.elapsed()},
                                      {"client", platform::currentProcessMemory()}});
#endif
        }
    }
    const QJsonObject report{{"passed", host.qmlErrorCount() == 0},
                             {"qtVersion", qVersion()},
                             {"platform", QGuiApplication::platformName()},
                             {"font", QFontInfo(QApplication::font()).family()},
                             {"graphicsApi", int(window->rendererInterface()->graphicsApi())},
                             {"screens", screens},
                             {"captures", captures},
                             {"accessibleControls", names},
                             {"memorySamples", memory},
                             {"scope",
                              "Synthetic UI and font injection; no core, service, OS "
                              "text-size change, physical screen migration or screen reader"}};
    if (!output.isEmpty()) {
        QFile file(QDir(output).filePath("results.json"));
        const auto bytes = QJsonDocument(report).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return 168;
    }
    std::printf("Accessibility: %lld named controls, 3 font sizes, 8 routes, wide/narrow, "
                "combo, modal cancel and focus restoration passed\n",
                static_cast<long long>(names.size()));
    return host.qmlErrorCount() == 0 ? 0 : 169;
}
} // namespace quickclash
