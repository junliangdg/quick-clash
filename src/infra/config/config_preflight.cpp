#include "infra/config/config_preflight.h"
#include "infra/config/config_schema.h"
#include <QJsonArray>
#include <QMessageAuthenticationCode>
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
            identities.insert(
                name, QStringLiteral("group:") +
                          QString::fromLatin1(QMessageAuthenticationCode::hash(
                                                  name.toUtf8() + serializeYaml(root["proxies"]) +
                                                      serializeYaml(root["proxy-groups"]),
                                                  key, QCryptographicHash::Sha256)
                                                  .toHex()));
        }
        identities.insert(QStringLiteral("DIRECT"), QStringLiteral("builtin:direct"));
        identities.insert(QStringLiteral("REJECT"), QStringLiteral("builtin:reject"));
        identities.insert(QStringLiteral("GLOBAL"),
                          QStringLiteral("global:") +
                              QString::fromLatin1(
                                  QMessageAuthenticationCode::hash(serializeYaml(root["proxies"]),
                                                                   key, QCryptographicHash::Sha256)
                                      .toHex()));
    } catch (const YAML::Exception&) {
        return {};
    } catch (const InvalidConfig&) {
        return {};
    }
    return identities;
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
