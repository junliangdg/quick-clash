#include "application_host.h"
#include "host_probe.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QTest>
#include <cmath>
#include <cstdio>

namespace quickclash {
int runVisualProbe(ApplicationHost& host) {
    auto& ui = host.state();
    // A helper launched with SW_HIDE needs a second visibility transition before
    // Windows exposes it. Never accept an unexposed native grab as visual evidence.
    host.window()->hide();
    host.window()->show();
    if (!QTest::qWaitForWindowExposed(host.window(), 5000))
        return 109;
    const auto option = [](const QString& key) {
        for (const auto& argument : QCoreApplication::arguments())
            if (argument.startsWith(key + '='))
                return argument.mid(key.size() + 1);
        return QString{};
    };
    const auto output = option(QStringLiteral("--visual-output"));
    if (!output.isEmpty() && !QDir().mkpath(output))
        return 110;
    const auto objects = [&host](const QString& name) {
        QList<QQuickItem*> result;
        const auto visit = [&](auto&& self, QQuickItem* item) -> void {
            if (item->objectName() == name)
                result.append(item);
            for (auto* child : item->childItems())
                self(self, child);
        };
        visit(visit, host.window()->contentItem());
        return result;
    };
    QJsonArray captures;
    const auto capture = [&](const QString& name) {
        QTest::qWait(100);
        const auto frame = host.window()->grabWindow();
        QJsonArray roots;
        for (auto* item : host.window()->contentItem()->childItems()) {
            roots.append(QJsonObject{{"class", item->metaObject()->className()},
                                     {"width", item->width()},
                                     {"height", item->height()},
                                     {"scale", item->scale()}});
        }
        QJsonArray primaryButtons;
        bool foregroundCorrect = true;
        const auto visitButtons = [&](auto&& self, QQuickItem* item) -> void {
            if (item->property("prominent").toBool()) {
                auto* content = item->property("contentItem").value<QObject*>();
                auto* theme = item->property("theme").value<QObject*>();
                if (item->isVisible() && item->isEnabled() && !item->property("pending").toBool()) {
                    const QColor expected(ui.dark() ? "#251a58" : "#ffffff");
                    foregroundCorrect =
                        foregroundCorrect && content && theme &&
                        content->property("color").value<QColor>() == expected &&
                        theme->property("accentForeground").value<QColor>() == expected;
                }
                primaryButtons.append(QJsonObject{
                    {"text", item->property("text").toString()},
                    {"accentForeground",
                     theme ? theme->property("accentForeground").toString() : "missing"},
                    {"contentClass", content ? content->metaObject()->className() : "missing"},
                    {"color", content ? content->property("color").toString() : "missing"}});
            }
            for (auto* child : item->childItems())
                self(self, child);
        };
        visitButtons(visitButtons, host.window()->contentItem());
        captures.append(QJsonObject{
            {"name", name},
            {"qtVersion", qVersion()},
            {"exposed", host.window()->isExposed()},
            {"roots", roots},
            {"primaryButtons", primaryButtons},
            {"width", host.window()->width()},
            {"height", host.window()->height()},
            {"contentWidth", host.window()->contentItem()->width()},
            {"contentHeight", host.window()->contentItem()->height()},
            {"dpr", host.window()->devicePixelRatio()},
            {"frameWidth", frame.width()},
            {"frameHeight", frame.height()},
            {"graphicsApi", static_cast<int>(host.window()->rendererInterface()->graphicsApi())}});
        return host.contentReady() && foregroundCorrect && !frame.isNull() &&
               host.window()->contentItem()->size() == QSizeF(host.window()->size()) &&
               (output.isEmpty() || frame.save(QDir(output).filePath(name + ".png")));
    };
    QJsonArray members, nodes, groups;
    for (int i = 1; i <= 6; ++i) {
        const auto name = QStringLiteral("日本高速 · %1 / Tokyo").arg(i, 2, 10, QLatin1Char('0'));
        members.append(name);
        nodes.append(QJsonObject{{"name", name}, {"type", "Trojan"}, {"udp", true}});
    }
    groups.append(QJsonObject{{"name", "节点选择"},
                              {"type", "Selector"},
                              {"members", members},
                              {"current", members.first()},
                              {"canSelect", true}});
    groups.append(QJsonObject{{"name", "自动选择"},
                              {"type", "URLTest"},
                              {"members", members},
                              {"current", members[1]},
                              {"canSelect", false}});
    ui.applyControl(
        {{"publicationGeneration", "1"},
         {"epoch", "visual-fixture"},
         {"ready", true},
         {"status", "Ready"},
         {"selectedId", "visual"},
         {"selectedName", "日常订阅 · Everyday"},
         {"coreVersion", "fixture"},
         {"systemProxy", false},
         {"tunState", "Off"},
         {"preferences",
          QJsonObject{{"mode", "rule"}, {"mixedPort", 7890}, {"controllerPort", 9090}}},
         {"outbound", QJsonObject{{"policy", "节点选择"},
                                  {"node", members.first()},
                                  {"confirmed", true},
                                  {"mode", "rule"},
                                  {"chain", QJsonArray{"节点选择", members.first()}}}}});
    const QJsonObject profile{
        {"id", "visual"},
        {"name", "日常订阅 · Everyday"},
        {"secretRef", "fixture"},
        {"updatedAt", "2026-10-27T06:00:00Z"},
        {"autoApply", true},
        {"usage", QJsonObject{{"total", 107374182400.0}, {"download", 21474836480.0}}}};
    const auto populate = [&] {
        ui.applyData({1,
                      "visual-fixture",
                      {{"profiles", QJsonArray{profile}},
                       {"groups", groups},
                       {"nodes", nodes},
                       {"delays", QJsonObject{{members.first().toString(), "38 ms"},
                                              {members[1].toString(), "Failed"}}},
                       {"connections",
                        QJsonArray{QJsonObject{{"id", "one"},
                                               {"epoch", "visual-fixture"},
                                               {"metadata", QJsonObject{{"host", "example.com"},
                                                                        {"destinationPort", 443},
                                                                        {"network", "tcp"}}},
                                               {"chains", QJsonArray{"节点选择", members.first()}},
                                               {"rule", "DOMAIN"}}}},
                       {"rules", QJsonArray{QJsonObject{{"type", "DOMAIN-SUFFIX"},
                                                        {"payload", "example.com"},
                                                        {"proxy", "节点选择"}}}},
                       {"traffic",
                        QJsonObject{{"traffic",
                                     QJsonObject{{"time", "60"}, {"up", 9300}, {"down", 45500}}}}},
                       {"logs", QJsonArray{QJsonObject{
                                    {"localSeq", "1"},
                                    {"time", "14:00:00"},
                                    {"source", "App"},
                                    {"level", "info"},
                                    {"message", "Synthetic visual fixture · 界面验证数据"}}}}},
                      ui.dataDemands().generations});
    };
    host.resizeWindow({1000, 640});
    populate();
    for (int second = 1; second <= 60; ++second) {
        const double rate = 1024 * (20 + 12 * std::sin(second * 0.4));
        ui.applyData(
            {1,
             "visual-fixture",
             {{"traffic", QJsonObject{{"traffic", QJsonObject{{"time", QString::number(second)},
                                                              {"up", rate},
                                                              {"down", rate * 4 + 8192}}}}}},
             ui.dataDemands().generations});
    }
    const auto generation = host.engineGeneration();
    for (const auto& theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        ui.setThemeMode(theme);
        for (const auto& entry : ui.navigation()) {
            const auto page = entry.toMap().value("pageId").toString();
            if (!ui.navigate(page))
                return 111;
            populate();
            if (!capture(theme + '_' + page))
                return 112;
        }
        ui.navigate("home");
        QTest::qWait(50);
        const auto subscription = objects("current_subscription_card");
        const auto traffic = objects("traffic_card");
        if (subscription.size() != 1 || traffic.size() != 1 ||
            std::abs(subscription.first()->height() - traffic.first()->height()) > 1)
            return 113;
        ui.navigate("settings");
        QTest::qWait(50);
        const auto fields = objects("delay_target");
        if (fields.size() != 1 || !fields.first()->setProperty("text", "https://draft.example/"))
            return 114;
        const auto ports = objects("mixed_port");
        if (ports.size() != 1)
            return 122;
        auto* editor =
            qobject_cast<QQuickItem*>(ports.first()->property("contentItem").value<QObject*>());
        if (!editor)
            return 122;
        editor->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(host.window(), Qt::Key_A, Qt::ControlModifier);
        for (const auto digit : QStringLiteral("17990"))
            QTest::keyClick(host.window(), static_cast<Qt::Key>(digit.unicode()));
        QTest::keyClick(host.window(), Qt::Key_Return);
        if (ports.first()->property("value").toInt() != 17990)
            return 122;
        auto* currentPage = fields.first()->parentItem();
        ui.setThemeMode(theme == "light" ? "dark" : "light");
        QTest::qWait(20);
        if (fields.first()->property("text") != "https://draft.example/" ||
            ports.first()->property("value").toInt() != 17990 ||
            fields.first()->parentItem() != currentPage || host.engineGeneration() != generation)
            return 115;
        ui.setThemeMode(theme);
        fields.first()->forceActiveFocus(Qt::TabFocusReason);
        const auto combos = objects("theme_selector");
        if (combos.size() != 1)
            return 116;
        auto* popup = combos.first()->property("popup").value<QObject*>();
        if (!popup || !QMetaObject::invokeMethod(popup, "open") ||
            !capture(theme + "_theme_menu") || !popup->property("visible").toBool())
            return 116;
        QMetaObject::invokeMethod(popup, "close");
        ui.requestClose();
        auto* confirmation = host.window()->findChild<QObject*>("close_confirmation");
        if (!confirmation || !capture(theme + "_confirmation") ||
            !confirmation->property("visible").toBool())
            return 117;
        QMetaObject::invokeMethod(confirmation, "close");
        auto failed = ui.runtime();
        failed.insert("status", "Failed");
        failed.insert("ready", false);
        failed.insert("error", "Synthetic runtime failure");
        ui.applyControl(QJsonObject::fromVariantMap(failed));
        ui.navigate("home");
        if (!capture(theme + "_runtime_error"))
            return 123;
        const auto details = objects("runtime_error_detail");
        if (!host.window()->property("runtimeFailed").toBool() || details.size() != 1 ||
            !details.first()->isVisible())
            return 123;
        failed.insert("status", "Ready");
        failed.insert("ready", true);
        failed.remove("error");
        ui.applyControl(QJsonObject::fromVariantMap(failed));
        if (!host.window()->property("runtimeHealthy").toBool())
            return 124;
    }
    for (const auto width : {1200, 800, 360}) {
        host.resizeWindow({width, 640});
        ui.navigate("home");
        populate();
        if (!capture(QStringLiteral("dark_home_%1").arg(width)))
            return 118;
        if (width == 360) {
            ui.navigate("settings");
            if (!capture("dark_settings_360"))
                return 119;
        }
    }
    if (host.qmlErrorCount() || host.engineGeneration() != generation ||
        ui.uploadHistory().size() != 60)
        return 120;
    if (!output.isEmpty()) {
        QFile evidence(QDir(output).filePath("captures.json"));
        if (!evidence.open(QIODevice::WriteOnly))
            return 121;
        evidence.write(QJsonDocument(captures).toJson());
    }
    std::puts("Visual UI: eight pages, both themes, retained draft, equal cards, menu/dialog, "
              "360/800/1000/1200 passed");
    return 0;
}
} // namespace quickclash
