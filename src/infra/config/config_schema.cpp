#include "infra/config/config_schema.h"
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace quickclash::infra {
namespace {
using Keys = std::set<std::string>;
struct InvalidField {
    QString path;
};
void require(bool value, const QString& path) {
    if (!value)
        throw InvalidField{path};
}
std::string text(const YAML::Node& node, const QString& path, std::size_t limit = 2048,
                 bool empty = false) {
    require(node.IsScalar() && node.Scalar().size() <= limit, path);
    const auto value = node.Scalar();
    require(empty || !value.empty(), path);
    return value;
}
int number(const YAML::Node& node, const QString& path, int minimum, int maximum) {
    int value = 0;
    require(node.IsScalar() && node.Scalar().size() <= 12, path);
    try {
        value = node.as<int>();
    } catch (const YAML::Exception&) {
        throw InvalidField{path};
    }
    require(value >= minimum && value <= maximum, path);
    return value;
}
void boolean(const YAML::Node& node, const QString& path) {
    require(node.IsScalar(), path);
    try {
        static_cast<void>(node.as<bool>());
    } catch (const YAML::Exception&) {
        throw InvalidField{path};
    }
}
bool oneOf(const std::string& value, const Keys& keys) {
    return keys.contains(value);
}
class Schema final {
  public:
    std::vector<domain::ConfigIssue> issues;
    void attempt(const std::function<void()>& action, const QString& path) {
        try {
            action();
        } catch (const InvalidField& field) {
            issue("InvalidConfigField", field.path, "field", "P2");
        } catch (const YAML::Exception&) {
            issue("InvalidConfigField", path, "field", "P2");
        }
    }
    void issue(const char* code, const QString& path, const std::string& feature,
               const char* phase = "P3") {
        if (issues.size() < 32)
            issues.push_back({QString::fromLatin1(code), path, QString::fromStdString(feature),
                              QString::fromLatin1(phase)});
    }
    void keys(const YAML::Node& node, const Keys& allowed, const QString& path) {
        require(node.IsMap(), path);
        for (const auto& entry : node) {
            const auto key = entry.first.as<std::string>();
            if (allowed.contains(key))
                continue;
            // Only fixed vocabulary is exposed. A YAML key may itself contain a credential.
            if (oneOf(key, {"proxy-providers", "rule-providers", "geox-url", "external-ui-url",
                            "certificate", "private-key", "ca", "ca-str", "client-cert",
                            "client-key", "plugin", "plugin-opts"}))
                issue("UnsupportedResource", path + '.' + QString::fromStdString(key), key);
            else
                issue("UnsupportedField", path + QStringLiteral(".<field>"), "field");
        }
    }
    void transport(const YAML::Node& proxy, const std::string& type, const QString& path) {
        if (proxy["network"]) {
            const auto network = text(proxy["network"], path + ".network", 64);
            if (!oneOf(network, {"tcp", "ws", "grpc"})) {
                issue("UnsupportedTransport", path + ".network",
                      oneOf(network, {"h2", "http", "quic", "httpupgrade", "xhttp"}) ? network
                                                                                     : "transport");
                return;
            }
        }
        const auto network = proxy["network"] ? proxy["network"].as<std::string>() : "tcp";
        if (proxy["ws-opts"]) {
            require(network == "ws", path + ".ws-opts");
            const auto ws = proxy["ws-opts"];
            keys(ws, {"path", "headers", "max-early-data", "early-data-header-name"},
                 path + ".ws-opts");
            if (ws["path"])
                text(ws["path"], path + ".ws-opts.path", 4096, true);
            if (ws["headers"]) {
                keys(ws["headers"], {"Host"}, path + ".ws-opts.headers");
                if (ws["headers"]["Host"])
                    text(ws["headers"]["Host"], path + ".ws-opts.headers.Host");
            }
            if (ws["max-early-data"])
                number(ws["max-early-data"], path + ".ws-opts.max-early-data", 0, 65536);
            if (ws["early-data-header-name"])
                text(ws["early-data-header-name"], path + ".ws-opts.early-data-header-name", 128);
        }
        if (proxy["grpc-opts"]) {
            require(network == "grpc", path + ".grpc-opts");
            keys(proxy["grpc-opts"], {"grpc-service-name"}, path + ".grpc-opts");
            if (proxy["grpc-opts"]["grpc-service-name"])
                text(proxy["grpc-opts"]["grpc-service-name"],
                     path + ".grpc-opts.grpc-service-name");
        }
        if (proxy["reality-opts"]) {
            require(type == "vless" && proxy["tls"] && proxy["tls"].as<bool>() && network == "tcp",
                    path + ".reality-opts");
            const auto reality = proxy["reality-opts"];
            keys(reality, {"public-key", "short-id"}, path + ".reality-opts");
            const auto publicKey = text(reality["public-key"], path + ".reality-opts.public-key");
            require(QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{43}$"))
                        .match(QString::fromStdString(publicKey))
                        .hasMatch(),
                    path + ".reality-opts.public-key");
            if (reality["short-id"]) {
                const auto shortId =
                    text(reality["short-id"], path + ".reality-opts.short-id", 16, true);
                require(shortId.size() % 2 == 0 &&
                            QRegularExpression(QStringLiteral("^[a-fA-F0-9]*$"))
                                .match(QString::fromStdString(shortId))
                                .hasMatch(),
                        path + ".reality-opts.short-id");
            }
        }
        if (proxy["flow"])
            require(
                type == "vless" && network == "tcp" && proxy["tls"] && proxy["tls"].as<bool>() &&
                    oneOf(text(proxy["flow"], path + ".flow", 64, true), {"", "xtls-rprx-vision"}),
                path + ".flow");
    }
    void proxy(const YAML::Node& proxy, const QString& path) {
        require(proxy.IsMap(), path);
        const auto type = text(proxy["type"], path + ".type", 64);
        if (!oneOf(type, {"http", "socks5", "ss", "hysteria2", "trojan", "vless", "vmess"})) {
            issue("UnsupportedProtocol", path + ".type",
                  oneOf(type, {"tuic", "hysteria", "wireguard", "ssh", "snell"}) ? type
                                                                                 : "protocol");
            return;
        }
        Keys allowed{"name", "type", "server", "port", "udp"};
        const auto add = [&](const Keys& fields) { allowed.insert(fields.begin(), fields.end()); };
        if (type == "http" || type == "socks5")
            add({"username", "password", "tls", "sni", "skip-cert-verify"});
        else if (type == "ss")
            add({"password", "cipher"});
        else if (type == "hysteria2")
            add({"password", "sni", "skip-cert-verify", "alpn", "up", "down", "obfs",
                 "obfs-password"});
        else {
            add({"tls", "skip-cert-verify", "alpn", "client-fingerprint", "network", "ws-opts",
                 "grpc-opts"});
            if (type == "trojan")
                add({"password", "sni"});
            else
                add({"uuid", "servername", "packet-encoding"});
            if (type == "vless")
                add({"flow", "reality-opts"});
            if (type == "vmess")
                add({"alterId", "cipher"});
        }
        keys(proxy, allowed, path);
        text(proxy["server"], path + ".server");
        number(proxy["port"], path + ".port", 1, 65535);
        for (const auto* field : {"udp", "tls", "skip-cert-verify"})
            if (allowed.contains(field) && proxy[field])
                boolean(proxy[field], path + '.' + field);
        for (const auto* field : {"sni", "servername", "username", "client-fingerprint"})
            if (allowed.contains(field) && proxy[field])
                text(proxy[field], path + '.' + field);
        if (type == "ss" || type == "trojan" || type == "hysteria2")
            text(proxy["password"], path + ".password", 4096);
        else if (allowed.contains("password") && proxy["password"])
            text(proxy["password"], path + ".password", 4096, true);
        if (type == "ss")
            text(proxy["cipher"], path + ".cipher", 128);
        if (type == "vless" || type == "vmess") {
            const auto uuid = text(proxy["uuid"], path + ".uuid", 36);
            require(QRegularExpression(
                        QStringLiteral("^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$"))
                        .match(QString::fromStdString(uuid))
                        .hasMatch(),
                    path + ".uuid");
            if (proxy["packet-encoding"])
                require(oneOf(text(proxy["packet-encoding"], path + ".packet-encoding"),
                              {"xudp", "packetaddr"}),
                        path + ".packet-encoding");
        }
        if (type == "vmess") {
            if (proxy["alterId"])
                number(proxy["alterId"], path + ".alterId", 0, 65535);
            if (proxy["cipher"])
                require(oneOf(text(proxy["cipher"], path + ".cipher"),
                              {"auto", "none", "zero", "aes-128-gcm", "chacha20-poly1305"}),
                        path + ".cipher");
        }
        if (allowed.contains("alpn") && proxy["alpn"]) {
            require(proxy["alpn"].IsSequence() && proxy["alpn"].size() <= 16, path + ".alpn");
            for (const auto& alpn : proxy["alpn"])
                text(alpn, path + ".alpn[]", 64);
        }
        if (type == "hysteria2") {
            for (const auto* field : {"up", "down"})
                if (proxy[field])
                    number(proxy[field], path + '.' + field, 1, 1000000);
            if (proxy["obfs"] || proxy["obfs-password"]) {
                require(text(proxy["obfs"], path + ".obfs") == "salamander", path + ".obfs");
                text(proxy["obfs-password"], path + ".obfs-password", 4096);
            }
        }
        if (type == "trojan" || type == "vless" || type == "vmess")
            transport(proxy, type, path);
    }
    void validate(const YAML::Node& root) {
        keys(root,
             {"proxies",
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
              "geo-auto-update",
              "unified-delay",
              "tcp-concurrent"},
             QStringLiteral("$"));
        for (const auto* field : {"unified-delay", "tcp-concurrent", "ipv6"})
            if (root[field])
                boolean(root[field], QStringLiteral("$.") + field);
        if (root["log-level"])
            require(oneOf(text(root["log-level"], "$.log-level"),
                          {"silent", "error", "warning", "info", "debug"}),
                    "$.log-level");
        if (root["dns"]) {
            const auto dns = root["dns"];
            keys(dns,
                 {"enable", "listen", "enhanced-mode", "nameserver", "fallback",
                  "default-nameserver", "ipv6"},
                 "$.dns");
            if (dns["enable"])
                boolean(dns["enable"], "$.dns.enable");
            if ((dns["enable"] && dns["enable"].as<bool>()) ||
                (dns["enhanced-mode"] &&
                 text(dns["enhanced-mode"], "$.dns.enhanced-mode") == "fake-ip"))
                issue("UnsupportedDns", "$.dns", "DNS");
        }
        if (root["tun"])
            keys(root["tun"], {"enable", "stack", "auto-route", "auto-detect-interface"}, "$.tun");
        if (root["profile"])
            keys(root["profile"], {"store-selected", "store-fake-ip"}, "$.profile");
        const auto proxies = root["proxies"];
        const auto groups = root["proxy-groups"];
        require(proxies.IsSequence() && proxies.size() <= 5000, "$.proxies");
        require(groups.IsSequence() && groups.size() <= 500, "$.proxy-groups");
        Keys names{"DIRECT", "REJECT", "GLOBAL"};
        for (std::size_t i = 0; i < proxies.size(); ++i) {
            const auto path = QStringLiteral("$.proxies[%1]").arg(i);
            const auto name = text(proxies[i]["name"], path + ".name", 512);
            require(names.insert(name).second, path + ".name");
            attempt([&] { proxy(proxies[i], path); }, path);
        }
        std::map<std::string, std::vector<std::string>> graph;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            const auto path = QStringLiteral("$.proxy-groups[%1]").arg(i);
            attempt(
                [&] {
                    const auto group = groups[i];
                    const auto name = text(group["name"], path + ".name", 512);
                    require(names.insert(name).second, path + ".name");
                    const auto type = text(group["type"], path + ".type", 64);
                    if (!oneOf(type, {"select", "url-test", "fallback"})) {
                        issue("UnsupportedGroup", path + ".type",
                              type == "load-balance" ? type : "group");
                        return;
                    }
                    Keys fields{"name", "type", "proxies"};
                    if (type != "select")
                        fields.insert({"url", "interval", "timeout", "lazy", "expected-status"});
                    if (type == "url-test")
                        fields.insert("tolerance");
                    keys(group, fields, path);
                    if (type != "select") {
                        const QUrl url(
                            QString::fromStdString(text(group["url"], path + ".url", 4096)));
                        require(url.isValid() &&
                                    (url.scheme() == "https" || url.scheme() == "http") &&
                                    !url.host().isEmpty() && url.userInfo().isEmpty() &&
                                    !url.hasFragment(),
                                path + ".url");
                        if (group["interval"])
                            number(group["interval"], path + ".interval", 1, 86400);
                        if (group["timeout"])
                            number(group["timeout"], path + ".timeout", 1, 60000);
                        if (group["lazy"])
                            boolean(group["lazy"], path + ".lazy");
                        if (group["tolerance"])
                            number(group["tolerance"], path + ".tolerance", 0, 60000);
                        if (group["expected-status"])
                            require(
                                QRegularExpression(
                                    QStringLiteral("^[1-5][0-9]{2}(-[1-5][0-9]{2})?((/"
                                                   "[1-5][0-9]{2}(-[1-5][0-9]{2})?))*$"))
                                    .match(QString::fromStdString(text(
                                        group["expected-status"], path + ".expected-status", 128)))
                                    .hasMatch(),
                                path + ".expected-status");
                    }
                    require(group["proxies"].IsSequence() && group["proxies"].size() > 0 &&
                                group["proxies"].size() <= 5000,
                            path + ".proxies");
                    Keys members;
                    for (const auto& member : group["proxies"]) {
                        const auto value = text(member, path + ".proxies[]", 512);
                        require(value != "GLOBAL" && members.insert(value).second,
                                path + ".proxies[]");
                        graph[name].push_back(value);
                    }
                },
                path);
        }
        for (const auto& [name, members] : graph) {
            Q_UNUSED(name)
            for (const auto& member : members)
                attempt([&] { require(names.contains(member), "$.proxy-groups[].proxies[]"); },
                        "$.proxy-groups[].proxies[]");
        }
        std::map<std::string, int> visiting;
        std::map<std::string, int> heights;
        std::function<int(const std::string&, int)> visit = [&](const std::string& name,
                                                                int depth) {
            require(depth <= 16 && visiting[name] != 1, "$.proxy-groups[].proxies[]");
            if (visiting[name] == 2)
                return heights[name];
            visiting[name] = 1;
            int height = 1;
            for (const auto& member : graph[name]) {
                if (graph.contains(member))
                    height = std::max(height, 1 + visit(member, depth + 1));
            }
            require(height <= 16, "$.proxy-groups[].proxies[]");
            visiting[name] = 2;
            heights[name] = height;
            return height;
        };
        for (const auto& [name, members] : graph) {
            Q_UNUSED(members)
            attempt([&] { visit(name, 1); }, "$.proxy-groups[].proxies[]");
        }
        require(root["rules"].IsSequence() && root["rules"].size() <= 20000, "$.rules");
        for (std::size_t i = 0; i < root["rules"].size(); ++i) {
            const auto path = QStringLiteral("$.rules[%1]").arg(i);
            attempt(
                [&] {
                    const auto parts =
                        QString::fromStdString(text(root["rules"][i], path, 8192)).split(',');
                    const auto type = parts.front().toStdString();
                    if (!oneOf(type, {"DOMAIN", "DOMAIN-SUFFIX", "DOMAIN-KEYWORD", "IP-CIDR",
                                      "IP-CIDR6", "MATCH"})) {
                        issue("UnsupportedRule", path,
                              oneOf(type, {"GEOIP", "GEOSITE", "RULE-SET"}) ? type : "rule");
                        return;
                    }
                    const bool match = type == "MATCH";
                    require(parts.size() == (match ? 2 : 3) ||
                                (!match && parts.size() == 4 &&
                                 (type == "IP-CIDR" || type == "IP-CIDR6") &&
                                 parts.back() == "no-resolve"),
                            path);
                    require(names.contains(parts[match ? 1 : 2].toStdString()), path);
                },
                path);
        }
    }
};
} // namespace
domain::CommandResult validateConfigSchema(const YAML::Node& root) {
    Schema schema;
    domain::Error error;
    error.stage = QStringLiteral("preflight");
    try {
        schema.validate(root);
    } catch (const InvalidField& field) {
        schema.issue("InvalidConfigField", field.path, "field", "P2");
    } catch (const YAML::Exception&) {
        schema.issue("InvalidConfigField", QStringLiteral("$"), "field", "P2");
    }
    if (schema.issues.empty())
        return std::monostate{};
    error.code = schema.issues.front().code == QStringLiteral("InvalidConfigField")
                     ? QStringLiteral("InvalidConfigField")
                     : QStringLiteral("UnsupportedConfigFeature");
    error.userMessageKey = error.code;
    error.configIssues = std::move(schema.issues);
    return error;
}
} // namespace quickclash::infra
