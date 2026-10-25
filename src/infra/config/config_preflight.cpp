#include "infra/config/config_preflight.h"
#include "infra/config/config_schema.h"
#include <QJsonArray>
#include <QMessageAuthenticationCode>
#include <QStringList>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
namespace {
struct InvalidConfig {
    const char* code;
};
domain::Error inputError(const char* code) {
    domain::Error error;
    error.code = QString::fromLatin1(code);
    error.userMessageKey = error.code;
    error.stage = QStringLiteral("preflight");
    return error;
}
void require(bool condition, const char* code = "InvalidConfig") {
    if (!condition) {
        throw InvalidConfig{code};
    }
}
// Bound parser recursion and events before allocating the full Node graph. P2 rejects aliases.
class ParseBudget final : public YAML::EventHandler {
  public:
    void OnDocumentStart(const YAML::Mark&) override {
        require(++documents_ == 1);
    }
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark&, YAML::anchor_t) override {
        count();
    }
    void OnAlias(const YAML::Mark&, YAML::anchor_t) override {
        throw InvalidConfig{"UnsupportedConfigFeature"};
    }
    void OnScalar(const YAML::Mark&, const std::string&, YAML::anchor_t,
                  const std::string&) override {
        count();
    }
    void OnSequenceStart(const YAML::Mark&, const std::string&, YAML::anchor_t,
                         YAML::EmitterStyle::value) override {
        enter();
    }
    void OnSequenceEnd() override {
        --depth_;
    }
    void OnMapStart(const YAML::Mark&, const std::string&, YAML::anchor_t,
                    YAML::EmitterStyle::value) override {
        enter();
    }
    void OnMapEnd() override {
        --depth_;
    }

  private:
    void count() {
        require(++events_ <= 100000, "ConfigLimitExceeded");
    }
    void enter() {
        count();
        require(++depth_ <= 32, "ConfigLimitExceeded");
    }
    std::size_t events_ = 0;
    int depth_ = 0;
    int documents_ = 0;
};
void inspectTree(const YAML::Node& node, int depth, std::size_t& budget) {
    require(depth <= 32 && budget > 0, "ConfigLimitExceeded");
    --budget;
    if (node.IsMap()) {
        std::set<std::string> keys;
        for (const auto& entry : node) {
            require(entry.first.IsScalar());
            require(keys.insert(entry.first.Scalar()).second, "DuplicateConfigKey");
            inspectTree(entry.second, depth + 1, budget);
        }
    } else if (node.IsSequence()) {
        for (const auto& child : node) {
            inspectTree(child, depth + 1, budget);
        }
    }
}
QByteArray serializeYaml(const YAML::Node& node) {
    YAML::Emitter emitter;
    emitter << node;
    require(emitter.good());
    return QByteArray(emitter.c_str(), static_cast<qsizetype>(emitter.size()));
}
} // namespace

QString subscriptionPayloadError(const QByteArray& input) {
    if (input.size() > 16 * 1024 * 1024)
        return QStringLiteral("SubscriptionTooLarge");
    auto content = input;
    if (content.startsWith(QByteArray::fromHex("efbbbf")))
        content.remove(0, 3);
    if (content.trimmed().isEmpty())
        return QStringLiteral("EmptySubscription");
    try {
        std::istringstream stream(content.toStdString());
        YAML::Parser parser(stream);
        ParseBudget parseBudget;
        while (parser.HandleNextDocument(parseBudget)) {
        }
        const auto documents = YAML::LoadAll(content.toStdString());
        require(documents.size() == 1, "UnsupportedSubscriptionFormat");
        const auto root = documents.front();
        require(root.IsMap(), "UnsupportedSubscriptionFormat");
        std::size_t budget = 100000;
        inspectTree(root, 0, budget);
        require(root["proxies"] || root["proxy-providers"], "UnsupportedSubscriptionFormat");
        return {};
    } catch (const InvalidConfig& error) {
        return QString::fromLatin1(error.code);
    } catch (const YAML::Exception&) {
        return QStringLiteral("UnsupportedSubscriptionFormat");
    }
}

domain::Result<PreflightConfig> preflightConfig(const QByteArray& input,
                                                const domain::RuntimePreferences& preferences) {
    if (input.size() > 16 * 1024 * 1024) {
        return inputError("ConfigLimitExceeded");
    }
    if (preferences.mixedPort < 1024 || preferences.controllerPort < 1024 ||
        preferences.mixedPort == preferences.controllerPort) {
        return inputError("InvalidListenerPorts");
    }
    try {
        std::istringstream stream(input.toStdString());
        YAML::Parser parser(stream);
        ParseBudget parseBudget;
        while (parser.HandleNextDocument(parseBudget)) {
        }
        const auto documents = YAML::LoadAll(input.toStdString());
        require(documents.size() == 1);
        auto root = documents.front();
        std::size_t budget = 100000;
        inspectTree(root, 0, budget);
        const auto validated = validateConfigSchema(root);
        if (const auto* failure = std::get_if<domain::Error>(&validated))
            return *failure;
        PreflightConfig output;
        const std::vector<std::string> managed{
            "mixed-port",  "port",        "socks-port",      "redir-port",
            "tproxy-port", "allow-lan",   "bind-address",    "external-controller",
            "secret",      "external-ui", "external-ui-url", "tun",
            "profile",     "mode",        "geodata-mode",    "geo-auto-update"};
        for (const auto& key : managed) {
            if (root[key]) {
                output.overriddenFields.push_back(QString::fromStdString(key));
            }
            root.remove(key);
        }
        root["mixed-port"] = preferences.mixedPort;
        root["bind-address"] = "127.0.0.1";
        root["allow-lan"] = false;
        root["external-controller"] = "127.0.0.1:" + std::to_string(preferences.controllerPort);
        root["mode"] = preferences.mode == domain::ProxyMode::Global   ? "global"
                       : preferences.mode == domain::ProxyMode::Direct ? "direct"
                                                                       : "rule";
        // DNS remains internal to Mihomo; this slice does not expose an OS DNS listener.
        if (root["dns"]) {
            if (root["dns"]["listen"])
                output.overriddenFields.push_back(QStringLiteral("dns.listen"));
            root["dns"]["listen"] = "";
        } else {
            root["dns"]["enable"] = false;
        }
        require(preferences.dnsMode == QStringLiteral("profile") ||
                    preferences.dnsMode == QStringLiteral("fake-ip") ||
                    preferences.dnsMode == QStringLiteral("redir-host"),
                "InvalidDnsMode");
        if (preferences.dnsMode != QStringLiteral("profile")) {
            root["dns"]["enable"] = true;
            root["dns"]["enhanced-mode"] = preferences.dnsMode.toStdString();
            if (!root["dns"]["nameserver"]) {
                root["dns"]["nameserver"].push_back("https://1.1.1.1/dns-query");
                root["dns"]["nameserver"].push_back("https://8.8.8.8/dns-query");
            }
            if (!root["dns"]["default-nameserver"])
                root["dns"]["default-nameserver"].push_back("1.1.1.1");
        }
        // Some subscription generators emit the legacy mport alias. Normalize it to
        // Mihomo's actual ports option, and reject conflicting values in the schema.
        for (auto proxy : root["proxies"]) {
            if (proxy["mport"]) {
                if (!proxy["ports"] || proxy["ports"].Scalar().empty())
                    proxy["ports"] = proxy["mport"].Scalar();
                proxy.remove("mport");
            }
        }
        root["geodata-mode"] = true;
        root["tun"]["enable"] = false;
        root["geo-auto-update"] = false;
        root["profile"]["store-selected"] = false;
        root["profile"]["store-fake-ip"] = false;
        output.semanticYaml = serializeYaml(root);
        return output;
    } catch (const InvalidConfig& error) {
        return inputError(error.code);
    } catch (const YAML::Exception&) {
        return inputError("InvalidYaml");
    }
}

QJsonObject nodeIdentities(const QByteArray& semantic, const QByteArray& key) {
    QJsonObject identities;
    if (key.size() < 32)
        return identities;
    try {
        const auto root = YAML::Load(semantic.toStdString());
        for (const auto& node : root["proxies"]) {
            const auto name = QString::fromStdString(node["name"].as<std::string>());
            auto descriptor = YAML::Clone(node);
            descriptor.remove("name");
            identities.insert(name, QString::fromLatin1(QMessageAuthenticationCode::hash(
                                                            serializeYaml(descriptor), key,
                                                            QCryptographicHash::Sha256)
                                                            .toHex()));
        }
        for (const auto& group : root["proxy-groups"]) {
            const auto name = QString::fromStdString(group["name"].as<std::string>());
            auto descriptor = YAML::Clone(group);
            descriptor.remove("proxies");
            identities.insert(name, QStringLiteral("group:") +
                                        QString::fromLatin1(QMessageAuthenticationCode::hash(
                                                                serializeYaml(descriptor), key,
                                                                QCryptographicHash::Sha256)
                                                                .toHex()));
        }
        identities.insert(QStringLiteral("DIRECT"), QStringLiteral("builtin:direct"));
        identities.insert(QStringLiteral("REJECT"), QStringLiteral("builtin:reject"));
        identities.insert(QStringLiteral("GLOBAL"),
                          QStringLiteral("global:") +
                              QString::fromLatin1(
                                  QMessageAuthenticationCode::hash(QByteArrayLiteral("GLOBAL"), key,
                                                                   QCryptographicHash::Sha256)
                                      .toHex()));
    } catch (const YAML::Exception&) {
        return {};
    } catch (const InvalidConfig&) {
        return {};
    }
    return identities;
}
QJsonObject configurationSummary(const QByteArray& semantic) {
    try {
        const auto root = YAML::Load(semantic.toStdString());
        QJsonArray groups, nodes, all;
        all.append(QStringLiteral("DIRECT"));
        all.append(QStringLiteral("REJECT"));
        for (const auto& node : root["proxies"]) {
            const auto name = QString::fromStdString(node["name"].as<std::string>());
            all.append(name);
            nodes.append(QJsonObject{
                {QStringLiteral("name"), name},
                {QStringLiteral("type"), QString::fromStdString(node["type"].as<std::string>())}});
        }
        for (const auto& group : root["proxy-groups"]) {
            QJsonArray members;
            for (const auto& member : group["proxies"])
                members.append(QString::fromStdString(member.as<std::string>()));
            const auto name = QString::fromStdString(group["name"].as<std::string>());
            const auto type = QString::fromStdString(group["type"].as<std::string>());
            all.append(name);
            groups.append(QJsonObject{
                {QStringLiteral("name"), name},
                {QStringLiteral("type"), type},
                {QStringLiteral("canSelect"), type == QStringLiteral("select")},
                {QStringLiteral("current"), members.isEmpty() ? QJsonValue{} : members.first()},
                {QStringLiteral("members"), members}});
        }
        groups.prepend(QJsonObject{{QStringLiteral("name"), QStringLiteral("GLOBAL")},
                                   {QStringLiteral("type"), QStringLiteral("select")},
                                   {QStringLiteral("canSelect"), true},
                                   {QStringLiteral("current"), all.first()},
                                   {QStringLiteral("members"), all}});
        return {{QStringLiteral("groups"), groups}, {QStringLiteral("nodes"), nodes}};
    } catch (const YAML::Exception&) {
        return {};
    }
}
QString redactedConfig(const QByteArray& semantic) {
    try {
        auto root = YAML::Load(semantic.toStdString());
        root.remove("secret");
        for (auto proxy : root["proxies"]) {
            for (auto entry : proxy) {
                const auto key = entry.first.as<std::string>();
                if (key != "name" && key != "type" && key != "port" && key != "cipher")
                    proxy[key] = "[redacted]";
            }
        }
        for (auto group : root["proxy-groups"])
            if (group["url"])
                group["url"] = "[redacted]";
        if (root["dns"])
            root["dns"] = "[redacted]";
        const auto bytes = serializeYaml(root);
        if (bytes.size() > 256 * 1024)
            return QStringLiteral("PreviewLimitExceeded");
        return QString::fromUtf8(bytes);
    } catch (const YAML::Exception&) {
        return QStringLiteral("InvalidYaml");
    } catch (const InvalidConfig&) {
        return QStringLiteral("InvalidYaml");
    }
}

namespace {
YAML::Node boundedYaml(const QByteArray& input) {
    require(input.size() <= 16 * 1024 * 1024, "ConfigLimitExceeded");
    std::istringstream stream(input.toStdString());
    YAML::Parser parser(stream);
    ParseBudget budget;
    while (parser.HandleNextDocument(budget)) {
    }
    const auto documents = YAML::LoadAll(input.toStdString());
    require(documents.size() == 1, "InvalidYaml");
    std::size_t count = 100000;
    inspectTree(documents.front(), 0, count);
    return documents.front();
}
} // namespace
domain::Result<QJsonArray> providerRequests(const QByteArray& input) {
    try {
        const auto root = boundedYaml(input);
        require(root.IsMap());
        QJsonArray requests;
        for (const auto* kind : {"proxy-providers", "rule-providers"}) {
            const auto providers = root[kind];
            if (!providers)
                continue;
            require(providers.IsMap() && providers.size() <= 32, "ProviderLimitExceeded");
            for (const auto& entry : providers) {
                require(entry.first.IsScalar() && entry.first.Scalar().size() <= 512);
                const auto value = entry.second;
                require(value.IsMap() && value["type"] && value["type"].as<std::string>() == "http",
                        "ProviderHttpsRequired");
                const std::set<std::string> allowed{"type",     "url",          "path",
                                                    "interval", "health-check", "behavior",
                                                    "format",   "size-limit"};
                for (const auto& field : value)
                    require(allowed.contains(field.first.as<std::string>()),
                            "UnsupportedProviderField");
                const auto urlText = QString::fromStdString(value["url"].as<std::string>());
                const QUrl url(urlText);
                require(urlText.size() <= 8192 && url.isValid() && url.scheme() == "https" &&
                            !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment(),
                        "ProviderHttpsRequired");
                const auto format = value["format"] ? value["format"].as<std::string>() : "yaml";
                require(format == "yaml" ||
                            (std::string(kind) == "rule-providers" && format == "text"),
                        "UnsupportedProviderFormat");
                const auto behavior =
                    value["behavior"] ? value["behavior"].as<std::string>() : "classical";
                require(behavior == "classical" || behavior == "domain" || behavior == "ipcidr",
                        "UnsupportedProviderFormat");
                requests.append(QJsonObject{
                    {QStringLiteral("key"), QString::fromLatin1(kind) + ':' +
                                                QString::fromStdString(entry.first.Scalar())},
                    {QStringLiteral("name"), QString::fromStdString(entry.first.Scalar())},
                    {QStringLiteral("kind"), QString::fromLatin1(kind)},
                    {QStringLiteral("url"), urlText}});
            }
        }
        require(requests.size() <= 32, "ProviderLimitExceeded");
        return requests;
    } catch (const InvalidConfig& failure) {
        return inputError(failure.code);
    } catch (const YAML::Exception&) {
        return inputError("InvalidProviderConfig");
    }
}
domain::Result<QByteArray> expandProviders(const QByteArray& input,
                                           const QHash<QString, QByteArray>& payloads) {
    try {
        auto root = boundedYaml(input);
        std::map<std::string, std::vector<std::string>> proxyNames, ruleValues;
        if (!root["proxies"])
            root["proxies"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto* kind : {"proxy-providers", "rule-providers"}) {
            for (const auto& entry : root[kind]) {
                const auto name = entry.first.as<std::string>();
                const auto key = QString::fromLatin1(kind) + ':' + QString::fromStdString(name);
                require(payloads.contains(key), "ProviderSnapshotMissing");
                const auto bytes = payloads.value(key);
                require(bytes.size() <= 4 * 1024 * 1024, "ProviderLimitExceeded");
                YAML::Node values;
                const auto format =
                    entry.second["format"] ? entry.second["format"].as<std::string>() : "yaml";
                if (format == "text") {
                    values = YAML::Node(YAML::NodeType::Sequence);
                    for (const auto& line : QString::fromUtf8(bytes).split('\n')) {
                        const auto value = line.trimmed();
                        if (!value.isEmpty() && !value.startsWith('#'))
                            values.push_back(value.toStdString());
                    }
                } else {
                    const auto document = boundedYaml(bytes);
                    values =
                        document[std::string(kind) == "proxy-providers" ? "proxies" : "payload"];
                }
                require(values.IsSequence() && values.size() <= 20000, "InvalidProviderPayload");
                if (std::string(kind) == "proxy-providers") {
                    proxyNames[name] = {};
                    for (const auto& proxy : values) {
                        proxyNames[name].push_back(proxy["name"].as<std::string>());
                        root["proxies"].push_back(YAML::Clone(proxy));
                    }
                } else {
                    ruleValues[name] = {};
                    const auto behavior = entry.second["behavior"]
                                              ? entry.second["behavior"].as<std::string>()
                                              : "classical";
                    for (const auto& item : values) {
                        auto value = item.as<std::string>();
                        require(value.size() <= 8192, "ProviderLimitExceeded");
                        if (behavior == "domain") {
                            if (value.starts_with("+."))
                                value = "DOMAIN-SUFFIX," + value.substr(2);
                            else if (value.starts_with("*."))
                                value = "DOMAIN-SUFFIX," + value.substr(2);
                            else
                                value = "DOMAIN," + value;
                        } else if (behavior == "ipcidr")
                            value =
                                (value.find(':') == std::string::npos ? "IP-CIDR," : "IP-CIDR6,") +
                                value;
                        ruleValues[name].push_back(value);
                    }
                }
            }
            root.remove(kind);
        }
        for (auto group : root["proxy-groups"]) {
            if (!group["use"])
                continue;
            require(group["use"].IsSequence() && group["use"].size() <= 32,
                    "InvalidProviderConfig");
            if (!group["proxies"])
                group["proxies"] = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& reference : group["use"]) {
                const auto name = reference.as<std::string>();
                require(proxyNames.contains(name), "ProviderSnapshotMissing");
                for (const auto& member : proxyNames[name])
                    group["proxies"].push_back(member);
            }
            group.remove("use");
        }
        YAML::Node rules(YAML::NodeType::Sequence);
        for (const auto& rule : root["rules"]) {
            const auto parts = QString::fromStdString(rule.as<std::string>()).split(',');
            if (parts.first() != "RULE-SET") {
                rules.push_back(YAML::Clone(rule));
                continue;
            }
            require(parts.size() == 3 || (parts.size() == 4 && parts[3] == "no-resolve"),
                    "InvalidProviderConfig");
            const auto name = parts[1].toStdString();
            require(ruleValues.contains(name), "ProviderSnapshotMissing");
            for (auto value : ruleValues[name]) {
                const bool noResolve = value.ends_with(",no-resolve") || parts.size() == 4;
                if (value.ends_with(",no-resolve"))
                    value.resize(value.size() - 11);
                value += ',' + parts[2].toStdString();
                if (noResolve)
                    value += ",no-resolve";
                rules.push_back(value);
                require(rules.size() <= 20000, "ProviderLimitExceeded");
            }
        }
        root["rules"] = rules;
        return serializeYaml(root);
    } catch (const InvalidConfig& failure) {
        return inputError(failure.code);
    } catch (const YAML::Exception&) {
        return inputError("InvalidProviderPayload");
    }
}

domain::Result<QByteArray> materializeSession(const PreflightConfig& config,
                                              const QByteArray& secret) {
    if (secret.size() < 32 || secret.size() > 256) {
        return inputError("InvalidControllerSecret");
    }
    try {
        auto root = YAML::Load(config.semanticYaml.toStdString());
        root["secret"] = secret.toStdString();
        return serializeYaml(root);
    } catch (const YAML::Exception&) {
        return inputError("InvalidYaml");
    } catch (const InvalidConfig& error) {
        return inputError(error.code);
    }
}
} // namespace quickclash::infra
