#include "language_ui_probe.h"
#include "application_host.h"
#include <QQuickItem>
#include <QTest>
#include <cstdio>

namespace quickclash {
int runLanguageUiProbe(ApplicationHost& host) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto& ui = host.state();
    const auto waitFor = [](auto condition) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (condition())
                return true;
            QTest::qWait(10);
        }
        return bool(condition());
    };
    const auto object = [&host](const char* name) {
        const auto visual = [name](auto&& self, QQuickItem* parent) -> QObject* {
            if (parent->objectName() == QLatin1String(name))
                return parent;
            for (auto* child : parent->childItems())
                if (auto* found = self(self, child))
                    return found;
            return nullptr;
        };
        return visual(visual, host.window()->contentItem());
    };
    const auto text = [&object](const char* name, const char* property = "text") {
        const auto* item = object(name);
        return item ? item->property(property).toString() : QString{};
    };
    const auto fail = [](const char* message) {
        std::fprintf(stderr, "Language probe: %s\n", message);
        return 1;
    };
    if (!waitFor([&] { return ui.settingsReady(); }) || !ui.navigate("settings") ||
        !waitFor([&] { return object("settings_language"); })) {
        std::fprintf(stderr, "ready=%d page=%s category=%d errors=%d root=%p general=%p\n",
                     ui.settingsReady(), qPrintable(ui.currentPage()),
                     ui.settingsSession().value("category").toInt(), host.qmlErrorCount(),
                     object("settings_page"), object("settings_general"));
        return fail("settings page is ready");
    }
    const auto generation = host.engineGeneration();
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
        const bool chinese = language == "zh";
        auto* combo = object("settings_language");
        const auto index = chinese ? 2 : 1;
        if (!combo || !combo->setProperty("currentIndex", index) ||
            !QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index)))
            return fail("language combo activation is available");
        if (!waitFor([&] {
                return ui.languageMode() == language &&
                       text("appearance_language_heading", "title") ==
                           (chinese ? QStringLiteral("语言") : QStringLiteral("Language"));
            }) ||
            ui.pageTitle() != (chinese ? QStringLiteral("设置") : QStringLiteral("Settings")) ||
            !object("settings_language") ||
            object("settings_language")->property("currentIndex").toInt() != index) {
            std::fprintf(stderr, "language=%s mode=%s heading=%s page=%s index=%d\n",
                         qPrintable(language), qPrintable(ui.languageMode()),
                         qPrintable(text("appearance_language_heading", "title")),
                         qPrintable(ui.pageTitle()),
                         object("settings_language")
                             ? object("settings_language")->property("currentIndex").toInt()
                             : -1);
            return fail("language selection updates labels and selected state");
        }
        ui.setSettingsCategory(4);
        if (!waitFor([&] { return object("application_version"); }) ||
            text("application_about_heading") !=
                (chinese ? QStringLiteral("关于") : QStringLiteral("About")) ||
            text("application_version") !=
                (chinese ? QStringLiteral("版本：%1") : QStringLiteral("Version: %1"))
                    .arg(QCoreApplication::applicationVersion()))
            return fail("lazy About category uses current language and full version");
        ui.setSettingsCategory(0);
        if (!waitFor([&] { return object("settings_language"); }))
            return fail("returning to General restores the language control");
    }
    if (ui.currentPage() != "settings" || host.engineGeneration() != generation ||
        host.qmlErrorCount() != 0) {
        std::fprintf(stderr, "page=%s generation=%d/%d errors=%d\n", qPrintable(ui.currentPage()),
                     host.engineGeneration(), generation, host.qmlErrorCount());
        return fail("language switching preserves the page and QML engine");
    }
    std::puts("Language combo and lazy categories retranslate without rebuilding the engine");
    return 0;
}
} // namespace quickclash
