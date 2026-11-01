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
    if (QCoreApplication::translate("quickclash::presentation::UiState", "Core probe") !=
            "内核探测" ||
        QCoreApplication::translate("quickclash::presentation::UiState", "Group probe target") !=
            "组探测目标")
        return 10;
    if (QCoreApplication::translate("subscriptions_page", "Apply updates when selected") ==
            "Apply updates when selected" ||
        QCoreApplication::translate("proxies_page", "%1 members") == "%1 members")
        return 6;
    if (QCoreApplication::translate("proxies_page", "Waiting for core confirmation") !=
            "等待内核确认" ||
        QCoreApplication::translate("proxies_page", "Group members may be out of date") !=
            "组成员可能已过期")
        return 9;
    if (!localization.load({"ja-JP"}) || localization.language() != "en" ||
        QCoreApplication::translate("main", "Quit") != "Quit")
        return 4;
    if (!localization.load({"zh-CN"}) ||
        QCoreApplication::translate("subscription_details", "Used %1 / %2") == "Used %1 / %2" ||
        QCoreApplication::translate("settings_page", "Save delay test target") ==
            "Save delay test target" ||
        QCoreApplication::translate("quickclash::ApplicationHost", "System proxy") ==
            "System proxy")
        return 7;
    if (QCoreApplication::translate("subscription_card", "Manage") == "Manage" ||
        QCoreApplication::translate(
            "settings_page",
            "Low memory: unload after 5 seconds hidden or minimized (experimental)") ==
            "Low memory: unload after 5 seconds hidden or minimized (experimental)")
        return 8;
    if (QCoreApplication::translate("home_proxy_controls", "Proxy and network") != "代理与网络" ||
        QCoreApplication::translate("network_tools", "IP comparison") != "IP 对照" ||
        QCoreApplication::translate("home_page", "Current core session totals") !=
            "本次内核运行累计")
        return 10;
    if (QCoreApplication::translate("runtime_overview", "Runtime overview") != "运行概况" ||
        QCoreApplication::translate("runtime_overview", "Client memory") != "客户端内存" ||
        QCoreApplication::translate("runtime_overview", "Copy address") != "复制地址")
        return 11;
    if (!localization.loadPreference("zh") ||
        QCoreApplication::translate("settings_page", "Language") != "语言" ||
        QCoreApplication::translate(
            "settings_page",
            "Choose the interface language. Follow system uses your system's preferred language; "
            "other languages use English. Changes apply immediately.") !=
            "选择界面语言。跟随系统使用系统首选语言，其他语言默认使用英文；更改会立即生效。")
        return 12;
    if (!localization.loadPreference("en") ||
        QCoreApplication::translate("settings_page", "Language") != "Language")
        return 13;
    if (localization.loadPreference("fr") || localization.language() != "en")
        return 14;
    if (!localization.loadPreference("system") ||
        localization.language() != Localization::languageFor(QLocale::system().uiLanguages()))
        return 15;
    std::cout << "Locale preference, Chinese catalog, English and unsupported fallback passed\n";
}
