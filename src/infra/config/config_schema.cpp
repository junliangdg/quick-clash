#include "infra/config/config_schema.h"
#include "infra/config/config_preflight.h"
#include <QRegularExpression>
#include <QUrl>
#include <map>
#include <tuple>

namespace quickclash::infra {
namespace {
struct InvalidField {
    QString path;
};
void require(bool condition, const QString& path) {
    if (!condition)
        throw InvalidField{path};
}
std::string text(const YAML::Node& node, const QString& path, std::size_t limit = 8192) {
    require(node.IsScalar() && !node.Scalar().empty() && node.Scalar().size() <= limit, path);
    return node.Scalar();
}
class Schema final {
    std::map<std::tuple<QString, QString>, std::size_t> issueGroups_;

  public:
    std::vector<domain::ConfigIssue> issues;
    void issue(const char* code, QString path) {
        path.replace(QRegularExpression(QStringLiteral("\\[\\d+\\]")), QStringLiteral("[*]"));
        const auto key = std::make_tuple(QString::fromLatin1(code), path);
        if (const auto found = issueGroups_.find(key); found != issueGroups_.end()) {
            ++issues[found->second].occurrences;
        } else if (issues.size() < 32) {
            issueGroups_.emplace(key, issues.size());
            issues.push_back({QString::fromLatin1(code), path, QStringLiteral("field"),
                              QStringLiteral("preflight")});
        }
    }
    // Keep resource and projection contracts here. The selected official core validates
    // protocols, transports, groups, rules, DNS and their options; a duplicate schema drifts.
    void proxy(const YAML::Node& node, const QString& path) {
        require(node.IsMap(), path);
        text(node["name"], path + ".name", 512);
        text(node["type"], path + ".type", 64);
    }
    void validate(const YAML::Node& root, const std::vector<std::string>* separateRules) {
        require(root.IsMap(), "$");
        const auto proxies = root["proxies"];
        const auto groups = root["proxy-groups"];
        require(proxies.IsSequence() && proxies.size() <= 5000, "$.proxies");
        require(groups.IsSequence() && groups.size() <= 500, "$.proxy-groups");
        for (std::size_t index = 0; index < proxies.size(); ++index)
            proxy(proxies[index], QStringLiteral("$.proxies[%1]").arg(index));
        for (std::size_t index = 0; index < groups.size(); ++index) {
            const auto path = QStringLiteral("$.proxy-groups[%1]").arg(index);
            const auto group = groups[index];
            require(group.IsMap(), path);
            text(group["name"], path + ".name", 512);
            text(group["type"], path + ".type", 64);
            for (const auto* field : {"proxies", "use"}) {
                const auto members = group[field];
                require(!members || (members.IsSequence() && members.size() <= 5000),
                        path + '.' + field);
                if (members)
                    for (const auto& member : members)
                        text(member, path + '.' + field + "[]", 512);
            }
        }
        std::size_t providerCount = 0, dynamicCount = proxies.size(), providerRules = 0;
        qsizetype providerBytes = 0;
        for (const auto* kind : {"proxy-providers", "rule-providers"}) {
            const auto providers = root[kind];
            if (!providers)
                continue;
            require(!providers || (providers.IsMap() && providers.size() <= 32 - providerCount),
                    "$<providers>");
            providerCount += providers.size();
            for (const auto& entry : providers) {
                const auto path = QStringLiteral("$.") + kind + ".<provider>";
                const auto name = text(entry.first, path, 512);
                const auto value = entry.second;
                require(value.IsMap(), path);
                const bool inlineProvider =
                    value["type"] && value["type"].as<std::string>() == "inline";
                if (value["x-quickclash-body"]) {
                    const auto type = text(value["type"], path);
                    require(type == "http" || type == "file", path);
                    const auto body = text(value["x-quickclash-body"], path, 6 * 1024 * 1024);
                    const auto decoded = QByteArray::fromBase64Encoding(
                        QByteArray::fromStdString(body), QByteArray::AbortOnBase64DecodingErrors);
                    require(decoded && !decoded.decoded.isEmpty() &&
                                decoded.decoded.size() <= 4 * 1024 * 1024 &&
                                decoded.decoded.size() <= 16 * 1024 * 1024 - providerBytes,
                            path);
                    providerBytes += decoded.decoded.size();
                    const auto source = text(value["x-quickclash-source"], path);
                    if (type == "http") {
                        const QUrl address(QString::fromStdString(source));
                        require(text(value["url"], path) == source && address.isValid() &&
                                    (address.scheme() == "https" || address.scheme() == "http") &&
                                    !address.host().isEmpty() && address.userInfo().isEmpty() &&
                                    !address.hasFragment(),
                                path);
                    }
                    const auto format = value["format"] ? text(value["format"], path) : "yaml";
                    const auto behavior = value["behavior"] ? text(value["behavior"], path) : "";
                    require(text(value["path"], path) ==
                                runtimeProviderPath(
                                    QString::fromLatin1(kind), QString::fromStdString(name),
                                    QString::fromStdString(source), QString::fromStdString(format),
                                    QString::fromStdString(behavior))
                                    .toStdString(),
                            path);
                    require(value["size-limit"].as<int>() == 4 * 1024 * 1024, path);
                    continue;
                }
                if (!value["payload"] || (!inlineProvider && !value["size-limit"])) {
                    issue("UnsupportedResource", path);
                    continue; // Dependencies must pass the managed provider pipeline first.
                }
                if (!inlineProvider) {
                    const auto address = text(value["url"], path + ".url");
                    const QUrl url(QString::fromStdString(address));
                    require(url.isValid() && (url.scheme() == "https" || url.scheme() == "http") &&
                                !url.host().isEmpty() && url.userInfo().isEmpty() &&
                                !url.hasFragment(),
                            path + ".url");
                    const auto format =
                        value["format"] ? text(value["format"], path + ".format") : "yaml";
                    const auto behavior =
                        value["behavior"] ? text(value["behavior"], path + ".behavior") : "";
                    require(text(value["path"], path + ".path") ==
                                runtimeProviderPath(
                                    QString::fromLatin1(kind), QString::fromStdString(name),
                                    QString::fromStdString(address), QString::fromStdString(format),
                                    QString::fromStdString(behavior))
                                    .toStdString(),
                            path + ".path");
                    require(value["size-limit"].as<int>() == 4 * 1024 * 1024, path + ".size-limit");
                }
                require(value["payload"].IsSequence(), path + ".payload");
                if (std::string(kind) == "proxy-providers") {
                    require(value["payload"].size() <= 5000 - dynamicCount, path + ".payload");
                    dynamicCount += value["payload"].size();
                    for (const auto& node : value["payload"])
                        proxy(node, path + ".payload[]");
                } else {
                    require(value["payload"].size() <= 20000 - providerRules, path + ".payload");
                    providerRules += value["payload"].size();
                    for (const auto& rule : value["payload"])
                        text(rule, path + ".payload[]");
                }
            }
        }
        require(root["rules"].IsSequence(), "$.rules");
        const auto ruleCount = separateRules ? separateRules->size() : root["rules"].size();
        require(ruleCount <= 20000, "$.rules");
        for (std::size_t index = 0; index < ruleCount; ++index) {
            const auto path = QStringLiteral("$.rules[%1]").arg(index);
            const auto rule =
                separateRules ? (*separateRules)[index] : text(root["rules"][index], path);
            require(!rule.empty() && rule.size() <= 8192, path);
        }
    }
};
} // namespace
domain::CommandResult validateConfigSchema(const YAML::Node& root,
                                           const std::vector<std::string>* rules) {
    Schema schema;
    try {
        schema.validate(root, rules);
    } catch (const InvalidField& field) {
        schema.issue("InvalidConfigField", field.path);
    } catch (const YAML::Exception&) {
        schema.issue("InvalidConfigField", QStringLiteral("$"));
    }
    if (schema.issues.empty())
        return std::monostate{};
    domain::Error error;
    error.stage = QStringLiteral("preflight");
    error.code = schema.issues.front().code == QStringLiteral("InvalidConfigField")
                     ? QStringLiteral("InvalidConfigField")
                     : QStringLiteral("UnsupportedConfigFeature");
    error.userMessageKey = error.code;
    error.configIssues = std::move(schema.issues);
    return error;
}
} // namespace quickclash::infra
