#include "ui_font.h"
#include <QFontDatabase>
#include <QSettings>
#include <QtMath>
#include <algorithm>

namespace quickclash {
void configureUiFontBackend() {
#ifdef Q_OS_WIN
    // DirectWrite's fallback lookup can populate every installed font when
    // the UI font needs an emoji fallback. Qt's GDI database resolves that case
    // on demand, while the Quick scene graph keeps its default RHI backend.
    // Qt's explicit -platform argument also overrides this environment default.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "windows:fontengine=gdi");
#endif
}
QFont platformUiFont() {
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    int textPercent = 100;
#ifdef Q_OS_WIN
    if (QFontDatabase::hasFamily(QStringLiteral("Microsoft YaHei"))) {
        font.setFamily(QStringLiteral("Microsoft YaHei"));
        font.setStyleName(QString{});
        font.setStyleHint(QFont::SansSerif);
    }
    const QSettings accessibility(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Accessibility"),
        QSettings::NativeFormat);
    bool valid = false;
    const auto value = accessibility.value(QStringLiteral("TextScaleFactor"), 100).toInt(&valid);
    if (valid)
        textPercent = std::clamp(value, 100, 225);
#endif
    font.setPixelSize(qRound(14.0 * textPercent / 100));
    return font;
}
} // namespace quickclash
