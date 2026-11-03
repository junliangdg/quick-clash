#include "infra/config/config_overlay.h"
#include "infra/config/config_metadata.h"
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
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

domain::Result<QJsonObject> userRuleTargets(const QByteArray& source) {
    const auto metadata = configMetadata(source, true);
    if (const auto* error = std::get_if<domain::Error>(&metadata))
        return *error;
    try {
        const auto root = YAML::Load(std::get<ConfigMetadata>(metadata).fieldsYaml.toStdString());
        QJsonArray groups, providers;
        if (root["proxy-groups"]) {
            for (const auto& group : root["proxy-groups"]) {
                if (group["name"] && group["name"].IsScalar())
                    groups.append(QString::fromStdString(group["name"].Scalar()));
            }
        }
        if (root["rule-providers"] && root["rule-providers"].IsMap()) {
            for (const auto& provider : root["rule-providers"]) {
                if (provider.first.IsScalar())
                    providers.append(QString::fromStdString(provider.first.Scalar()));
            }
        }
        return QJsonObject{{QStringLiteral("groups"), groups},
                           {QStringLiteral("providers"), providers}};
    } catch (const YAML::Exception&) {
        return invalid();
    }
}

domain::Result<QByteArray> composeUserRules(const QByteArray& source, const QJsonObject& rulebook,
                                            const QString& profileId) {
    if (rulebook.isEmpty())
        return source;
    auto failure = [](const QString& code) {
        auto result = invalid();
        result.code = code;
        result.userMessageKey = code;
        result.stage = QStringLiteral("user_rules");
        return result;
    };
    if (QJsonDocument(rulebook).toJson(QJsonDocument::Compact).size() > 256 * 1024 ||
        (rulebook.contains("global") && !rulebook.value("global").isArray()) ||
        (rulebook.contains("profiles") && !rulebook.value("profiles").isObject()))
        return failure(QStringLiteral("InvalidUserRules"));
    const auto targets = userRuleTargets(source);
    if (const auto* error = std::get_if<domain::Error>(&targets))
        return *error;
    const auto available = std::get<QJsonObject>(targets);
    const auto groups = available.value("groups").toArray();
    const auto providers = available.value("providers").toArray();
    const auto profiles = rulebook.value("profiles").toObject();
    if (profiles.contains(profileId) && !profiles.value(profileId).isObject())
        return failure(QStringLiteral("InvalidUserRules"));
    const auto profile = profiles.value(profileId).toObject();
    if ((profile.contains("rules") && !profile.value("rules").isArray()) ||
        (profile.contains("proxy") && !profile.value("proxy").isString()))
        return failure(QStringLiteral("InvalidUserRules"));
    const auto proxy = profile.value("proxy").toString();
    QJsonArray result;
    QSet<QString> ids;
    const auto append = [&](const QJsonArray& rules, bool global) -> QString {
        for (const auto& entry : rules) {
            if (!entry.isObject())
                return QStringLiteral("InvalidUserRules");
            const auto rule = entry.toObject();
            const auto id = rule.value("id").toString();
            const auto type = rule.value("type").toString();
            const auto value = rule.value("value").toString().trimmed();
            auto target = rule.value("target").toString();
            if (id.isEmpty() || id.size() > 128 || ids.contains(id) ||
                !rule.value("enabled").isBool() || value.isEmpty() || value.size() > 2048 ||
                value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('\n')) ||
                value.contains(QLatin1Char('\r')) || value.contains(QChar::Null))
                return QStringLiteral("InvalidUserRules");
            ids.insert(id);
            if (!QStringList{"DOMAIN", "DOMAIN-SUFFIX", "DOMAIN-KEYWORD", "IP-CIDR", "IP-CIDR6",
                             "GEOIP", "GEOSITE", "RULE-SET"}
                     .contains(type) ||
                (global && type == QStringLiteral("RULE-SET")))
                return QStringLiteral("InvalidUserRules");
            if (type != QStringLiteral("RULE-SET")) {
                for (const auto character : value)
                    if (character.isSpace())
                        return QStringLiteral("InvalidUserRules");
            }
            if (type == QStringLiteral("IP-CIDR") || type == QStringLiteral("IP-CIDR6")) {
                const auto network = QHostAddress::parseSubnet(value);
                if (network.second < 0 ||
                    (type == QStringLiteral("IP-CIDR") &&
                     network.first.protocol() != QAbstractSocket::IPv4Protocol) ||
                    (type == QStringLiteral("IP-CIDR6") &&
                     network.first.protocol() != QAbstractSocket::IPv6Protocol))
                    return QStringLiteral("InvalidUserRules");
            }
            if (!rule.value("enabled").toBool())
                continue;
            if (type == QStringLiteral("RULE-SET") && !providers.contains(value))
                return QStringLiteral("UserRuleProviderMissing");
            if (global && target == QStringLiteral("PROXY")) {
                if (proxy.isEmpty() || !groups.contains(proxy))
                    return QStringLiteral("UserRuleProxyMissing");
                target = proxy;
            } else if (target != QStringLiteral("DIRECT") && target != QStringLiteral("REJECT") &&
                       (global || !groups.contains(target))) {
                return QStringLiteral("UserRuleTargetMissing");
            }
            result.append(type + QLatin1Char(',') + value + QLatin1Char(',') + target);
        }
        return {};
    };
    auto issue = append(profile.value("rules").toArray(), false);
    if (issue.isEmpty())
        issue = append(rulebook.value("global").toArray(), true);
    if (!issue.isEmpty())
        return failure(issue);
    if (result.isEmpty())
        return source;
    return composeOverlay(source, {{"rules", QJsonObject{{"mode", "prepend"}, {"items", result}}}});
}
} // namespace quickclash::infra
