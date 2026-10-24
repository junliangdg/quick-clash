#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QFontInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <memory>

// Diagnostic cost stages, never a replacement for full product measurements.
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    const auto arguments = app.arguments();
    const auto option = [&arguments](const QString& name, const QString& fallback) {
        for (const auto& argument : arguments) {
            if (argument.startsWith(name + QLatin1Char('='))) {
                return argument.mid(name.size() + 1);
            }
        }
        return fallback;
    };
    const auto stage = option(QStringLiteral("--stage"), QStringLiteral("application"));
    const auto fontMode = option(QStringLiteral("--font"), QStringLiteral("default"));
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (fontMode == QStringLiteral("system_cjk")) {
        font.setFamily(option(QStringLiteral("--family"), QStringLiteral("Microsoft YaHei UI")));
        font.setStyleName(QString{});
        font.setStyleHint(QFont::SansSerif);
    } else if (fontMode == QStringLiteral("application_cjk")) {
        const auto file =
            QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts/msyh.ttc"));
        const auto families =
            QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(file));
        if (families.isEmpty()) {
            return 2;
        }
        font.setFamily(families.contains(QStringLiteral("Microsoft YaHei UI"))
                           ? QStringLiteral("Microsoft YaHei UI")
                           : families.first());
        font.setStyleName(QString{});
        font.setStyleHint(QFont::SansSerif);
    } else if (fontMode != QStringLiteral("default")) {
        return 3;
    }
    QApplication::setFont(font);
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QQuickWindow> window;
    if (stage == QStringLiteral("quick_window")) {
        window = std::make_unique<QQuickWindow>();
    } else if (stage == QStringLiteral("controls")) {
        engine = std::make_unique<QQmlEngine>();
        QQmlComponent component(engine.get());
        component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
    visible: true
    color: "#f4f6fa"
    Rectangle {
        width: 200; height: parent.height; color: "#e8edf5"
        Label { anchors.centerIn: parent; text: "QuickClash 设置"; font.family: cjkFamily }
    }
    Label { x: 232; y: 32; text: "设置 / Settings"; font.family: cjkFamily }
    Button { x: 232; y: 84; text: "主题 / Theme"; font.family: cjkFamily }
    CheckBox { x: 232; y: 140; text: "回放 / Replay"; font.family: cjkFamily }
    property string cjkFamily
}
)",
                          QUrl(QStringLiteral("cost_probe.qml")));
        window.reset(qobject_cast<QQuickWindow*>(
            component.createWithInitialProperties({{QStringLiteral("cjkFamily"), font.family()}})));
        if (!window || component.isError()) {
            return 4;
        }
    } else if (stage != QStringLiteral("application")) {
        return 5;
    }
    if (window) {
        window->resize(1160, 780);
        window->show();
    }
    const auto diagnostics = option(QStringLiteral("--diagnostics"), QString{});
    const auto capture = option(QStringLiteral("--capture"), QString{});
    QTimer::singleShot(1000, &app, [&] {
        QJsonObject result{{QStringLiteral("stage"), stage},
                           {QStringLiteral("font_mode"), fontMode},
                           {QStringLiteral("requested_font"), font.family()},
                           {QStringLiteral("resolved_font"), QFontInfo(font).family()},
                           {QStringLiteral("cjk_present"),
                            QFontDatabase::hasFamily(QStringLiteral("Microsoft YaHei UI"))},
                           {QStringLiteral("qt_version"), QStringLiteral(QT_VERSION_STR)}};
        if (!diagnostics.isEmpty()) {
            QSaveFile file(diagnostics);
            if (!file.open(QIODevice::WriteOnly) ||
                file.write(QJsonDocument(result).toJson()) < 0 || !file.commit()) {
                app.exit(6);
            }
        }
        if (!capture.isEmpty() && (!window || !window->grabWindow().save(capture))) {
            app.exit(7);
        }
    });
    QTimer::singleShot(option(QStringLiteral("--quit-after-ms"), QStringLiteral("15000")).toInt(),
                       &app, &QCoreApplication::quit);
    return app.exec();
}
