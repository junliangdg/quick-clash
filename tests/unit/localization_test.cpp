#include "presentation/localization.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    using quickclash::presentation::Localization;
    if (Localization::languageFor({"en-US", "zh-CN"}) != "en" ||
        Localization::languageFor({"fr-FR", "zh-Hant-TW"}) != "zh" ||
        Localization::languageFor({"zh_Hans_CN"}) != "zh" ||
        Localization::languageFor({"de-DE"}) != "en" || Localization::languageFor({}) != "en")
        return 1;
    Localization localization;
    if (!localization.load({"zh-CN"}) ||
        QCoreApplication::translate("FeatureRegistry", "Dashboard") !=
            QString::fromUtf8("\xe4\xbb\xaa\xe8\xa1\xa8\xe7\x9b\x98") ||
        QCoreApplication::translate("main", "Quit") !=
            QString::fromUtf8("\xe9\x80\x80\xe5\x87\xba"))
        return 2;
    if (!localization.load({"en-GB"}) ||
        QCoreApplication::translate("FeatureRegistry", "Dashboard") != "Dashboard")
        return 3;
    if (!localization.load({"zh-CN"}) ||
        QCoreApplication::translate("quickclash::presentation::UiState", "Unsupported protocol") ==
            "Unsupported protocol" ||
        QCoreApplication::translate("proxies_page", "Automatic failover · fallback") ==
            "Automatic failover · fallback")
        return 5;
    if (!localization.load({"ja-JP"}) || localization.language() != "en" ||
        QCoreApplication::translate("main", "Quit") != "Quit")
        return 4;
    std::cout << "Locale preference, Chinese catalog, English and unsupported fallback passed\n";
}
