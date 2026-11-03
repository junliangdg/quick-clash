#include "infra/config/custom_groups.h"
#include "infra/config/config_metadata.h"
#include "infra/config/config_overlay.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
namespace {
struct InvalidGroup {
    const char* code;
};
void require(bool condition, const char* code = "InvalidCustomGroup") {
    if (!condition)
        throw InvalidGroup{code};
}
domain::Error failure(const char* code) {
    domain::Error error;
    error.code = QString::fromLatin1(code);
    error.userMessageKey = error.code;
    error.stage = QStringLiteral("custom_groups");
    return error;
}
QString text(const YAML::Node& node) {
    return node && node.IsScalar() ? QString::fromStdString(node.Scalar()) : QString{};
}
QString pattern(const QString& input, const QString& mode) {
    require(input.size() <= 512, "InvalidCustomGroupFilter");
    if (input.trimmed().isEmpty())
        return {};
    QString value;
    if (mode == QStringLiteral("keywords")) {
        QStringList parts;
        const auto words =
            input.split(QRegularExpression(QStringLiteral("[,，\\n\\r]+")), Qt::SkipEmptyParts);
        require(words.size() <= 32, "InvalidCustomGroupFilter");
        const QString special = QStringLiteral("\\.^$|?*+(){}[]");
        for (auto word : words) {
            word = word.trimmed();
            if (word.isEmpty())
                continue;
            QString escaped;
            for (const auto c : word) {
                if (special.contains(c))
                    escaped += QLatin1Char('\\');
                escaped += c;
            }
            parts.append(escaped);
        }
        value = parts.join(QLatin1Char('|'));
    } else {
        require(mode == QStringLiteral("regex"), "InvalidCustomGroupFilter");
        value = input.trimmed();
        // One portable, bounded regex: no backreferences, lookarounds, inline engines or
        // stacked quantifiers. Core -t is still required before a preview can be applied.
        require(!value.contains(QStringLiteral("(?")) && !value.contains(QStringLiteral("(*")) &&
                    !value.contains(QLatin1Char('`')) &&
                    !QRegularExpression(QStringLiteral("\\\\[1-9]|[+*?}]\\s*[+*?{]"))
                         .match(value)
                         .hasMatch(),
                "InvalidCustomGroupFilter");
        // Quantified alternations, not only nested repetitions, can be pathological.
        require(!QRegularExpression(QStringLiteral("\\)\\s*[+*?{]")).match(value).hasMatch(),
                "InvalidCustomGroupFilter");
        // Keep the portable subset small. The core regex engine is not our worker's engine.
        int repeats = 0;
        bool escaped = false, inClass = false;
        for (const auto c : value) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == QLatin1Char('\\')) {
                escaped = true;
                continue;
            }
            if (c == QLatin1Char('['))
                inClass = true;
            if (c == QLatin1Char(']'))
                inClass = false;
            if (!inClass) {
                require(c != QLatin1Char('{') && c != QLatin1Char('}'), "InvalidCustomGroupFilter");
                if (c == QLatin1Char('*') || c == QLatin1Char('+') || c == QLatin1Char('?'))
                    require(++repeats <= 1, "InvalidCustomGroupFilter");
            }
        }
    }
    require(!value.contains(QLatin1Char('`')) && !value.contains(QChar::Null) &&
                !value.contains(QLatin1Char('\n')) && QRegularExpression(value).isValid(),
            "InvalidCustomGroupFilter");
    return value.isEmpty() ? QString{} : QStringLiteral("(?i)") + value;
}
int integer(const QJsonObject& object, const char* key, int min, int max) {
    const auto value = object.value(QLatin1String(key));
    require(value.isDouble() && value.toDouble() == value.toInt() && value.toInt() >= min &&
            value.toInt() <= max);
    return value.toInt();
}
QByteArray emitConfig(YAML::Node root, const std::vector<std::string>& rules) {
    root.remove("rules");
    root.SetStyle(YAML::EmitterStyle::Block);
    YAML::Emitter out;
    out << root;
    QByteArray bytes(out.c_str(), static_cast<qsizetype>(out.size()));
    bytes += "\nrules:\n";
    if (rules.empty())
        bytes += "  []\n";
    for (const auto& rule : rules) {
        YAML::Emitter scalar;
        scalar << YAML::DoubleQuoted << rule;
        bytes += "  - " + QByteArray(scalar.c_str(), static_cast<qsizetype>(scalar.size())) + '\n';
    }
    require(bytes.size() <= 16 * 1024 * 1024);
    return bytes;
}
} // namespace

domain::Result<CustomGroupConfig> composeCustomGroups(const QByteArray& source,
                                                      const QJsonArray& definitions,
                                                      const QJsonObject& observedProviders) {
    if (definitions.size() > 32 ||
        QJsonDocument(definitions).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return failure("CustomGroupLimitExceeded");
    const auto metadata = configMetadata(source, true);
    if (const auto* error = std::get_if<domain::Error>(&metadata))
        return *error;
    try {
        const auto& fields = std::get<ConfigMetadata>(metadata);
        auto root = YAML::Load(fields.fieldsYaml.toStdString());
        require(root.IsMap());
        QSet<QString> names{QStringLiteral("DIRECT"), QStringLiteral("REJECT"),
                            QStringLiteral("GLOBAL")};
        QJsonArray parents, providers, previews;
        QHash<QString, QStringList> sourceNodes;
        QStringList inlineNodes;
        for (const auto& node : root["proxies"]) {
            const auto name = text(node["name"]);
            require(!name.isEmpty() && !names.contains(name), "CustomGroupNameConflict");
            names.insert(name);
            inlineNodes.append(name);
        }
        for (const auto& provider : root["proxy-providers"]) {
            const auto name = text(provider.first);
            providers.append(name);
            for (const auto& node : provider.second["payload"]) {
                const auto nodeName = text(node["name"]);
                if (!nodeName.isEmpty()) {
                    names.insert(nodeName);
                    sourceNodes[name].append(nodeName);
                }
            }
        }
        for (auto it = observedProviders.begin(); it != observedProviders.end(); ++it) {
            if (!providers.contains(it.key()))
                continue;
            require(it.value().isArray() && it.value().toArray().size() <= 5000);
            sourceNodes[it.key()].clear();
            for (const auto& value : it.value().toArray()) {
                require(value.isString() && value.toString().size() <= 512);
                names.insert(value.toString());
                sourceNodes[it.key()].append(value.toString());
            }
        }
        require(inlineNodes.size() <= 5000 && providers.size() <= 32, "CustomGroupLimitExceeded");
        for (const auto& group : root["proxy-groups"]) {
            const auto name = text(group["name"]);
            names.insert(name);
            if (text(group["type"]) == QStringLiteral("select"))
                parents.append(name);
        }
        QSet<QString> ids;
        QHash<QString, QByteArray> healthChecks;
        for (const auto& entry : definitions) {
            require(entry.isObject());
            const auto spec = entry.toObject();
            const QSet<QString> allowed{QStringLiteral("id"),        QStringLiteral("name"),
                                        QStringLiteral("type"),      QStringLiteral("inline"),
                                        QStringLiteral("providers"), QStringLiteral("sourceNames"),
                                        QStringLiteral("mode"),      QStringLiteral("include"),
                                        QStringLiteral("exclude"),   QStringLiteral("url"),
                                        QStringLiteral("interval"),  QStringLiteral("timeout"),
                                        QStringLiteral("tolerance"), QStringLiteral("lazy"),
                                        QStringLiteral("strategy"),  QStringLiteral("parent")};
            for (auto it = spec.begin(); it != spec.end(); ++it)
                require(allowed.contains(it.key()));
            const auto id = spec.value("id").toString();
            const auto name = spec.value("name").toString();
            require(!QUuid(id).isNull() && !ids.contains(id));
            ids.insert(id);
            require(
                !name.trimmed().isEmpty() && name == name.trimmed() && name.size() <= 80 &&
                !QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")).match(name).hasMatch());
            require(!names.contains(name), "CustomGroupNameConflict");
            names.insert(name);
            const auto type = spec.value("type").toString();
            require(QStringList{QStringLiteral("url-test"), QStringLiteral("fallback"),
                                QStringLiteral("load-balance")}
                        .contains(type));
            require(spec.value("inline").isBool() && spec.value("providers").isArray() &&
                    spec.value("lazy").isBool());
            require(!spec.contains("parent") || spec.value("parent").isString());
            const auto parent = spec.value("parent").toString();
            require(parent.isEmpty() || parents.contains(parent), "CustomGroupParentMissing");
            const auto mode = spec.value("mode").toString();
            require(mode == QStringLiteral("keywords") || mode == QStringLiteral("regex"),
                    "InvalidCustomGroupFilter");
            require(spec.value("include").isString() && spec.value("exclude").isString());
            const auto include = pattern(spec.value("include").toString(), mode);
            const auto exclude = pattern(spec.value("exclude").toString(), mode);
            const auto bounded = [](const QString& expression) {
                return expression.isEmpty()
                           ? QRegularExpression{}
                           : QRegularExpression(
                                 QStringLiteral("(*LIMIT_MATCH=10000)(*LIMIT_DEPTH=128)") +
                                 expression);
            };
            const QRegularExpression included = bounded(include), excluded = bounded(exclude);
            const QUrl url(spec.value("url").toString());
            require(url.isValid() &&
                    (url.scheme() == QStringLiteral("https") ||
                     url.scheme() == QStringLiteral("http")) &&
                    !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment() &&
                    url.toString().size() <= 4096);
            const auto interval = integer(spec, "interval", 1, 86400);
            const auto timeout = integer(spec, "timeout", 1, 60000);
            const auto tolerance = integer(spec, "tolerance", 0, 60000);
            const auto strategy = spec.value("strategy").toString();
            require(QStringList{QStringLiteral("consistent-hashing"), QStringLiteral("round-robin"),
                                QStringLiteral("sticky-sessions")}
                        .contains(strategy));
            QSet<QString> seen;
            QStringList matched, scope;
            QSet<QString> frozen;
            if (spec.contains("sourceNames")) {
                require(spec.value("sourceNames").isArray() &&
                        spec.value("sourceNames").toArray().size() <= 5000);
                for (const auto& value : spec.value("sourceNames").toArray()) {
                    require(value.isString());
                    frozen.insert(value.toString());
                }
            }
            const auto accept = [&](const QString& nodeName) {
                const auto in = included.match(nodeName), out = excluded.match(nodeName);
                require((include.isEmpty() || in.isValid()) && (exclude.isEmpty() || out.isValid()),
                        "InvalidCustomGroupFilter");
                return (include.isEmpty() || in.hasMatch()) &&
                       (exclude.isEmpty() || !out.hasMatch());
            };
            YAML::Node group(YAML::NodeType::Map);
            group["name"] = name.toStdString();
            group["type"] = type.toStdString();
            group["proxies"] = YAML::Node(YAML::NodeType::Sequence);
            if (spec.value("inline").toBool()) {
                for (const auto& nodeName : inlineNodes) {
                    if (spec.contains("sourceNames") && !frozen.contains(nodeName))
                        continue;
                    scope.append(nodeName);
                    if (accept(nodeName)) {
                        group["proxies"].push_back(nodeName.toStdString());
                        matched.append(nodeName);
                        seen.insert(nodeName);
                    }
                }
            }
            YAML::Node health(YAML::NodeType::Map);
            health["enable"] = true;
            health["url"] = url.toString().toStdString();
            health["interval"] = interval;
            health["timeout"] = timeout;
            health["lazy"] = spec.value("lazy").toBool();
            YAML::Emitter healthEmitter;
            healthEmitter << health;
            const QByteArray healthBytes(healthEmitter.c_str(),
                                         static_cast<qsizetype>(healthEmitter.size()));
            const auto selectedProviders = spec.value("providers").toArray();
            require(selectedProviders.size() <= 32);
            QSet<QString> used;
            for (const auto& value : selectedProviders) {
                require(value.isString());
                const auto providerName = value.toString();
                require(providers.contains(providerName) && !used.contains(providerName),
                        "CustomGroupSourceMissing");
                used.insert(providerName);
                require(!healthChecks.contains(providerName) ||
                            healthChecks.value(providerName) == healthBytes,
                        "CustomGroupHealthConflict");
                healthChecks.insert(providerName, healthBytes);
                root["proxy-providers"][providerName.toStdString()]["health-check"] =
                    YAML::Clone(health);
                group["use"].push_back(providerName.toStdString());
                for (const auto& nodeName : sourceNodes.value(providerName)) {
                    scope.append(nodeName);
                    if (accept(nodeName) && !seen.contains(nodeName)) {
                        seen.insert(nodeName);
                        matched.append(nodeName);
                    }
                }
            }
            require(spec.value("inline").toBool() || !used.isEmpty(), "CustomGroupSourceMissing");
            // A provider may become empty after a live refresh. Never permit implicit DIRECT.
            group["proxies"].push_back("REJECT");
            group["empty-fallback"] = "REJECT";
            group["url"] = url.toString().toStdString();
            group["interval"] = interval;
            group["timeout"] = timeout;
            group["lazy"] = spec.value("lazy").toBool();
            if (type == QStringLiteral("url-test"))
                group["tolerance"] = tolerance;
            if (type == QStringLiteral("load-balance"))
                group["strategy"] = strategy.toStdString();
            if (!used.isEmpty()) {
                if (!include.isEmpty())
                    group["filter"] = include.toStdString();
                // Do not filter the explicit fail-closed sentinel out of the group.
                if (!exclude.isEmpty())
                    group["exclude-filter"] = exclude.toStdString();
            }
            root["proxy-groups"].push_back(group);
            if (!parent.isEmpty()) {
                for (auto original : root["proxy-groups"]) {
                    if (text(original["name"]) == parent) {
                        bool present = false;
                        for (const auto& member : original["proxies"])
                            present = present || text(member) == name;
                        if (!present)
                            original["proxies"].push_back(name.toStdString());
                    }
                }
            }
            QJsonArray sample;
            for (qsizetype index = 0; index < qMin(qsizetype{100}, matched.size()); ++index)
                sample.append(matched[index]);
            scope.removeDuplicates();
            previews.append(QJsonObject{{"id", id},
                                        {"name", name},
                                        {"count", matched.size()},
                                        {"members", sample},
                                        {"truncated", matched.size() > 100},
                                        {"sourceNames", QJsonArray::fromStringList(scope)},
                                        {"providers", selectedProviders}});
        }
        CustomGroupConfig result;
        result.yaml = definitions.isEmpty() ? source : emitConfig(root, fields.rules);
        result.view = {{"definitions", definitions},
                       {"groups", previews},
                       {"parents", parents},
                       {"providers", providers},
                       {"providerNamesLive", !observedProviders.isEmpty()},
                       {"inlineCount", inlineNodes.size()}};
        return result;
    } catch (const InvalidGroup& invalid) {
        return failure(invalid.code);
    } catch (const YAML::Exception&) {
        return failure("InvalidCustomGroup");
    }
}
domain::Result<QByteArray> composeProfileConfig(const QByteArray& source,
                                                const QJsonObject& profile) {
    if (profile.contains("customGroups") && !profile.value("customGroups").isArray())
        return failure("InvalidCustomGroup");
    const auto overlay = composeOverlay(source, profile.value("overlay").toObject());
    if (const auto* error = std::get_if<domain::Error>(&overlay))
        return *error;
    if (profile.value("customGroups").toArray().isEmpty())
        return std::get<QByteArray>(overlay);
    const auto groups =
        composeCustomGroups(std::get<QByteArray>(overlay), profile.value("customGroups").toArray());
    if (const auto* error = std::get_if<domain::Error>(&groups))
        return *error;
    return std::get<CustomGroupConfig>(groups).yaml;
}
QJsonArray archiveCustomGroups(const QJsonArray& definitions, const QJsonObject& view) {
    QJsonArray result;
    for (const auto& value : definitions) {
        auto spec = value.toObject();
        for (const auto& item : view.value("groups").toArray()) {
            const auto group = item.toObject();
            if (group.value("id") == spec.value("id"))
                spec.insert("sourceNames", group.value("sourceNames"));
        }
        spec.insert("inline", true);
        spec.insert("providers", QJsonArray{});
        result.append(spec);
    }
    return result;
}
} // namespace quickclash::infra
