#include "presentation/localization.h"
#include <QCoreApplication>

static void initializeTranslations() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(quickclash_translations);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace quickclash::presentation {
QString Localization::languageFor(const QStringList& preferences) {
    for (const auto& preference : preferences) {
        const auto code = preference.toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
        const auto language = code.section(QLatin1Char('-'), 0, 0);
        if (language == QStringLiteral("zh") || language == QStringLiteral("en")) {
            return language;
        }
    }
    return QStringLiteral("en");
}
bool Localization::load(const QStringList& preferences) {
    initializeTranslations();
    QCoreApplication::removeTranslator(&application_);
    QCoreApplication::removeTranslator(&qtBase_);
    QCoreApplication::removeTranslator(&qtDeclarative_);
    language_ = languageFor(preferences);
    if (language_ == QStringLiteral("en"))
        return true;
    if (!application_.load(QStringLiteral(":/i18n/quickclash_zh_cn.qm"))) {
        language_ = QStringLiteral("en");
        return false;
    }
    if (qtBase_.load(QStringLiteral(":/i18n/qtbase_zh_cn.qm")))
        QCoreApplication::installTranslator(&qtBase_);
    if (qtDeclarative_.load(QStringLiteral(":/i18n/qtdeclarative_zh_cn.qm")))
        QCoreApplication::installTranslator(&qtDeclarative_);
    return QCoreApplication::installTranslator(&application_);
}
} // namespace quickclash::presentation
