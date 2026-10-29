#include "infra/config/config_preflight.h"
#include "infra/config/config_metadata.h"
#include "infra/config/config_schema.h"
#include <QJsonArray>
#include <QMessageAuthenticationCode>
#include <QSet>
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
void checkParseBudget(const QByteArray& input) {
    // Release the event scanner, its token buffers and the stream copy before Node allocation.
    std::istringstream stream(input.toStdString());
    YAML::Parser parser(stream);
    ParseBudget budget;
    while (parser.HandleNextDocument(budget)) {
    }
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
        const auto metadata = configMetadata(content);
        if (const auto* failure = std::get_if<domain::Error>(&metadata))
            return failure->code == QStringLiteral("InvalidYaml")
                       ? QStringLiteral("UnsupportedSubscriptionFormat")
                       : failure->code;
        const auto& keys = std::get<ConfigMetadata>(metadata).keys;
        require(keys.contains(QStringLiteral("proxies")) ||
                    keys.contains(QStringLiteral("proxy-providers")),
                "UnsupportedSubscriptionFormat");
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
        checkParseBudget(input); // Preserve budget errors before structural/schema diagnostics.
        const auto metadata = configMetadata(input, true);
        if (const auto* failure = std::get_if<domain::Error>(&metadata))
            return *failure;
        const auto& fields = std::get<ConfigMetadata>(metadata);
        auto root = YAML::Load(fields.fieldsYaml.toStdString());
        std::size_t budget = 100000;
        inspectTree(root, 0, budget);
        const auto validated = validateConfigSchema(root, &fields.rules);
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
        root.SetStyle(YAML::EmitterStyle::Block);
        root.remove("rules");
        output.semanticYaml = serializeYaml(root);
        YAML::Emitter rules;
        rules << YAML::BeginSeq;
        for (const auto& rule : fields.rules)
            rules << rule;
        rules << YAML::EndSeq;
        require(rules.good());
        output.semanticYaml += "\nrules:\n";
        output.semanticYaml.append(rules.c_str(), static_cast<qsizetype>(rules.size()));
        output.semanticYaml += '\n';
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
        const auto metadata = configMetadata(semantic);
        if (!std::holds_alternative<ConfigMetadata>(metadata))
            return {};
        const auto root = YAML::Load(std::get<ConfigMetadata>(metadata).fieldsYaml.toStdString());
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
        const auto metadata = configMetadata(semantic);
        if (!std::holds_alternative<ConfigMetadata>(metadata))
            return {};
        const auto& fields = std::get<ConfigMetadata>(metadata);
        const auto root = YAML::Load(fields.fieldsYaml.toStdString());
        QJsonArray groups, nodes, all;
        all.append(QStringLiteral("DIRECT"));
        all.append(QStringLiteral("REJECT"));
        for (const auto& node : root["proxies"]) {
            const auto name = QString::fromStdString(node["name"].as<std::string>());
            all.append(name);
            nodes.append(QJsonObject{
                {QStringLiteral("name"), name},
                {QStringLiteral("type"), QString::fromStdString(node["type"].as<std::string>())},
                {QStringLiteral("udp"), node["type"].as<std::string>() == "tuic" ||
                                            (node["udp"] && node["udp"].as<bool>())}});
        }
        for (const auto& group : root["proxy-groups"]) {
            QJsonArray members;
            for (const auto& member : group["proxies"])
                members.append(QString::fromStdString(member.as<std::string>()));
            const auto name = QString::fromStdString(group["name"].as<std::string>());
            const auto type = QString::fromStdString(group["type"].as<std::string>());
            all.append(name);
            groups.append(
                QJsonObject{{QStringLiteral("name"), name},
                            {QStringLiteral("type"), type},
                            {QStringLiteral("canSelect"), type == QStringLiteral("select")},
                            {QStringLiteral("current"), type == QStringLiteral("load-balance") ||
                                                                members.isEmpty() || group["use"]
                                                            ? QJsonValue{}
                                                            : members.first()},
                            {QStringLiteral("members"), members}});
        }
        groups.prepend(QJsonObject{{QStringLiteral("name"), QStringLiteral("GLOBAL")},
                                   {QStringLiteral("type"), QStringLiteral("select")},
                                   {QStringLiteral("canSelect"), true},
                                   {QStringLiteral("current"), all.first()},
                                   {QStringLiteral("members"), all}});
        return {{QStringLiteral("groups"), groups},
                {QStringLiteral("nodes"), nodes},
                {QStringLiteral("defaultPolicy"), fields.defaultPolicy}};
    } catch (const YAML::Exception&) {
        return {};
    }
}
QJsonArray orderedProxyGroups(const QJsonArray& configured, const QJsonObject& live) {
    QJsonArray result;
    QSet<QString> added;
    const auto append = [&](const QString& name) {
        const auto value = live.value(name).toObject();
        const auto type = value.value(QStringLiteral("type")).toString();
        if (added.contains(name) ||
            (type != QStringLiteral("Selector") && type != QStringLiteral("URLTest") &&
             type != QStringLiteral("Fallback") && type != QStringLiteral("LoadBalance")))
            return;
        added.insert(name);
        result.append(
            QJsonObject{{QStringLiteral("name"), name},
                        {QStringLiteral("type"), type},
                        {QStringLiteral("canSelect"), type == QStringLiteral("Selector")},
                        {QStringLiteral("current"), type == QStringLiteral("LoadBalance")
                                                        ? QJsonValue{}
                                                        : value.value(QStringLiteral("now"))},
                        {QStringLiteral("members"), value.value(QStringLiteral("all"))}});
    };
    for (const auto& group : configured)
        append(group.toObject().value(QStringLiteral("name")).toString());
    // Include core-generated groups without changing the subscription sequence.
    for (auto it = live.begin(); it != live.end(); ++it)
        append(it.key());
    return result;
}
QJsonObject outboundSummary(const QJsonArray& groups, const QString& defaultPolicy,
                            const QString& mode, bool ready) {
    const auto policy = mode == QStringLiteral("direct")   ? QStringLiteral("DIRECT")
                        : mode == QStringLiteral("global") ? QStringLiteral("GLOBAL")
                                                           : defaultPolicy;
    QJsonObject byName;
    for (const auto& value : groups) {
        const auto group = value.toObject();
        byName.insert(group.value(QStringLiteral("name")).toString(), group);
    }
    QJsonArray chain;
    QSet<QString> visited;
    auto current = policy;
    QString node;
    bool balanced = false;
    while (!current.isEmpty() && !visited.contains(current)) {
        visited.insert(current);
        chain.append(current);
        if (!byName.contains(current)) {
            node = current;
            break;
        }
        const auto group = byName.value(current).toObject();
        const auto type = group.value(QStringLiteral("type")).toString().toLower();
        if (type == QStringLiteral("loadbalance") || type == QStringLiteral("load-balance")) {
            balanced = true;
            break; // A balancing group has no single node for every new connection.
        }
        current = group.value(QStringLiteral("current")).toString();
    }
    return {{QStringLiteral("mode"), mode},         {QStringLiteral("policy"), policy},
            {QStringLiteral("chain"), chain},       {QStringLiteral("node"), node},
            {QStringLiteral("balanced"), balanced}, {QStringLiteral("confirmed"), ready}};
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
        for (const auto* kind : {"proxy-providers", "rule-providers"})
            for (auto entry : root[kind]) {
                auto provider = entry.second;
                provider["url"] = "[redacted]";
                provider.remove("path");
                provider.remove("payload");
                if (provider["health-check"])
                    provider["health-check"] = "[redacted]";
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

domain::Result<QJsonArray> providerRequests(const QByteArray& input) {
    try {
        const auto metadata = configMetadata(input);
        if (const auto* failure = std::get_if<domain::Error>(&metadata))
            return *failure;
        const auto root = YAML::Load(std::get<ConfigMetadata>(metadata).fieldsYaml.toStdString());
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
domain::Result<QByteArray> materializeSession(const PreflightConfig& config,
                                              const QByteArray& secret) {
    if (secret.size() < 32 || secret.size() > 256) {
        return inputError("InvalidControllerSecret");
    }
    try {
        if (config.semanticYaml.trimmed().startsWith('{')) {
            // Older stored artifacts can retain a flow root. Keep them readable on upgrade.
            auto root = YAML::Load(config.semanticYaml.toStdString());
            require(root.IsMap());
            root["secret"] = secret.toStdString();
            return serializeYaml(root);
        }
        // Preflight emits a block root mapping with managed secret removed. Append the only
        // session field without rebuilding the entire (possibly 20,000-rule) YAML graph.
        // The scalar still uses the YAML emitter so arbitrary secret bytes cannot inject YAML.
        YAML::Emitter scalar;
        scalar << YAML::DoubleQuoted << secret.toStdString();
        if (!scalar.good() || config.semanticYaml.isEmpty())
            return inputError("InvalidYaml");
        auto session = config.semanticYaml;
        session += "\nsecret: ";
        session.append(scalar.c_str(), static_cast<qsizetype>(scalar.size()));
        session += '\n';
        return session;
    } catch (const YAML::Exception&) {
        return inputError("InvalidYaml");
    } catch (const InvalidConfig& error) {
        return inputError(error.code);
    }
}
} // namespace quickclash::infra
