#include "presentation/feature_registry.h"
#include <QCoreApplication>
#include <QMetaType>
#include <QSet>
#include <utility>

namespace quickclash::presentation {
namespace {
QUrl factory(const QString& path) {
    return QUrl(QStringLiteral("qrc:/qt/qml/QuickClash/Ui/") + path);
}
bool validContext(const QVariantMap& context, const QStringList& keys) {
    if (context.size() > 8) {
        return false;
    }
    for (auto it = context.cbegin(); it != context.cend(); ++it) {
        if (!keys.contains(it.key()) || it.value().metaType().id() != QMetaType::QString ||
            it.value().toString().size() > 128 || it.value().toString().contains(QChar::Null)) {
            return false;
        }
    }
    return true;
}
} // namespace
FeatureRegistry::FeatureRegistry() {
    const auto add = [this](const char* id, const char* title, const char* glyph, const char* path,
                            const char* availability, QStringList keys = {}) {
        features_.append(
            {QString::fromUtf8(id), QCoreApplication::translate("FeatureRegistry", title),
             QString::fromUtf8(glyph), factory(QString::fromUtf8(path)),
             QCoreApplication::translate("FeatureRegistry", availability), std::move(keys)});
    };
    add("home", QT_TRANSLATE_NOOP("FeatureRegistry", "Dashboard"), "⌂",
        "features/home/home_page.qml", QT_TRANSLATE_NOOP("FeatureRegistry", "Live"));
    add("proxies", QT_TRANSLATE_NOOP("FeatureRegistry", "Proxies"), "⇄",
        "features/proxies/proxies_page.qml", QT_TRANSLATE_NOOP("FeatureRegistry", "Live"),
        {"group"});
    add("subscriptions", QT_TRANSLATE_NOOP("FeatureRegistry", "Subscriptions"), "▤",
        "features/subscriptions/subscriptions_page.qml",
        QT_TRANSLATE_NOOP("FeatureRegistry", "Live"));
    add("connections", QT_TRANSLATE_NOOP("FeatureRegistry", "Connections"), "⇌",
        "features/connections/connections_page.qml", QT_TRANSLATE_NOOP("FeatureRegistry", "Live"),
        {"filter"});
    add("rules", QT_TRANSLATE_NOOP("FeatureRegistry", "Rules"), "≡",
        "features/rules/rules_page.qml", QT_TRANSLATE_NOOP("FeatureRegistry", "Live"));
    add("logs", QT_TRANSLATE_NOOP("FeatureRegistry", "Logs"), "☷", "features/logs/logs_page.qml",
        QT_TRANSLATE_NOOP("FeatureRegistry", "Live"));
    add("tests", QT_TRANSLATE_NOOP("FeatureRegistry", "Tests"), "⌁",
        "features/tests/tests_page.qml", QT_TRANSLATE_NOOP("FeatureRegistry", "Live"));
    add("settings", QT_TRANSLATE_NOOP("FeatureRegistry", "Settings"), "⚙",
        "features/settings/settings_page.qml", QT_TRANSLATE_NOOP("FeatureRegistry", "Preferences"));
    embedded_.append({QStringLiteral("proxy_summary"),
                      QStringLiteral("proxies"),
                      factory(QStringLiteral("features/proxies/proxy_summary.qml")),
                      {QStringLiteral("group")}});
}
const FeatureDescriptor* FeatureRegistry::resolve(const QString& id,
                                                  const QVariantMap& context) const {
    for (const auto& feature : features_) {
        if (feature.id == id && validContext(context, feature.contextKeys) &&
            (!context.contains(QStringLiteral("group")) ||
             context.value(QStringLiteral("group")).toString() == QStringLiteral("primary"))) {
            return &feature;
        }
    }
    return nullptr;
}
const EmbeddedDescriptor* FeatureRegistry::resolveEmbedded(const QString& id,
                                                           const QVariantMap& context) const {
    for (const auto& content : embedded_) {
        if (content.id == id && validContext(context, content.contextKeys) &&
            (!context.contains(QStringLiteral("group")) ||
             context.value(QStringLiteral("group")).toString() == QStringLiteral("primary"))) {
            return &content;
        }
    }
    return nullptr;
}
QList<FeatureDescriptor> FeatureRegistry::project(const QStringList& ids) const {
    QList<FeatureDescriptor> result;
    QSet<QString> seen;
    for (const auto& id : ids) {
        if (const auto* feature = resolve(id); feature && !seen.contains(id)) {
            result.append(*feature);
            seen.insert(id);
        }
    }
    return result;
}
} // namespace quickclash::presentation
