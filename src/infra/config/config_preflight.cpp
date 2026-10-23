#include "infra/config/config_preflight.h"
#include <QStringList>
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
void allowedKeys(const YAML::Node& node, const std::set<std::string>& keys) {
    require(node.IsMap());
    for (const auto& entry : node) {
        require(keys.contains(entry.first.as<std::string>()), "UnsupportedConfigFeature");
    }
}
QByteArray serializeYaml(const YAML::Node& node) {
    YAML::Emitter emitter;
    emitter << node;
    require(emitter.good());
    return QByteArray(emitter.c_str(), static_cast<qsizetype>(emitter.size()));
}
} // namespace

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
        allowedKeys(root, {"proxies",
                           "proxy-groups",
                           "rules",
                           "mode",
                           "log-level",
                           "ipv6",
                           "mixed-port",
                           "port",
                           "socks-port",
                           "redir-port",
                           "tproxy-port",
                           "allow-lan",
                           "bind-address",
                           "external-controller",
                           "secret",
                           "external-ui",
                           "external-ui-url",
                           "tun",
                           "dns",
                           "profile",
                           "geodata-mode",
                           "geo-auto-update"});
        const auto dns = root["dns"];
        if (dns) {
            allowedKeys(dns, {"enable", "listen", "enhanced-mode", "nameserver", "fallback",
                              "default-nameserver", "ipv6"});
            require(!dns["enhanced-mode"] || dns["enhanced-mode"].as<std::string>() != "fake-ip",
                    "UnsupportedConfigFeature");
        }
        if (root["tun"]) {
            allowedKeys(root["tun"], {"enable", "stack", "auto-route", "auto-detect-interface"});
        }
        if (root["profile"]) {
            allowedKeys(root["profile"], {"store-selected", "store-fake-ip"});
        }
        require(root["proxies"].IsSequence() && root["proxies"].size() <= 5000);
        std::set<std::string> names{"DIRECT", "REJECT"};
        for (const auto& proxy : root["proxies"]) {
            allowedKeys(
                proxy, {"name", "type", "server", "port", "username", "password", "cipher", "udp"});
            const auto name = proxy["name"].as<std::string>();
            require(!name.empty() && name.size() <= 512 && names.insert(name).second);
            const auto type = proxy["type"].as<std::string>();
            require(type == "http" || type == "socks5" || type == "ss", "UnsupportedConfigFeature");
            require(!proxy["server"].as<std::string>().empty());
            const auto port = proxy["port"].as<unsigned>();
            require(port > 0 && port <= 65535);
            if (type == "ss") {
                require(proxy["cipher"] && proxy["password"]);
            }
        }
        require(root["proxy-groups"].IsSequence() && root["proxy-groups"].size() <= 500);
        for (const auto& group : root["proxy-groups"]) {
            allowedKeys(group, {"name", "type", "proxies"});
            const auto name = group["name"].as<std::string>();
            require(!name.empty() && name.size() <= 512 && names.insert(name).second);
            require(group["type"].as<std::string>() == "select", "UnsupportedConfigFeature");
            require(group["proxies"].IsSequence() && group["proxies"].size() > 0);
        }
        for (const auto& group : root["proxy-groups"]) {
            for (const auto& member : group["proxies"]) {
                require(names.contains(member.as<std::string>()));
            }
        }
        require(root["rules"].IsSequence() && root["rules"].size() <= 20000);
        for (const auto& rule : root["rules"]) {
            const auto parts = QString::fromStdString(rule.as<std::string>()).split(',');
            const auto type = parts.front();
            const bool match = type == QStringLiteral("MATCH");
            require(match || type == QStringLiteral("DOMAIN") ||
                        type == QStringLiteral("DOMAIN-SUFFIX") ||
                        type == QStringLiteral("DOMAIN-KEYWORD") ||
                        type == QStringLiteral("IP-CIDR") || type == QStringLiteral("IP-CIDR6"),
                    "UnsupportedConfigFeature");
            require(parts.size() == (match ? 2 : 3) ||
                    (!match && parts.size() == 4 &&
                     (type == QStringLiteral("IP-CIDR") || type == QStringLiteral("IP-CIDR6")) &&
                     parts.back() == QStringLiteral("no-resolve")));
            require(names.contains(parts[match ? 1 : 2].toStdString()));
        }
        PreflightConfig output;
        const std::vector<std::string> managed{"mixed-port",
                                               "port",
                                               "socks-port",
                                               "redir-port",
                                               "tproxy-port",
                                               "allow-lan",
                                               "bind-address",
                                               "external-controller",
                                               "secret",
                                               "external-ui",
                                               "external-ui-url",
                                               "tun",
                                               "dns",
                                               "profile",
                                               "mode",
                                               "geodata-mode",
                                               "geo-auto-update"};
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
        root["dns"]["enable"] = false;
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
