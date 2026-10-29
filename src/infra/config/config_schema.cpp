#include "infra/config/config_schema.h"
#include "infra/config/config_preflight.h"
#include <QHostAddress>
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
        if (proxy["flow"]) {
            const auto flow = text(proxy["flow"], path + ".flow", 64, true);
            require(flow.empty() || (type == "vless" && network == "tcp" && proxy["tls"] &&
                                     proxy["tls"].as<bool>() && flow == "xtls-rprx-vision"),
                    path + ".flow");
        }
    }
    void proxy(const YAML::Node& proxy, const QString& path) {
        require(proxy.IsMap(), path);
        const auto type = text(proxy["type"], path + ".type", 64);
        if (!oneOf(type,
                   {"http", "socks5", "ss", "hysteria2", "trojan", "vless", "vmess", "tuic"})) {
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
        else if (type == "tuic")
            add({"uuid", "password", "token", "sni", "alpn", "disable-sni", "reduce-rtt",
                 "request-timeout", "udp-relay-mode", "congestion-controller", "heartbeat-interval",
                 "skip-cert-verify", "max-udp-relay-packet-size", "fast-open", "max-open-streams"});
        else if (type == "hysteria2")
            add({"password", "sni", "skip-cert-verify", "alpn", "up", "down", "obfs",
                 "obfs-password", "ports", "mport", "hop-interval", "fingerprint"});
        else {
            add({"tls", "skip-cert-verify", "alpn", "client-fingerprint", "network", "ws-opts",
                 "grpc-opts", "fingerprint"});
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
        if (type == "tuic") {
            const bool token = static_cast<bool>(proxy["token"]);
            require(token ? !proxy["uuid"] && !proxy["password"]
                          : proxy["uuid"] && proxy["password"],
                    path + ".uuid");
            if (proxy["uuid"])
                require(QRegularExpression(
                            QStringLiteral("^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$"))
                            .match(QString::fromStdString(text(proxy["uuid"], path + ".uuid", 36)))
                            .hasMatch(),
                        path + ".uuid");
            for (const auto* field :
                 {"uuid", "password", "token", "sni", "udp-relay-mode", "congestion-controller"})
                if (proxy[field])
                    text(proxy[field], path + '.' + field, 4096);
            if (proxy["udp-relay-mode"])
                require(oneOf(proxy["udp-relay-mode"].as<std::string>(), {"native", "quic"}),
                        path + ".udp-relay-mode");
            if (proxy["congestion-controller"])
                require(oneOf(proxy["congestion-controller"].as<std::string>(),
                              {"cubic", "new_reno", "bbr"}),
                        path + ".congestion-controller");
            for (const auto* field : {"disable-sni", "reduce-rtt", "fast-open"})
                if (proxy[field])
                    boolean(proxy[field], path + '.' + field);
            if (proxy["udp"])
                require(proxy["udp"].as<bool>(), path + ".udp");
            for (const auto* field : {"request-timeout", "heartbeat-interval",
                                      "max-udp-relay-packet-size", "max-open-streams"})
                if (proxy[field])
                    number(proxy[field], path + '.' + field, 1, 1000000);
        }
        text(proxy["server"], path + ".server");
        number(proxy["port"], path + ".port", 1, 65535);
        for (const auto* field : {"udp", "tls", "skip-cert-verify"})
            if (allowed.contains(field) && proxy[field])
                boolean(proxy[field], path + '.' + field);
        for (const auto* field : {"sni", "servername", "username", "client-fingerprint"})
            if (allowed.contains(field) && proxy[field])
                text(proxy[field], path + '.' + field);
        if (allowed.contains("fingerprint") && proxy["fingerprint"]) {
            const auto value = text(proxy["fingerprint"], path + ".fingerprint", 128, true);
            require(value.empty() || QRegularExpression(QStringLiteral("^[a-fA-F0-9:]{64,95}$"))
                                         .match(QString::fromStdString(value))
                                         .hasMatch(),
                    path + ".fingerprint");
        }
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
            if (proxy["mport"] && proxy["ports"] && !proxy["mport"].Scalar().empty() &&
                !proxy["ports"].Scalar().empty())
                require(proxy["mport"].Scalar() == proxy["ports"].Scalar(), path + ".ports");
            for (const auto* field : {"ports", "mport"}) {
                if (!proxy[field])
                    continue;
                const auto ranges =
                    QString::fromStdString(text(proxy[field], path + '.' + field, 1024, true));
                if (ranges.isEmpty())
                    continue;
                for (const auto& range : ranges.split(',')) {
                    const auto bounds = range.split('-');
                    require(bounds.size() <= 2, path + '.' + field);
                    int previous = 0;
                    for (const auto& bound : bounds) {
                        bool valid = false;
                        const auto port = bound.toInt(&valid);
                        require(valid && port >= 1 && port <= 65535 && port >= previous,
                                path + '.' + field);
                        previous = port;
                    }
                }
            }
            if (proxy["hop-interval"])
                require(QRegularExpression(QStringLiteral("^[0-9]{1,5}(s)?$"))
                            .match(QString::fromStdString(
                                text(proxy["hop-interval"], path + ".hop-interval", 6)))
                            .hasMatch(),
                        path + ".hop-interval");
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
    void validate(const YAML::Node& root, const std::vector<std::string>* separateRules) {
        for (const auto* kind : {"proxy-providers", "rule-providers"})
            for (const auto& entry : root[kind])
                if (!entry.second["payload"] || !entry.second["size-limit"]) {
                    issue("UnsupportedResource", QStringLiteral("$.") + kind, kind);
                    return; // Raw providers must pass the managed import pipeline first.
                }
        keys(root,
             {"proxies",
              "proxy-providers",
              "rule-providers",
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
                 {"enable",
                  "listen",
                  "enhanced-mode",
                  "nameserver",
                  "fallback",
                  "default-nameserver",
                  "ipv6",
                  "fake-ip-range",
                  "fake-ip-filter",
                  "use-hosts",
                  "respect-rules",
                  "proxy-server-nameserver",
                  "fallback-filter",
                  "nameserver-policy",
                  "direct-nameserver",
                  "direct-nameserver-follow-policy",
                  "fake-ip-filter-mode",
                  "cache-algorithm",
                  "prefer-h3",
                  "use-system-hosts"},
                 "$.dns");
            for (const auto* field :
                 {"enable", "ipv6", "use-hosts", "respect-rules", "direct-nameserver-follow-policy",
                  "prefer-h3", "use-system-hosts"})
                if (dns[field])
                    boolean(dns[field], QStringLiteral("$.dns.") + field);
            if (dns["listen"])
                text(dns["listen"], "$.dns.listen", 256, true);
            if (dns["enhanced-mode"]) {
                const auto mode = text(dns["enhanced-mode"], "$.dns.enhanced-mode", 64);
                if (!oneOf(mode, {"fake-ip", "redir-host", "normal"}))
                    issue("UnsupportedDns", "$.dns.enhanced-mode", "DNS");
            }
            if (dns["fake-ip-range"]) {
                const auto range =
                    QString::fromStdString(text(dns["fake-ip-range"], "$.dns.fake-ip-range", 32));
                const auto subnet = QHostAddress::parseSubnet(range);
                require(subnet.first.protocol() == QAbstractSocket::IPv4Protocol &&
                            subnet.second >= 15 && subnet.second <= 30 &&
                            subnet.first.isInSubnet(QHostAddress(QStringLiteral("198.18.0.0")), 15),
                        "$.dns.fake-ip-range");
            }
            const auto list = [&](const YAML::Node& values, const QString& path, bool resolver) {
                require(values.IsSequence() && values.size() <= 256 &&
                            (!resolver || values.size() > 0),
                        path);
                for (const auto& value : values) {
                    const auto entry = QString::fromStdString(text(value, path + "[]", 2048));
                    if (!resolver)
                        continue;
                    const QUrl url(entry.contains("://") ? entry
                                                         : QStringLiteral("udp://") + entry);
                    require(url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
                                !url.hasFragment() &&
                                (url.scheme() == "udp" || url.scheme() == "tcp" ||
                                 url.scheme() == "tls" || url.scheme() == "https"),
                            path + "[]");
                    const auto host = url.host().toLower();
                    const QHostAddress address(host);
                    bool isV4 = false;
                    const auto ipv4 = address.toIPv4Address(&isV4);
                    require(!address.isLoopback() &&
                                !(isV4 && ((ipv4 >> 24) == 127 || ipv4 == 0)) &&
                                address != QHostAddress::AnyIPv6 && !host.contains('%') &&
                                host != QStringLiteral("localhost") &&
                                !host.endsWith(QStringLiteral(".localhost")) &&
                                host != QStringLiteral("localhost.") &&
                                !host.endsWith(QStringLiteral(".localhost.")),
                            path + "[]");
                    if (path == QStringLiteral("$.dns.default-nameserver"))
                        require(!address.isNull(), path + "[]");
                }
            };
            for (const auto* field :
                 {"nameserver", "fallback", "default-nameserver", "proxy-server-nameserver"})
                if (dns[field])
                    list(dns[field], QStringLiteral("$.dns.") + field, true);
            if (dns["fake-ip-filter"])
                list(dns["fake-ip-filter"], "$.dns.fake-ip-filter", false);
            if (dns["direct-nameserver"])
                list(dns["direct-nameserver"], "$.dns.direct-nameserver", true);
            if (dns["cache-algorithm"])
                require(
                    oneOf(text(dns["cache-algorithm"], "$.dns.cache-algorithm"), {"lru", "arc"}),
                    "$.dns.cache-algorithm");
            if (dns["fake-ip-filter-mode"])
                require(oneOf(text(dns["fake-ip-filter-mode"], "$.dns.fake-ip-filter-mode"),
                              {"blacklist", "whitelist"}),
                        "$.dns.fake-ip-filter-mode");
            if (dns["nameserver-policy"]) {
                require(dns["nameserver-policy"].IsMap() && dns["nameserver-policy"].size() <= 256,
                        "$.dns.nameserver-policy");
                for (const auto& entry : dns["nameserver-policy"]) {
                    text(entry.first, "$.dns.nameserver-policy.<key>", 2048);
                    if (entry.second.IsSequence())
                        list(entry.second, "$.dns.nameserver-policy.<value>", true);
                    else {
                        YAML::Node resolverList(YAML::NodeType::Sequence);
                        resolverList.push_back(entry.second);
                        list(resolverList, "$.dns.nameserver-policy.<value>", true);
                    }
                }
            }
            if (dns["respect-rules"] && dns["respect-rules"].as<bool>())
                require(dns["proxy-server-nameserver"] && dns["proxy-server-nameserver"].size() > 0,
                        "$.dns.proxy-server-nameserver");
            if (dns["fallback-filter"]) {
                const auto filter = dns["fallback-filter"];
                keys(filter, {"geoip", "geoip-code", "geosite", "ipcidr", "domain"},
                     "$.dns.fallback-filter");
                if (filter["geoip"])
                    boolean(filter["geoip"], "$.dns.fallback-filter.geoip");
                if (filter["geoip-code"])
                    require(QRegularExpression(QStringLiteral("^[a-zA-Z]{2}$"))
                                .match(QString::fromStdString(text(
                                    filter["geoip-code"], "$.dns.fallback-filter.geoip-code", 2)))
                                .hasMatch(),
                            "$.dns.fallback-filter.geoip-code");
                for (const auto* field : {"geosite", "ipcidr", "domain"})
                    if (filter[field])
                        list(filter[field], QStringLiteral("$.dns.fallback-filter.") + field,
                             false);
            }
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
        Keys proxyProviders, ruleProviders, dynamicNames;
        std::size_t providerCount = 0, dynamicCount = 0, providerRules = 0;
        for (const auto* kind : {"proxy-providers", "rule-providers"}) {
            const auto providers = root[kind];
            if (!providers)
                continue;
            require(providers.IsMap() && providers.size() <= 32 - providerCount, "$<providers>");
            providerCount += providers.size();
            for (const auto& entry : providers) {
                const bool proxyProvider = std::string(kind) == "proxy-providers";
                const auto path = QStringLiteral("$.") + kind + QStringLiteral(".<provider>");
                const auto name = text(entry.first, path, 512);
                require(name != "default" && name != "GLOBAL" && name != "DIRECT" &&
                            name != "REJECT",
                        path);
                (proxyProvider ? proxyProviders : ruleProviders).insert(name);
                const auto value = entry.second;
                Keys allowed{"type", "url", "path", "interval", "size-limit", "payload"};
                if (proxyProvider)
                    allowed.insert("health-check");
                else
                    allowed.insert({"behavior", "format"});
                keys(value, allowed, path);
                require(text(value["type"], path + ".type") == "http", path + ".type");
                const auto address = text(value["url"], path + ".url", 8192);
                const QUrl url(QString::fromStdString(address));
                require(url.isValid() && url.scheme() == "https" && !url.host().isEmpty() &&
                            url.userInfo().isEmpty() && !url.hasFragment(),
                        path + ".url");
                const auto format =
                    value["format"] ? text(value["format"], path + ".format") : "yaml";
                const auto behavior =
                    value["behavior"] ? text(value["behavior"], path + ".behavior") : "";
                require(format == "yaml" || (!proxyProvider && format == "text"), path + ".format");
                require(proxyProvider || oneOf(behavior, {"domain", "ipcidr", "classical"}),
                        path + ".behavior");
                require(text(value["path"], path + ".path") ==
                            runtimeProviderPath(
                                QString::fromLatin1(kind), QString::fromStdString(name),
                                QString::fromStdString(address), QString::fromStdString(format),
                                QString::fromStdString(behavior))
                                .toStdString(),
                        path + ".path");
                number(value["size-limit"], path + ".size-limit", 1, 4 * 1024 * 1024);
                number(value["interval"], path + ".interval", 0, 604800);
                const auto payload = value["payload"];
                require(payload.IsSequence(), path + ".payload");
                if (proxyProvider) {
                    require(payload.size() > 0 &&
                                payload.size() <= 5000 - proxies.size() - dynamicCount,
                            path + ".payload");
                    dynamicCount += payload.size();
                    for (const auto& node : payload) {
                        const auto nodeName = text(node["name"], path + ".payload[].name", 512);
                        require(!names.contains(nodeName) && dynamicNames.insert(nodeName).second,
                                path + ".payload[].name");
                        proxy(node, path + ".payload[]");
                    }
                } else {
                    require(payload.size() <= 20000 - providerRules, path + ".payload");
                    providerRules += payload.size();
                    for (const auto& rule : payload)
                        text(rule, path + ".payload[]", 8192);
                }
                if (value["health-check"]) {
                    const auto health = value["health-check"];
                    keys(health,
                         {"enable", "url", "interval", "timeout", "lazy", "expected-status"},
                         path + ".health-check");
                    boolean(health["enable"], path + ".health-check.enable");
                    if (health["url"]) {
                        const QUrl target(QString::fromStdString(
                            text(health["url"], path + ".health-check.url", 4096)));
                        require(target.isValid() &&
                                    oneOf(target.scheme().toStdString(), {"http", "https"}) &&
                                    !target.host().isEmpty() && target.userInfo().isEmpty() &&
                                    !target.hasFragment(),
                                path + ".health-check.url");
                    }
                    if (health["interval"])
                        number(health["interval"], path + ".health-check.interval", 1, 86400);
                    if (health["timeout"])
                        number(health["timeout"], path + ".health-check.timeout", 1, 60000);
                    if (health["lazy"])
                        boolean(health["lazy"], path + ".health-check.lazy");
                    if (health["expected-status"])
                        text(health["expected-status"], path + ".health-check.expected-status",
                             128);
                }
            }
        }
        for (std::size_t i = 0; i < proxies.size(); ++i) {
            const auto path = QStringLiteral("$.proxies[%1]").arg(i);
            const auto name = text(proxies[i]["name"], path + ".name", 512);
            require(!dynamicNames.contains(name), path + ".name");
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
                    require(!dynamicNames.contains(name), path + ".name");
                    require(names.insert(name).second, path + ".name");
                    const auto type = text(group["type"], path + ".type", 64);
                    if (!oneOf(type, {"select", "url-test", "fallback", "load-balance"})) {
                        issue("UnsupportedGroup", path + ".type",
                              type == "load-balance" ? type : "group");
                        return;
                    }
                    Keys fields{"name", "type", "proxies", "use"};
                    if (type != "select")
                        fields.insert({"url", "interval", "timeout", "lazy", "expected-status"});
                    if (type == "url-test")
                        fields.insert("tolerance");
                    if (type == "load-balance") {
                        fields.insert("strategy");
                        if (group["strategy"])
                            require(oneOf(text(group["strategy"], path + ".strategy"),
                                          {"consistent-hashing", "round-robin", "sticky-sessions"}),
                                    path + ".strategy");
                    }
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
                    require((!group["proxies"] || group["proxies"].IsSequence()) &&
                                group["proxies"].size() <= 5000 &&
                                (group["proxies"].size() > 0 || group["use"].size() > 0),
                            path + ".proxies");
                    if (group["use"]) {
                        require(group["use"].IsSequence() && group["use"].size() <= 32,
                                path + ".use");
                        Keys used;
                        for (const auto& ref : group["use"]) {
                            const auto provider = text(ref, path + ".use[]", 512);
                            require(proxyProviders.contains(provider) &&
                                        used.insert(provider).second,
                                    path + ".use[]");
                        }
                    }
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
        const auto ruleCount = separateRules ? separateRules->size() : root["rules"].size();
        require(root["rules"].IsSequence() && ruleCount <= 20000, "$.rules");
        for (std::size_t i = 0; i < ruleCount; ++i) {
            const auto path = QStringLiteral("$.rules[%1]").arg(i);
            attempt(
                [&] {
                    const auto value =
                        separateRules ? (*separateRules)[i] : text(root["rules"][i], path, 8192);
                    require(!value.empty() && value.size() <= 8192, path);
                    const auto parts = QString::fromStdString(value).split(',');
                    const auto type = parts.front().toStdString();
                    if (type == "RULE-SET" &&
                        (parts.size() < 2 || !ruleProviders.contains(parts[1].toStdString()))) {
                        issue("UnsupportedRule", path, "RULE-SET");
                        return;
                    }
                    if (!oneOf(type, {"DOMAIN", "DOMAIN-SUFFIX", "DOMAIN-KEYWORD", "IP-CIDR",
                                      "IP-CIDR6", "GEOIP", "GEOSITE", "RULE-SET", "MATCH"})) {
                        issue("UnsupportedRule", path,
                              oneOf(type, {"GEOIP", "GEOSITE", "RULE-SET"}) ? type : "rule");
                        return;
                    }
                    const bool match = type == "MATCH";
                    require(parts.size() == (match ? 2 : 3) ||
                                (!match && parts.size() == 4 &&
                                 (type == "IP-CIDR" || type == "IP-CIDR6" || type == "GEOIP" ||
                                  type == "RULE-SET") &&
                                 parts.back() == "no-resolve"),
                            path);
                    require(names.contains(parts[match ? 1 : 2].toStdString()), path);
                    if (type == "RULE-SET")
                        require(ruleProviders.contains(parts[1].toStdString()), path);
                },
                path);
        }
    }
};
} // namespace
domain::CommandResult validateConfigSchema(const YAML::Node& root,
                                           const std::vector<std::string>* rules) {
    Schema schema;
    domain::Error error;
    error.stage = QStringLiteral("preflight");
    try {
        schema.validate(root, rules);
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
