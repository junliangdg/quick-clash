#pragma once
#include <QLocale>
#include <QStringList>
#include <QTranslator>

namespace quickclash::presentation {
// Install before constructing presentation objects; translators outlive every window.
class Localization final {
  public:
    [[nodiscard]] static QString languageFor(const QStringList& preferences);
    [[nodiscard]] bool load(const QStringList& preferences = QLocale::system().uiLanguages());
    [[nodiscard]] QString language() const {
        return language_;
    }

  private:
    QString language_ = QStringLiteral("en");
    QTranslator application_;
    QTranslator qtBase_;
    QTranslator qtDeclarative_;
};
} // namespace quickclash::presentation
