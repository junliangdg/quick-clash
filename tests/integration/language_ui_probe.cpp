#include "language_ui_probe.h"
#include "application_host.h"
#include <QQuickItem>
#include <QTest>
#include <cstdio>

namespace quickclash {
int runLanguageUiProbe(ApplicationHost& host) {
    auto& ui = host.state();
    const auto generation = host.engineGeneration();
    const QString chineseSettings = QString::fromUtf8("\xe8\xae\xbe\xe7\xbd\xae");
    const QString chineseLanguage = QString::fromUtf8("\xe8\xaf\xad\xe8\xa8\x80");
    const QString chineseFollowSystem =
        QString::fromUtf8("\xe8\xb7\x9f\xe9\x9a\x8f\xe7\xb3\xbb\xe7\xbb\x9f");
    const auto waitFor = [](auto condition) {
        for (int attempt = 0; attempt < 60; ++attempt) {
            if (condition())
                return true;
            QTest::qWait(10);
        }
        return condition();
    };
    const auto text = [&host](const QString& objectName) {
        const auto* object = host.window()->findChild<QObject*>(objectName);
        return object ? object->property("text").toString() : QString{};
    };
    const auto fail = [](const char* message) {
        std::fprintf(stderr, "Language probe: %s\n", message);
        return false;
    };
    const auto click = [&host](const QString& objectName) {
        auto* object = host.window()->findChild<QObject*>(objectName);
        return object && QMetaObject::invokeMethod(object, "clicked");
    };

    if (!waitFor([&] { return ui.settingsReady(); }) || !ui.navigate(QStringLiteral("settings")) ||
        !waitFor([&] { return host.contentReady(); })) {
        return fail("settings page is ready");
    }
    if (!click(QStringLiteral("appearance_language_en"))) {
        return fail("English language control is available");
    }
    if (!waitFor([&] {
            return ui.languageMode() == QStringLiteral("en") &&
                   text(QStringLiteral("navigation_settings")) == QStringLiteral("Settings") &&
                   text(QStringLiteral("appearance_language_heading")) ==
                       QStringLiteral("Language");
        })) {
        return fail("English selection updates the interface");
    }
    if (ui.pageTitle() != QStringLiteral("Settings") ||
        text(QStringLiteral("appearance_language_en")) != QStringLiteral("English") ||
        text(QStringLiteral("appearance_language_zh")) !=
            QString::fromUtf8("\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87") ||
        text(QStringLiteral("application_about_heading")) != QStringLiteral("About") ||
        text(QStringLiteral("application_version")) !=
            QStringLiteral("Version: %1").arg(QCoreApplication::applicationVersion()) ||
        !host.window()
             ->findChild<QObject*>(QStringLiteral("appearance_language_en"))
             ->property("selected")
             .toBool()) {
        return fail("English labels and selected state are coherent");
    }

    if (!click(QStringLiteral("appearance_language_zh"))) {
        return fail("Chinese language control is available");
    }
    if (!waitFor([&] {
            return ui.languageMode() == QStringLiteral("zh") &&
                   text(QStringLiteral("navigation_settings")) == chineseSettings &&
                   text(QStringLiteral("appearance_language_heading")) == chineseLanguage;
        })) {
        return fail("Chinese selection updates the interface");
    }
    if (ui.pageTitle() != chineseSettings || ui.currentPage() != QStringLiteral("settings") ||
        text(QStringLiteral("appearance_language_system")) != chineseFollowSystem ||
        text(QStringLiteral("application_about_heading")) != QStringLiteral("关于") ||
        text(QStringLiteral("application_version")) !=
            QStringLiteral("版本：%1").arg(QCoreApplication::applicationVersion()) ||
        host.engineGeneration() != generation || host.qmlErrorCount() != 0) {
        return fail("Chinese selection keeps the current page and QML engine");
    }
    std::puts("Runtime language switch updated QML and navigation without rebuilding the engine");
    return 0;
}
} // namespace quickclash
