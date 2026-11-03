#include "application_host.h"
#include "focus_layout_checks.h"
#include "host_probe.h"
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QSignalSpy>
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
        QJsonArray scrollBars;
        for (auto* bar : objects("themed_vertical_scroll_bar")) {
            auto* view = bar->parentItem();
            if (!view || !view->isVisible())
                continue;
            const auto bounds = bar->mapRectToScene(bar->boundingRect());
            scrollBars.append(QJsonObject{{"view", view->objectName()},
                                          {"x", bounds.x()},
                                          {"y", bounds.y()},
                                          {"width", bounds.width()},
                                          {"height", bounds.height()},
                                          {"size", bar->property("size").toReal()},
                                          {"position", bar->property("position").toReal()}});
        }
        auto* title = host.window()->findChild<QQuickItem*>("page_title_row");
        const auto pageBounds = title ? title->mapRectToScene(title->boundingRect()) : QRectF{};
        captures.append(QJsonObject{
            {"name", name},
            {"pageContent", QJsonObject{{"left", pageBounds.left()},
                                        {"right", pageBounds.right()},
                                        {"width", pageBounds.width()}}},
            {"scrollBars", scrollBars},
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
         {"coreName", "mihomo"},
         {"coreVersion", "v1.19.33"},
         {"coreExecutionMode", "Local"},
         {"settingsState",
          QJsonObject{{"running", true}, {"effective", QJsonObject{{"mixedPort", 7890}}}}},
         {"systemProxy", false},
         {"tunState", "Off"},
         {"features",
          QJsonObject{
              {"ipInfo",
               QJsonObject{
                   {"v4",
                    QJsonObject{{"local", QJsonObject{{"state", "Ready"},
                                                      {"ip", "192.0.2.10"},
                                                      {"isp", "Example ISP"},
                                                      {"organization", "Documentation Network"},
                                                      {"country", "中国"},
                                                      {"city", "上海"},
                                                      {"source", "visual fixture"},
                                                      {"time", "2026-10-31T06:00:00Z"}}},
                                {"proxy", QJsonObject{{"state", "Ready"},
                                                      {"ip", "198.51.100.20"},
                                                      {"isp", "Example Transit"},
                                                      {"organization", "Documentation Exit"},
                                                      {"country", "日本"},
                                                      {"city", "东京"},
                                                      {"source", "visual fixture"},
                                                      {"time", "2026-10-31T06:00:00Z"}}}}}}},
              {"testHistory",
               QJsonArray{QJsonObject{{"target", "https://www.google.com/generate_204"},
                                      {"path", "CurrentProxy"},
                                      {"state", "Reachable"},
                                      {"elapsedMs", 182},
                                      {"status", 204},
                                      {"time", "2026-10-31T06:00:00Z"}},
                          QJsonObject{{"target", "https://www.youtube.com/generate_204"},
                                      {"path", "CurrentProxy"},
                                      {"state", "Timeout"},
                                      {"elapsedMs", 5000},
                                      {"time", "2026-10-31T06:00:00Z"}}}}}},
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
        {"usageStatus", QJsonObject{{"complete", true},
                                    {"used", 21474836480.0},
                                    {"quota", 107374182400.0},
                                    {"expiresAt", "2026-12-31T06:00:00Z"}}},
        {"usage", QJsonObject{{"total", 107374182400.0}, {"download", 21474836480.0}}}};
    const auto profileVisible = [&](const QString& name, int index, const QString& id) {
        const auto selectors = objects("home_profile_selector");
        const auto details = objects("home_subscription_details");
        if (selectors.size() != 1 || details.size() != 1) {
            qWarning() << "Missing dashboard subscription controls" << selectors.size()
                       << details.size();
            return false;
        }
        auto* content = selectors.first()->property("contentItem").value<QQuickItem*>();
        const auto rendered = content && !content->childItems().isEmpty()
                                  ? content->childItems().first()->property("text").toString()
                                  : QString{};
        const auto actualId = details.first()->property("profile").toMap().value("id").toString();
        const auto actualIndex = selectors.first()->property("currentIndex").toInt();
        const auto displayText = selectors.first()->property("displayText").toString();
        if (actualIndex != index || displayText != name || rendered != name || actualId != id) {
            qWarning() << "Dashboard subscription mismatch: expected" << name << index << id
                       << "actual" << displayText << rendered << actualIndex << actualId;
            return false;
        }
        return true;
    };
    const auto populate = [&] {
        ui.applyData(
            {1,
             "visual-fixture",
             {{"profiles", QJsonArray{profile}},
              {"groups", groups},
              {"nodes", nodes},
              {"samples", QJsonObject{{"coreUptimeSeconds", 9300}}},
              {"resources",
               QJsonObject{
                   {"memory", QJsonObject{{"client", QJsonObject{{"privateWorkingSet", "62400000"},
                                                                 {"workingSet", "92000000"},
                                                                 {"privateCommit", "120000000"}}},
                                          {"host", QJsonObject{{"privateWorkingSet", "62400000"},
                                                               {"workingSet", "92000000"},
                                                               {"privateCommit", "120000000"}}},
                                          {"core", QJsonObject{{"privateWorkingSet", "28100000"},
                                                               {"workingSet", "40000000"},
                                                               {"privateCommit", "60000000"}}}}},
                   {"memoryComplete", true},
                   {"memorySample", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}},
              {"delays", QJsonObject{{members.first().toString(), "38 ms"},
                                     {members[1].toString(), "Failed"}}},
              {"delay_details",
               QJsonObject{
                   {members.first().toString(),
                    QJsonObject{{"latest", QJsonObject{{"state", "Success"},
                                                       {"delayMs", 38},
                                                       {"source", "Core"},
                                                       {"targetHash", "visual-target"},
                                                       {"time", "2026-11-01T05:00:00Z"}}}}},
                   {members[1].toString(),
                    QJsonObject{{"latest", QJsonObject{{"state", "Failed"},
                                                       {"source", "Core"},
                                                       {"targetHash", "visual-target"},
                                                       {"time", "2026-11-01T05:00:00Z"}}}}}}},
              {"connections",
               QJsonArray{QJsonObject{{"id", "one"},
                                      {"epoch", "visual-fixture"},
                                      {"metadata", QJsonObject{{"host", "example.com"},
                                                               {"destinationPort", 443},
                                                               {"network", "tcp"}}},
                                      {"chains", QJsonArray{"节点选择", members.first()}},
                                      {"rule", "DOMAIN-SUFFIX"},
                                      {"rulePayload", "example.com"},
                                      {"downloadRate", 15482880},
                                      {"uploadRate", 2031616},
                                      {"download", 104857600},
                                      {"upload", 2097152}}}},
              {"rules", QJsonArray{QJsonObject{{"position", 1},
                                               {"type", "DOMAIN-SUFFIX"},
                                               {"payload", "example.com"},
                                               {"proxy", "节点选择"}}}},
              {"traffic",
               QJsonObject{
                   {"traffic",
                    QJsonObject{{"time", QString::number(QDateTime::currentMSecsSinceEpoch())},
                                {"up", 9300},
                                {"down", 45500},
                                {"upTotal", 1048576},
                                {"downTotal", 104857600}}}}},
              {"logs",
               QJsonArray{QJsonObject{{"localSeq", "1"},
                                      {"time", "14:00:00"},
                                      {"source", "App"},
                                      {"level", "info"},
                                      {"message", "Synthetic visual fixture · 界面验证数据"}}}}},
             ui.dataDemands().generations});
        const auto now = QDateTime::currentMSecsSinceEpoch();
        for (int second = 1; second <= 60; ++second) {
            const double rate = 1024 * (20 + 12 * std::sin(second * 0.4));
            ui.applyData(
                {1,
                 "visual-fixture",
                 {{"traffic",
                   QJsonObject{{"traffic",
                                QJsonObject{{"time", QString::number(now - (60 - second) * 1000)},
                                            {"up", rate},
                                            {"down", rate * 4 + 8192},
                                            {"upTotal", 1048576},
                                            {"downTotal", 104857600}}}}}},
                 ui.dataDemands().generations});
        }
    };
    host.resizeWindow({1000, 640});
    ui.navigate("home");
    QTest::qWait(50);
    // Control state can arrive before profiles; the selected name must still be drawn.
    if (!profileVisible("日常订阅 · Everyday", -1, ""))
        return 132;
    const QJsonObject alternate{{"id", "alternate"}, {"name", "备用订阅"}};
    const auto applyProfiles = [&](const QJsonArray& profiles) {
        ui.applyData({1, "visual-fixture", {{"profiles", profiles}}, ui.dataDemands().generations});
        QTest::qWait(20);
    };
    applyProfiles({alternate, profile});
    if (!profileVisible("日常订阅 · Everyday", 1, "visual"))
        return 133;
    const auto originalControl = ui.runtime();
    auto changedControl = originalControl;
    changedControl.insert("selectedName", "");
    ui.applyControl(QJsonObject::fromVariantMap(changedControl));
    if (!profileVisible("日常订阅 · Everyday", 1, "visual"))
        return 134;
    auto renamed = profile;
    renamed.insert("name", "已更新订阅");
    renamed.insert("secretRef", "updated-fixture");
    applyProfiles({alternate, renamed});
    if (!profileVisible("已更新订阅", 1, "visual") ||
        objects("home_subscription_details")
                .first()
                ->property("profile")
                .toMap()
                .value("secretRef") != "updated-fixture")
        return 135;
    applyProfiles({renamed, alternate});
    if (!profileVisible("已更新订阅", 0, "visual"))
        return 136;
    changedControl.insert("selectedId", "alternate");
    changedControl.insert("selectedName", "备用订阅");
    ui.applyControl(QJsonObject::fromVariantMap(changedControl));
    if (!profileVisible("备用订阅", 1, "alternate"))
        return 137;
    ui.navigate("settings");
    QTest::qWait(50);
    ui.navigate("home");
    QTest::qWait(50);
    if (!profileVisible("备用订阅", 1, "alternate"))
        return 138;
    changedControl.insert("selectedId", "");
    changedControl.insert("selectedName", "");
    ui.applyControl(QJsonObject::fromVariantMap(changedControl));
    const auto emptySelectors = objects("home_profile_selector");
    const auto emptyDetails = objects("home_subscription_details");
    if (emptySelectors.size() != 1 || emptyDetails.size() != 1 ||
        emptySelectors.first()->property("displayText").toString().isEmpty() ||
        !profileVisible(emptySelectors.first()->property("displayText").toString(), -1, "") ||
        emptyDetails.first()->isVisible())
        return 139;
    ui.applyControl(QJsonObject::fromVariantMap(originalControl));
    populate();
    // The nested delay controls must not also select a node or toggle the group.
    ui.navigate("proxies");
    populate();
    QTest::qWait(80);
    const auto group = objects(QStringLiteral("proxy_group_节点选择"));
    const auto tiles =
        objects(QStringLiteral("proxy_member_节点选择_") + members.first().toString());
    if (group.size() != 1 || tiles.size() != 1)
        return 189;
    auto* batch = group.first()->findChild<QQuickItem*>("test_group");
    auto* delay = tiles.first()->findChild<QQuickItem*>("test_node");
    auto* groupTitle = group.first()->findChild<QQuickItem*>("proxy_group_title");
    auto* groupSubtitle = group.first()->findChild<QQuickItem*>("proxy_group_subtitle");
    if (!batch || !delay || !groupTitle || !groupSubtitle || !batch->isEnabled() ||
        !delay->isEnabled() || delay->width() < 44 || delay->height() < 32 ||
        std::abs(groupTitle->mapToScene(QPointF{}).x() - groupSubtitle->mapToScene(QPointF{}).x()) >
            1 ||
        std::abs(groupTitle->mapToScene(QPointF(0, groupTitle->height() / 2)).y() -
                 batch->mapToScene(QPointF(0, batch->height() / 2)).y()) > 1)
        return 190;
    QSignalSpy groupClicked(group.first(), SIGNAL(clicked()));
    QSignalSpy batchClicked(batch, SIGNAL(clicked()));
    QSignalSpy tileClicked(tiles.first(), SIGNAL(clicked()));
    QSignalSpy delayClicked(delay, SIGNAL(clicked()));
    const auto clickItem = [&](QQuickItem* item) {
        QTest::mouseClick(
            host.window(), Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        QTest::qWait(20);
    };
    clickItem(delay);
    if (delayClicked.size() != 1 || !tileClicked.isEmpty() || !groupClicked.isEmpty())
        return 191;
    delay->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(host.window(), Qt::Key_Return);
    if (delayClicked.size() != 2 || !tileClicked.isEmpty())
        return 192;
    clickItem(batch);
    if (batchClicked.size() != 1 || !groupClicked.isEmpty())
        return 193;
    batch->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(host.window(), Qt::Key_Return);
    if (batchClicked.size() != 2 || !groupClicked.isEmpty())
        return 194;
    const auto generation = host.engineGeneration();
    for (const auto& theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        ui.setThemeMode(theme);
        const auto brand = objects("brand_mark");
        const auto expectedBrand = theme == "dark"
                                       ? QStringLiteral("qrc:/icons/brand/qcat_tray_light.svg")
                                       : QStringLiteral("qrc:/icons/brand/qcat_tray_dark.svg");
        if (brand.size() != 1 || brand.first()->property("source").toUrl() != QUrl(expectedBrand))
            return 196;
        for (const auto& entry : ui.navigation()) {
            const auto page = entry.toMap().value("pageId").toString();
            if (!ui.navigate(page))
                return 111;
            populate();
            if (!capture(theme + '_' + page) || !test::checkScrollLayouts(host.window()) ||
                !test::checkPageAlignment(host.window()))
                return 112;
            if (page == "settings") {
                const auto versions = objects("application_version");
                const auto views = objects("settings_page");
                const auto expectedVersion =
                    QCoreApplication::translate("settings_page", "Version: %1")
                        .arg(QCoreApplication::applicationVersion());
                if (QCoreApplication::applicationVersion().isEmpty() || versions.size() != 1 ||
                    views.size() != 1 ||
                    versions.first()->property("text").toString() != expectedVersion)
                    return 197;
                auto* viewport = views.first()->property("contentItem").value<QQuickItem*>();
                if (!viewport ||
                    !viewport->setProperty("contentY",
                                           viewport->property("contentHeight").toReal() -
                                               viewport->height()) ||
                    !capture(theme + "_settings_about"))
                    return 198;
                const auto bounds =
                    versions.first()->mapRectToScene(versions.first()->boundingRect());
                const auto viewportBounds = viewport->mapRectToScene(viewport->boundingRect());
                if (!viewportBounds.contains(bounds))
                    return 199;
            }
        }
        ui.navigate("subscriptions");
        QTest::qWait(30);
        auto* importPopup = host.window()->findChild<QObject*>("subscription_import_dialog");
        if (!importPopup || !QMetaObject::invokeMethod(importPopup, "open") ||
            !capture(theme + "_subscription_import"))
            return 195;
        QMetaObject::invokeMethod(importPopup, "close");
        QTest::qWait(30);
        ui.navigate("home");
        QTest::qWait(50);
        const auto peakTime = QDateTime::currentMSecsSinceEpoch();
        for (int second = 0; second < 60; ++second) {
            const auto up = second % 4 == 0 ? 1073741824 : second % 4 == 2 ? 1 : 0;
            const auto down = second % 4 == 2 ? 1073741824 : 0;
            ui.applyData(
                {1,
                 "visual-fixture",
                 {{"traffic",
                   QJsonObject{{"traffic",
                                QJsonObject{{"time", QString::number(peakTime + second * 1000LL)},
                                            {"up", up},
                                            {"down", down}}}}}},
                 ui.dataDemands().generations});
        }
        if (!capture(theme + "_traffic_extreme"))
            return 112;
        ui.applyData(
            {1,
             "visual-fixture",
             {{"traffic",
               QJsonObject{{"traffic", QJsonObject{{"time", QString::number(peakTime + 63000)},
                                                   {"up", 1073741824},
                                                   {"down", 0}}}}}},
             ui.dataDemands().generations});
        if (!capture(theme + "_traffic_gap"))
            return 112;
        populate();
        const auto subscription = objects("current_subscription_card");
        const auto traffic = objects("traffic_card");
        if (subscription.size() != 1 || traffic.size() != 1 ||
            std::abs(subscription.first()->height() - traffic.first()->height()) > 1 ||
            std::abs(subscription.first()->width() - traffic.first()->width()) > 1 ||
            objects("home_proxy_card").size() != 1 || objects("home_tools_card").size() != 1 ||
            objects("home_system_card").size() != 1 ||
            !ui.dataDemands().active.contains("resources") ||
            ui.dataDemands().active.contains("connections"))
            return 113;
        const auto selector = objects("home_proxy_selector");
        if (selector.size() != 1 || !selector.first()->isVisible())
            return 125;
        const auto delayValues = objects("home_delay_value");
        if (!selector.first()->property("displayText").toString().contains("38 ms") ||
            selector.first()->property("currentText").toString() != members.first().toString() ||
            delayValues.size() != 1 || delayValues.first()->property("text") != "38 ms" ||
            ui.proxyState(members.first().toString()).value("description").toString().isEmpty())
            return 131;
        const auto proxyCards = objects("home_proxy_card");
        auto* proxyViewport = proxyCards.first()->parentItem();
        while (proxyViewport && !proxyViewport->inherits("QQuickFlickable"))
            proxyViewport = proxyViewport->parentItem();
        if (!proxyViewport)
            return 131;
        proxyViewport->setProperty("contentY", proxyCards.first()->y());
        if (!capture(theme + "_proxy_card"))
            return 131;
        auto* proxyPopup = selector.first()->property("popup").value<QObject*>();
        if (!proxyPopup || !QMetaObject::invokeMethod(proxyPopup, "open") ||
            !capture(theme + "_proxy_dropdown"))
            return 131;
        QMetaObject::invokeMethod(proxyPopup, "close");
        proxyViewport->setProperty("contentY", 0);
        const auto resourceGeneration = ui.dataDemands().generations.value("resources");
        ui.setDashboardOption("resources", true);
        QTest::qWait(20);
        if (!ui.dataDemands().active.contains("resources"))
            return 126;
        ui.setDashboardOption("resources", false);
        QTest::qWait(20);
        if (!ui.dataDemands().active.contains("resources") ||
            ui.dataDemands().generations.value("resources") != resourceGeneration)
            return 126;
        populate();
        QTest::qWait(20);
        const auto memory = objects("home_runtime_client");
        const auto copy = objects("home_copy_proxy_address");
        if (memory.size() != 1 || memory.first()->property("text") != "62.4 MB" ||
            copy.size() != 1 || !copy.first()->isEnabled())
            return 129;
        const auto overviewText = [&](const QString& name) {
            const auto labels = objects(name);
            return labels.size() == 1 ? labels.first()->property("text").toString() : QString{};
        };
        const auto confirmed = QJsonObject::fromVariantMap(ui.runtime());
        auto pending = confirmed;
        pending.insert("preferences", QJsonObject{{"mixedPort", 17999}});
        ui.applyControl(pending);
        QTest::qWait(20);
        if (!overviewText("home_proxy_address").endsWith("127.0.0.1:7890"))
            return 130;
        pending.insert("settingsState", QJsonObject{{"running", true}});
        ui.applyControl(pending);
        QTest::qWait(20);
        if (copy.first()->isEnabled() || overviewText("home_proxy_address").contains("17999"))
            return 130;
        ui.applyControl(confirmed);
        const auto applyResources = [&](const QJsonObject& resources) {
            ui.applyData(
                {1, "visual-fixture", {{"resources", resources}}, ui.dataDemands().generations});
            QTest::qWait(20);
        };
        QJsonObject partial{
            {"memory", QJsonObject{{"client", QJsonObject{}},
                                   {"core", QJsonObject{{"privateWorkingSet", "28100000"}}}}},
            {"memoryComplete", false},
            {"memorySample", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
        applyResources(partial);
        if (overviewText("home_runtime_client") != "—" ||
            overviewText("home_runtime_core") != "28.1 MB" ||
            !overviewText("home_resource_status")
                 .contains(QCoreApplication::translate("runtime_overview", "Partial statistics")))
            return 131;
        partial.insert("memorySample",
                       QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs));
        applyResources(partial);
        if (overviewText("home_runtime_core") != "—" ||
            !overviewText("home_resource_status")
                 .contains(
                     QCoreApplication::translate("runtime_overview", "Resource data is stale")))
            return 131;
        partial.remove("memorySample");
        applyResources(partial);
        if (overviewText("home_runtime_core") != "—")
            return 131;
        populate();
        ui.setUiVisible(false);
        if (ui.dataDemands().active.contains("resources"))
            return 129;
        ui.setUiVisible(true);
        QTest::qWait(20);
        if (!ui.dataDemands().active.contains("resources"))
            return 129;
        ui.navigate("settings");
        QTest::qWait(50);
        if (ui.dataDemands().active.contains("resources"))
            return 129;
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
        const auto appearance = objects(theme == "light" ? "appearance_dark" : "appearance_light");
        if (appearance.size() != 1 || !QMetaObject::invokeMethod(appearance.first(), "clicked"))
            return 115;
        QTest::qWait(20);
        if (fields.first()->property("text") != "https://draft.example/" ||
            ports.first()->property("value").toInt() != 17990 ||
            fields.first()->parentItem() != currentPage || host.engineGeneration() != generation)
            return 115;
        const auto restoreAppearance = objects("appearance_" + theme);
        if (restoreAppearance.size() != 1 ||
            !QMetaObject::invokeMethod(restoreAppearance.first(), "clicked"))
            return 115;
        ui.navigate("home");
        QTest::qWait(50);
        const auto combos = objects("home_profile_selector");
        if (combos.size() != 1 ||
            combos.first()->property("currentText").toString() != "日常订阅 · Everyday" ||
            !profileVisible("日常订阅 · Everyday", 0, "visual"))
            return 116;
        auto* popup = combos.first()->property("popup").value<QObject*>();
        if (!popup || !QMetaObject::invokeMethod(popup, "open") ||
            !capture(theme + "_profile_menu") || !popup->property("visible").toBool())
            return 116;
        QMetaObject::invokeMethod(popup, "close");
        ui.requestClose();
        auto* confirmation = host.window()->findChild<QObject*>("close_confirmation");
        if (!confirmation || !capture(theme + "_confirmation") ||
            !confirmation->property("visible").toBool())
            return 117;
        QMetaObject::invokeMethod(confirmation, "close");
        ui.navigate("home");
        QTest::qWait(50);
        const auto pageHost = objects("active_page_host");
        if (pageHost.size() != 1)
            return 123;
        const auto pageBounds = pageHost.first()->mapRectToScene(pageHost.first()->boundingRect());
        auto failed = ui.runtime();
        failed.insert("status", "Failed");
        failed.insert("ready", false);
        failed.insert("error", "Synthetic runtime failure");
        ui.applyControl(QJsonObject::fromVariantMap(failed));
        if (!capture(theme + "_runtime_error"))
            return 123;
        if (objects("home_copy_proxy_address").first()->isEnabled() ||
            objects("home_runtime_core").first()->property("text") != "—")
            return 129;
        const auto* tooltip = host.window()->findChild<QObject*>("runtime_status_tooltip");
        if (!host.window()->property("runtimeFailed").toBool() || !tooltip ||
            tooltip->property("text").toString() != ui.runtimeError() ||
            pageHost.first()->mapRectToScene(pageHost.first()->boundingRect()) != pageBounds ||
            !objects("runtime_error_detail").isEmpty() || !objects("theme_selector").isEmpty())
            return 123;
        const auto badges = objects("runtime_status_badge");
        if (badges.size() != 1)
            return 123;
        badges.first()->forceActiveFocus(Qt::TabFocusReason);
        QTest::qWait(550);
        if (!tooltip->property("visible").toBool() || !capture(theme + "_runtime_error_tooltip"))
            return 123;
        failed.insert("status", "Ready");
        failed.insert("ready", true);
        failed.remove("error");
        ui.applyControl(QJsonObject::fromVariantMap(failed));
        if (!host.window()->property("runtimeHealthy").toBool())
            return 124;
    }
    for (const auto width : {1440, 1200, 800, 360}) {
        host.resizeWindow({width, 640});
        ui.navigate("home");
        populate();
        const auto cards = objects("current_subscription_card");
        if (cards.size() != 1)
            return 118;
        auto* homeViewport = cards.first()->parentItem();
        while (homeViewport && !homeViewport->inherits("QQuickFlickable"))
            homeViewport = homeViewport->parentItem();
        if (!homeViewport)
            return 118;
        homeViewport->setProperty("contentY", 0);
        if (!capture(QStringLiteral("dark_home_%1").arg(width)))
            return 118;
        const auto sections = objects("home_proxy_sections");
        if (sections.size() != 1 ||
            sections.first()->property("columns").toInt() != (width == 1440 ? 2 : 1))
            return 128;
        if (width == 360) {
            ui.navigate("settings");
            if (!capture("dark_settings_360"))
                return 119;
        }
        ui.navigate("home");
        QTest::qWait(50);
        populate();
        const auto proxyPanels = objects("home_proxy_card");
        if (proxyPanels.size() != 1)
            return 127;
        auto* proxyViewport = proxyPanels.first()->parentItem();
        while (proxyViewport && !proxyViewport->inherits("QQuickFlickable"))
            proxyViewport = proxyViewport->parentItem();
        if (!proxyViewport)
            return 127;
        proxyViewport->setProperty("contentY", proxyPanels.first()->y());
        if (!capture(QStringLiteral("dark_proxy_card_%1").arg(width)))
            return 127;
        const auto panels = objects("home_tools_card");
        if (panels.size() != 1)
            return 127;
        QQuickItem* viewport = panels.first()->parentItem();
        while (viewport && !viewport->inherits("QQuickFlickable"))
            viewport = viewport->parentItem();
        if (!viewport)
            return 127;
        viewport->setProperty("contentY", panels.first()->y());
        if (!capture(QStringLiteral("dark_tools_%1").arg(width)))
            return 127;
        if (width == 1440 || width == 360) {
            ui.setThemeMode("light");
            if (!capture(QStringLiteral("light_tools_%1").arg(width)))
                return 127;
            const auto sites = objects("home_sites_section");
            if (sites.size() != 1)
                return 127;
            viewport->setProperty("contentY", panels.first()->y() + sites.first()->y());
            if (!capture(QStringLiteral("light_sites_%1").arg(width)))
                return 127;
            ui.setThemeMode("dark");
            if (!capture(QStringLiteral("dark_sites_%1").arg(width)))
                return 127;
        }
        const auto overview = objects("home_system_card");
        if (overview.size() != 1)
            return 129;
        viewport->setProperty("contentY", overview.first()->y());
        if (!capture(QStringLiteral("dark_overview_%1").arg(width)))
            return 129;
        if (width == 1440 || width == 360) {
            ui.setThemeMode("light");
            if (!capture(QStringLiteral("light_overview_%1").arg(width)))
                return 129;
            ui.setThemeMode("dark");
            ui.setDashboardOption("resources", true);
            if (!capture(QStringLiteral("dark_overview_details_%1").arg(width)))
                return 129;
            ui.setDashboardOption("resources", false);
        }
    }
    // Inspect the real bar after resize and content scrolling, not just the form's padding.
    for (const auto& theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        ui.setThemeMode(theme);
        for (const auto width : {1000, 360}) {
            host.resizeWindow({width, 640});
            for (const auto& page : {QStringLiteral("home"), QStringLiteral("settings")}) {
                if (!ui.navigate(page))
                    return 140;
                QTest::qWait(40);
                const auto targets = objects(page == "home" ? "home_system_card" : "settings_page");
                if (targets.size() != 1)
                    return 140;
                auto* view = targets.first();
                while (view && !view->property("form").isValid())
                    view = view->parentItem();
                auto* bar =
                    view ? view->findChild<QQuickItem*>("themed_vertical_scroll_bar") : nullptr;
                auto* viewport =
                    view ? view->property("contentItem").value<QQuickItem*>() : nullptr;
                if (!view || !bar || !viewport || !test::checkScrollLayouts(host.window()))
                    return 140;
                auto* title = host.window()->findChild<QQuickItem*>("page_title_row");
                auto* form = view->property("form").value<QQuickItem*>();
                if (!title || !form || !test::checkPageAlignment(host.window()))
                    return 145;
                const auto bodyWidth = form->width();
                const auto heading = title->mapRectToScene(title->boundingRect());
                const auto body = form->mapRectToScene(form->boundingRect());
                const auto track = bar->mapRectToScene(bar->boundingRect());
                auto* tokens = view->property("theme").value<QObject*>();
                if (!tokens)
                    return 145;
                const auto focusExtent = tokens->property("focusExtent").toReal();
                if (track.left() + 0.5 < body.right() + focusExtent ||
                    track.right() > host.window()->width() - 1.5 ||
                    std::abs(heading.left() - body.left()) > 0.5 ||
                    std::abs(heading.right() - body.right()) > 0.5)
                    return 145;
                const auto policy = bar->property("policy");
                if (!bar->setProperty("policy", 1)) // ScrollBar.AlwaysOff
                    return 146;
                QTest::qWait(25);
                if (bar->isVisible() || std::abs(form->width() - bodyWidth) > 0.5 ||
                    !test::checkPageAlignment(host.window()) || !bar->setProperty("policy", policy))
                    return 146;
                QTest::qWait(25);
                if (!bar->isVisible() || std::abs(form->width() - bodyWidth) > 0.5 ||
                    !test::checkPageAlignment(host.window()))
                    return 146;
                const auto contentHeight = viewport->property("contentHeight").toReal();
                const auto origin = viewport->property("originY").toReal();
                const auto range = contentHeight - viewport->height();
                if (range <= 0 || !bar->isVisible())
                    return 141;
                viewport->setProperty("contentY", origin);
                QTest::qWait(15);
                if (!QMetaObject::invokeMethod(bar, "increase"))
                    return 142;
                QTest::qWait(15);
                if (viewport->property("contentY").toReal() <= origin ||
                    !QMetaObject::invokeMethod(bar, "decrease"))
                    return 142;
                QTest::qWait(15);
                if (std::abs(viewport->property("contentY").toReal() - origin) > 0.5)
                    return 142;
                for (const auto fraction : {0.0, 0.5, 1.0}) {
                    viewport->setProperty("contentY", origin + range * fraction);
                    QTest::qWait(15);
                    if (!test::checkScrollLayouts(host.window()) ||
                        std::abs(bar->property("size").toReal() -
                                 viewport->height() / contentHeight) > 0.001 ||
                        std::abs(bar->property("position").toReal() -
                                 range * fraction / contentHeight) > 0.001)
                        return 143;
                }
                if (page == "settings") {
                    const auto versions = objects("application_version");
                    if (versions.size() != 1 ||
                        !viewport->mapRectToScene(viewport->boundingRect())
                             .contains(versions.first()->mapRectToScene(
                                 versions.first()->boundingRect())) ||
                        !capture(QStringLiteral("%1_settings_about_%2").arg(theme).arg(width)))
                        return 199;
                }
                viewport->setProperty("contentY", origin + range / 2);
                bar->setProperty("active", true);
                if (!capture(QStringLiteral("%1_scrollbar_%2_%3").arg(theme, page).arg(width)))
                    return 144;
            }
        }
    }
    // Page-level virtual and proxy lists also keep their contents stable when the
    // bar is hidden; their own list/scroll semantics remain unchanged.
    for (const auto width : {1000, 360}) {
        host.resizeWindow({width, 640});
        for (const auto& page : {QStringLiteral("subscriptions"), QStringLiteral("proxies")}) {
            if (!ui.navigate(page))
                return 147;
            QTest::qWait(40);
            const auto bars =
                objects(page == "subscriptions" ? "subscription_page_bar" : "proxy_page_bar");
            const auto bodies = objects(page == "subscriptions" ? "subscription_content_header"
                                                                : "proxy_content_column");
            if (bars.size() != 1 || bodies.size() != 1 || !test::checkPageAlignment(host.window()))
                return 147;
            const auto widthBefore = bodies.first()->width();
            const auto policy = bars.first()->property("policy");
            if (!bars.first()->setProperty("policy", 1))
                return 147;
            QTest::qWait(20);
            if (bars.first()->isVisible() ||
                std::abs(bodies.first()->width() - widthBefore) > 0.5 ||
                !test::checkPageAlignment(host.window()) ||
                !bars.first()->setProperty("policy", policy))
                return 147;
            QTest::qWait(20);
            if (std::abs(bodies.first()->width() - widthBefore) > 0.5 ||
                !test::checkPageAlignment(host.window()))
                return 147;
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
              "360/800/1000/1200/1440, home/settings scrollbar geometry and interaction passed");
    return 0;
}
} // namespace quickclash
