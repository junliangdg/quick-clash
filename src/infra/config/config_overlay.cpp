#include "infra/config/config_overlay.h"
#include "infra/config/config_metadata.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <set>
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
namespace {
domain::Error invalid() {
    domain::Error error;
    error.code = QStringLiteral("InvalidOverlay");
    error.userMessageKey = error.code;
    error.stage = QStringLiteral("overlay");
    return error;
}
bool merge(YAML::Node target, const QJsonObject& values, int depth = 0) {
    if (depth > 24)
        return false;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.value().isNull())
            return false;
        const auto key = it.key().toStdString();
        if (it.value().isObject()) {
            if (!target[key] || !target[key].IsMap())
                target[key] = YAML::Node(YAML::NodeType::Map);
            if (!merge(target[key], it.value().toObject(), depth + 1))
                return false;
        } else {
            const auto wrapped = QJsonDocument(QJsonObject{{QStringLiteral("value"), it.value()}})
                                     .toJson(QJsonDocument::Compact);
            target[key] = YAML::Load(wrapped.toStdString())["value"];
        }
    }
    return true;
}
} // namespace
domain::Result<QByteArray> composeOverlay(const QByteArray& source, const QJsonObject& overlay) {
    if (overlay.isEmpty())
        return source;
    if (QJsonDocument(overlay).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return invalid();
    const std::set<QString> operations{QStringLiteral("set"), QStringLiteral("remove"),
                                       QStringLiteral("rules")};
    for (auto it = overlay.begin(); it != overlay.end(); ++it)
        if (!operations.contains(it.key()))
            return invalid();
    if ((overlay.contains(QStringLiteral("set")) && !overlay.value("set").isObject()) ||
        (overlay.contains(QStringLiteral("remove")) && !overlay.value("remove").isArray()) ||
        (overlay.contains(QStringLiteral("rules")) && !overlay.value("rules").isObject()))
        return invalid();
    const std::set<QString> reserved{QStringLiteral("secret"),
                                     QStringLiteral("external-controller"),
                                     QStringLiteral("external-controller-pipe"),
                                     QStringLiteral("tun"),
                                     QStringLiteral("mixed-port"),
                                     QStringLiteral("port"),
                                     QStringLiteral("socks-port"),
                                     QStringLiteral("allow-lan"),
                                     QStringLiteral("bind-address"),
                                     QStringLiteral("listeners"),
                                     QStringLiteral("proxy-providers"),
                                     QStringLiteral("rule-providers"),
                                     QStringLiteral("rules")};
    auto metadata = configMetadata(source, true);
    if (const auto* failure = std::get_if<domain::Error>(&metadata))
        return *failure;
    try {
        auto& parsed = std::get<ConfigMetadata>(metadata);
        auto root = YAML::Load(parsed.fieldsYaml.toStdString());
        if (!root.IsMap())
            return invalid();
        const auto values = overlay.value("set").toObject();
        for (auto it = values.begin(); it != values.end(); ++it)
            if (reserved.contains(it.key()))
                return invalid();
        if (!merge(root, values))
            return invalid();
        for (const auto& value : overlay.value("remove").toArray()) {
            if (!value.isString() || value.toString().isEmpty() ||
                reserved.contains(value.toString()))
                return invalid();
            root.remove(value.toString().toStdString());
        }
        const auto operation = overlay.value("rules").toObject();
        QStringList rules;
        for (const auto& rule : parsed.rules)
            rules.append(QString::fromStdString(rule));
        if (!operation.isEmpty()) {
            if (operation.size() != 2 || !operation.value("items").isArray())
                return invalid();
            const auto mode = operation.value("mode").toString();
            QStringList items;
            for (const auto& value : operation.value("items").toArray()) {
                if (!value.isString() || value.toString().size() > 8192)
                    return invalid();
                items.append(value.toString());
            }
            if (mode == QStringLiteral("replace"))
                rules = items;
            else if (mode == QStringLiteral("prepend"))
                rules = items + rules;
            else if (mode == QStringLiteral("append"))
                rules += items;
            else
                return invalid();
        }
        if (rules.size() > 20000)
            return invalid();
        root.remove("rules");
        YAML::Emitter output;
        output << root;
        QByteArray bytes(output.c_str(), static_cast<qsizetype>(output.size()));
        if (bytes.size() > 16 * 1024 * 1024)
            return invalid();
        bytes += "\nrules:\n";
        if (rules.isEmpty())
            bytes += "  []\n";
        for (const auto& rule : rules) {
            YAML::Emitter scalar;
            scalar << YAML::DoubleQuoted << rule.toStdString();
            bytes +=
                "  - " + QByteArray(scalar.c_str(), static_cast<qsizetype>(scalar.size())) + '\n';
            if (bytes.size() > 16 * 1024 * 1024)
                return invalid();
        }
        return output.good() ? domain::Result<QByteArray>(bytes)
                             : domain::Result<QByteArray>(invalid());
    } catch (const YAML::Exception&) {
        return invalid();
    }
}
} // namespace quickclash::infra
