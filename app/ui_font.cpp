#include "ui_font.h"
#include <QFontDatabase>
#include <QSettings>
#include <QtMath>
#include <algorithm>

namespace quickclash {
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
