#include "application/ports.h"
#include "infra/config/config_metadata.h"
#include "infra/config/config_preflight.h"
#include "infra/config/geo_resources.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
#include <limits>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
QByteArray fixture(const char* name) {
    QFile file(QStringLiteral(QUICKCLASH_SOURCE_DIR "/tests/fixtures/mvp/") +
               QString::fromLatin1(name));
    check(file.open(QIODevice::ReadOnly), "fixture open");
    return file.readAll();
}
void expectError(const QByteArray& input, const char* code,
                 const std::source_location location = std::source_location::current()) {
    const auto result = quickclash::infra::preflightConfig(input, {});
    check(std::holds_alternative<quickclash::domain::Error>(result), "input must fail");
    const auto actual = std::get<quickclash::domain::Error>(result).code;
    if (actual != QString::fromLatin1(code))
        throw std::runtime_error("line " + std::to_string(location.line()) + ": expected " + code +
                                 ", got " + actual.toStdString());
}
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    using namespace quickclash::domain;
    if (app.arguments().contains(QStringLiteral("--geo-resource-probe"))) {
        QTemporaryDir destination;
        const auto result = quickclash::infra::prepareGeoResources(
            "dns: {enable: true}\nrules: []\n", destination.path());
        std::cout << (std::holds_alternative<Error>(result) ? "GeoResourceIntegrityFailed"
                                                            : "Verified");
        return std::holds_alternative<Error>(result) ? 1 : 0;
    }
    try {
        using quickclash::infra::subscriptionPayloadError;
        check(subscriptionPayloadError(fixture("inline_valid.yaml")).isEmpty(),
              "standard YAML subscription recognized independently of MIME");
        check(subscriptionPayloadError(QByteArray::fromHex("efbbbf") + fixture("inline_valid.yaml"))
                  .isEmpty(),
              "UTF-8 BOM subscription recognized");
        check(subscriptionPayloadError(" \n") == "EmptySubscription", "empty subscription");
        for (const auto& body : {"<html>Login required</html>", "c3M6Ly9wdWJsaWM=",
                                 "ss://public-fixture", "{\"error\":\"expired\"}"})
            check(subscriptionPayloadError(body) == "UnsupportedSubscriptionFormat",
                  "non-configuration content rejected clearly");
        check(subscriptionPayloadError("proxies: []\nproxies: []") == "DuplicateConfigKey",
              "format detection preserves duplicate-key rejection");
        const ConfigHistory history;
        QTemporaryDir geoRoot, geoDestination;
        QJsonObject invalidGeo{
            {QStringLiteral("geoip.dat"),
             QJsonObject{{QStringLiteral("sha256"), QString(64, QLatin1Char('a'))},
                         {QStringLiteral("sourceRef"), QStringLiteral("../escape")}}}};
        check(std::holds_alternative<Error>(quickclash::infra::prepareGeoResources(
                  "dns: {enable: true}\nrules: []\n", geoDestination.path(), invalidGeo,
                  geoRoot.path())),
              "Geo snapshot rejects paths outside the resource store");
        check(!history.activeRevision && !history.lastGoodRevision,
              "first run has no confirmed history");
        const RuntimePreferences preferences;
        const auto routingConfig = quickclash::infra::configurationSummary(
            "proxies: [{name: node, type: http}]\nproxy-groups:\n"
            "- {name: Z-primary, type: select, proxies: [A-auto]}\n"
            "- {name: A-auto, type: fallback, proxies: [node]}\nrules: [\"MATCH,Z-primary\"]\n");
        QJsonObject live;
        for (const auto& value : routingConfig.value("groups").toArray()) {
            const auto group = value.toObject();
            live.insert(
                group.value("name").toString(),
                QJsonObject{{"type", group.value("canSelect").toBool() ? "Selector" : "Fallback"},
                            {"now", group.value("current")},
                            {"all", group.value("members")}});
        }
        live["GLOBAL"] = QJsonObject{{"type", "Selector"}, {"now", "Z-primary"}};
        const auto ordered =
            quickclash::infra::orderedProxyGroups(routingConfig.value("groups").toArray(), live);
        check(ordered[1].toObject().value("name") == "Z-primary" &&
                  ordered[2].toObject().value("name") == "A-auto",
              "live groups retain subscription order rather than sorted object keys");
        const auto outbound = quickclash::infra::outboundSummary(
            ordered, routingConfig.value("defaultPolicy").toString(), "rule", true);
        check(outbound.value("policy") == "Z-primary" && outbound.value("node") == "node" &&
                  outbound.value("chain").toArray().size() == 3,
              "default MATCH follows nested live selections to the actual leaf");
        check(quickclash::infra::outboundSummary(ordered, "Z-primary", "global", true)
                          .value("node") == "node" &&
                  quickclash::infra::outboundSummary(ordered, "Z-primary", "direct", true)
                          .value("node") == "DIRECT" &&
                  quickclash::infra::outboundSummary(ordered, {}, "rule", false)
                      .value("policy")
                      .toString()
                      .isEmpty(),
              "global/direct/no-default do not invent a single rule outbound");
        live["A-auto"] = QJsonObject{{"type", "LoadBalance"}, {"now", "node"}};
        const auto balancing = quickclash::infra::outboundSummary(
            quickclash::infra::orderedProxyGroups(routingConfig.value("groups").toArray(), live),
            "Z-primary", "rule", true);
        check(balancing.value("balanced").toBool() && balancing.value("node").toString().isEmpty(),
              "load balancing does not claim every connection uses one node");
        check(!preferences.tunDesired && !preferences.systemProxyDesired, "safe defaults");
        check(formatBytes(std::numeric_limits<std::uint64_t>::max()) ==
                  QStringLiteral("18446744073709551615"),
              "64-bit precision");
        RequestContext request{QUuid::createUuid(), 7, 2};
        auto current = request;
        current.expectedRevision = 8;
        check(!request.matches(current), "late response after reload rejected");
        current = request;
        current.epoch = QUuid::createUuid();
        check(!request.matches(current), "old epoch rejected");
        current = request;
        ++current.generation;
        check(!request.matches(current), "old generation rejected");
        const SubscriptionContext source{QUuid::createUuid(), 1, QStringLiteral("hash")};
        auto replacement = source;
        ++replacement.sourceGeneration;
        check(!source.matches(replacement), "changed source rejected");
        const auto now = std::chrono::steady_clock::now();
        Operation operation(QStringLiteral("startProxy"), now + std::chrono::seconds(1));
        check(!operation.finish(OperationState::Succeeded), "no unconfirmed success");
        check(operation.begin() && !operation.begin(), "single start");
        check(!operation.expired(now) && operation.expired(now + std::chrono::seconds(2)),
              "monotonic deadline");
        Error uncertain;
        uncertain.code = QStringLiteral("ResponseLost");
        uncertain.category = ErrorCategory::Uncertain;
        check(operation.finish(OperationState::Uncertain, uncertain), "uncertain is explicit");
        check(operation.error()->operationId == operation.id(), "error correlation");
        check(!operation.finish(OperationState::Succeeded),
              "late completion cannot replace terminal result");
        Operation cancelled(QStringLiteral("download"), now);
        check(cancelled.finish(OperationState::Cancelled), "cancel pending");
        check(!cancelled.begin(), "cancelled operation cannot restart");
        const auto valid =
            quickclash::infra::preflightConfig(fixture("inline_valid.yaml"), preferences);
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(valid),
              "inline fixture accepted");
        const auto config = std::get<quickclash::infra::PreflightConfig>(valid);
        check(!config.semanticYaml.contains("secret:"), "semantic template has no secret");
        const auto session = quickclash::infra::materializeSession(config, QByteArray(64, 'a'));
        check(std::holds_alternative<QByteArray>(session) &&
                  std::get<QByteArray>(session).contains("secret:"),
              "session requires secret");
        check(std::holds_alternative<Error>(quickclash::infra::materializeSession(config, "weak")),
              "weak secret rejected");
        const auto flowInput = quickclash::infra::preflightConfig(
            "{proxies: [{name: node, type: http, server: example.invalid, port: 80}], "
            "proxy-groups: [{name: group, type: select, proxies: [node]}], rules: "
            "[\"MATCH,group\"]}",
            {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(flowInput),
              "flow root accepted");
        const auto flowSession = quickclash::infra::materializeSession(
            std::get<quickclash::infra::PreflightConfig>(flowInput),
            QByteArray(32, 'x') + "\nsecret: injected");
        check(std::holds_alternative<QByteArray>(flowSession) &&
                  std::holds_alternative<quickclash::infra::PreflightConfig>(
                      quickclash::infra::preflightConfig(std::get<QByteArray>(flowSession), {})),
              "canonical block root and quoted secret preserve one valid document");
        const auto legacySession = quickclash::infra::materializeSession(
            {"{proxies: [], proxy-groups: [], rules: ['MATCH,DIRECT']}", {}}, QByteArray(32, 'x'));
        check(std::holds_alternative<QByteArray>(legacySession) &&
                  std::holds_alternative<quickclash::infra::PreflightConfig>(
                      quickclash::infra::preflightConfig(std::get<QByteArray>(legacySession), {})),
              "legacy flow artifact still materializes after upgrade");
        const auto metadata = quickclash::infra::configMetadata(
            "proxies: []\nignored: {nested: [one, {two: three}]}\n"
            "dns: {enable: true}\nrules: ['GEOIP,CN,DIRECT', 'MATCH,DIRECT']\n");
        check(std::holds_alternative<quickclash::infra::ConfigMetadata>(metadata) &&
                  std::get<quickclash::infra::ConfigMetadata>(metadata).defaultPolicy ==
                      QStringLiteral("DIRECT") &&
                  std::get<quickclash::infra::ConfigMetadata>(metadata).geoRule &&
                  !std::get<quickclash::infra::ConfigMetadata>(metadata).fieldsYaml.contains(
                      "ignored"),
              "metadata skips irrelevant trees while keeping routing summary");
        check(std::holds_alternative<Error>(quickclash::infra::configMetadata(
                  "proxies: []\nignored: {nested: 1, nested: 2}\n")) &&
                  std::holds_alternative<Error>(
                      quickclash::infra::configMetadata("proxies: []\n---\nproxies: []\n")) &&
                  std::holds_alternative<Error>(quickclash::infra::configMetadata(
                      "proxies: []\nignored: &a [one]\nother: *a\n")),
              "metadata keeps duplicate, document and alias rejection in skipped fields");
        expectError(fixture("provider_unsupported.yaml"), "UnsupportedConfigFeature");
        const auto geo = quickclash::infra::preflightConfig(fixture("geo_supported.yaml"), {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(geo),
              "managed Geo rules accepted");
        const auto dns = quickclash::infra::preflightConfig(fixture("dns_supported.yaml"), {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(dns),
              "bounded fake-ip DNS accepted");
        const auto dnsYaml = std::get<quickclash::infra::PreflightConfig>(dns).semanticYaml;
        check(dnsYaml.contains("fake-ip") && dnsYaml.contains("enable: true") &&
                  !dnsYaml.contains("0.0.0.0:53") && dnsYaml.contains("geodata-mode: true"),
              "DNS semantics retained without external listener");
        check(quickclash::infra::geoResourceManifest(dnsYaml).value("resources").toArray().size() ==
                  2,
              "DNS pins both Geo snapshots");
        check(!quickclash::infra::redactedConfig(dnsYaml).contains("dns.example.invalid"),
              "resolver credentials masked in preview");
        const auto emptyFlow = quickclash::infra::preflightConfig(
            "proxies: [{name: fixture, type: vless, server: proxy.example.invalid, port: 443, "
            "uuid: 00000000-0000-4000-8000-000000000001, network: ws, flow: ''}]\n"
            "proxy-groups: []\nrules: ['MATCH,DIRECT']\n",
            {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(emptyFlow),
              "empty VLESS flow imposes no TCP TLS requirement");
        auto invalidDns = fixture("dns_supported.yaml");
        invalidDns.replace("https://dns.example.invalid/dns-query", "file:///private/resolver");
        expectError(invalidDns, "InvalidConfigField");
        for (const auto* resolver :
             {"https://127.0.0.1/dns-query", "tcp://[::ffff:127.0.0.1]:53", "udp://0.0.0.0:53"}) {
            auto loopback = fixture("dns_supported.yaml");
            loopback.replace("https://dns.example.invalid/dns-query",
                             QByteArray("'") + resolver + '\'');
            expectError(loopback, "InvalidConfigField");
        }
        invalidDns = fixture("dns_supported.yaml");
        invalidDns.replace("198.18.0.1/16", "10.0.0.1/8");
        expectError(invalidDns, "InvalidConfigField");
        expectError(fixture("invalid.yaml"), "InvalidYaml");
        expectError("proxies: []\nproxies: []", "DuplicateConfigKey");
        expectError("external-controller-pipe: arbitrary", "UnsupportedConfigFeature");
        expectError("proxies: &nodes []\nproxy-groups: *nodes", "UnsupportedConfigFeature");
        expectError(QByteArray(40, '[') + "0" + QByteArray(40, ']'), "ConfigLimitExceeded");
        expectError(QByteArray(16 * 1024 * 1024 + 1, ' '), "ConfigLimitExceeded");
        const auto mainstreamBytes = fixture("mainstream_valid.yaml");
        const auto mainstream = quickclash::infra::preflightConfig(mainstreamBytes, {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(mainstream),
              "mainstream protocols, transports and nested automatic groups accepted");
        const auto semantic = std::get<quickclash::infra::PreflightConfig>(mainstream).semanticYaml;
        const auto preview = quickclash::infra::redactedConfig(semantic);
        check(!preview.contains("public_dummy") && !preview.contains("AAAAAAAAAAAAAAAA") &&
                  !preview.contains("00000000-0000") && !preview.contains("/public-fixture") &&
                  !preview.contains("example.invalid"),
              "nested secrets and health URLs redacted");
        const QByteArray identityKey(32, 'k');
        auto changed = semantic;
        changed.replace("public_dummy_password", "changed_dummy_password");
        check(quickclash::infra::nodeIdentities(semantic, identityKey).value("hy2_fixture") !=
                  quickclash::infra::nodeIdentities(changed, identityKey).value("hy2_fixture"),
              "protocol credential change invalidates saved identity");
        changed = mainstreamBytes;
        changed.replace("port: 443", "port: 70000");
        expectError(changed, "InvalidConfigField");
        changed = mainstreamBytes;
        changed.replace("tls: true", "tls: [true]");
        expectError(changed, "InvalidConfigField");
        changed = mainstreamBytes;
        changed.replace("00000000-0000-4000-8000-000000000001", "not-a-uuid");
        expectError(changed, "InvalidConfigField");
        changed = mainstreamBytes;
        changed.replace("short-id: 0123456789abcdef", "short-id: xyz");
        expectError(changed, "InvalidConfigField");
        changed = mainstreamBytes;
        changed.replace("headers: {Host: proxy.example.invalid}",
                        "headers: {secret_credential: value}");
        expectError(changed, "UnsupportedConfigFeature");
        changed = mainstreamBytes;
        changed.replace("proxies: [DIRECT, REJECT]", "proxies: [primary_fixture]");
        expectError(changed, "InvalidConfigField");
        changed = mainstreamBytes;
        changed.replace("proxies: [DIRECT, REJECT]", "proxies: [missing_fixture]");
        expectError(changed, "InvalidConfigField");
        changed = mainstreamBytes;
        changed.replace("interval: 86400", "interval: 0");
        expectError(changed, "InvalidConfigField");
        const auto incompatible =
            quickclash::infra::preflightConfig(fixture("compatibility_unsupported.yaml"), {});
        check(std::holds_alternative<Error>(incompatible), "unsupported fixture rejected");
        const auto failure = std::get<Error>(incompatible);
        std::set<QString> categories;
        QString safeDetail;
        for (const auto& issue : failure.configIssues) {
            categories.insert(issue.code);
            safeDetail += issue.path + issue.feature + issue.phase;
        }
        for (const auto* kind :
             {"UnsupportedProtocol", "UnsupportedTransport", "UnsupportedGroup", "UnsupportedRule",
              "UnsupportedDns", "UnsupportedResource", "UnsupportedField"})
            check(categories.contains(QString::fromLatin1(kind)),
                  "all compatibility categories reported together");
        check(!safeDetail.contains("secret_") && !safeDetail.contains("private.") &&
                  !safeDetail.contains("token=") && failure.configIssues.size() <= 32,
              "diagnostics expose fixed vocabulary and structural indexes only");
        QByteArray manyIssues = "proxies: []\nproxy-groups: []\nrules: [\n";
        for (int index = 0; index < 100; ++index)
            manyIssues += "  'RULE-SET,fixture,DIRECT',\n";
        manyIssues += "]\n";
        const auto bounded = quickclash::infra::preflightConfig(manyIssues, {});
        check(std::holds_alternative<Error>(bounded) &&
                  std::get<Error>(bounded).configIssues.size() == 32,
              "compatibility summaries remain bounded");
        const QByteArray providerConfig =
            "proxy-providers: {remote: {type: http, url: 'https://example.invalid/proxies', path: "
            "'../escape'}}\n"
            "rule-providers: {domains: {type: http, url: 'https://example.invalid/rules', "
            "behavior: domain}}\n"
            "proxy-groups: [{name: Main, type: select, use: [remote]}]\n"
            "rules: ['RULE-SET,domains,Main', 'MATCH,DIRECT']\n";
        const auto requests = quickclash::infra::providerRequests(providerConfig);
        check(std::holds_alternative<QJsonArray>(requests) &&
                  std::get<QJsonArray>(requests).size() == 2,
              "managed providers discovered without accepting source paths");
        const auto expanded = quickclash::infra::expandProviders(
            providerConfig,
            {{QStringLiteral("proxy-providers:remote"),
              QByteArrayLiteral(
                  "proxies: [{name: remote_node, type: http, server: 127.0.0.1, port: 8080}]")},
             {QStringLiteral("rule-providers:domains"),
              QByteArrayLiteral("payload: ['+.example.invalid']")}});
        check(std::holds_alternative<QByteArray>(expanded), "provider snapshots expand");
        const auto expandedBytes = std::get<QByteArray>(expanded);
        check(!expandedBytes.contains("https://") && !expandedBytes.contains("../escape") &&
                  expandedBytes.contains("DOMAIN-SUFFIX,example.invalid,Main") &&
                  std::holds_alternative<quickclash::infra::PreflightConfig>(
                      quickclash::infra::preflightConfig(expandedBytes, {})),
              "expanded providers use safe inline policy and preserve rule target");
        auto insecureProvider = providerConfig;
        insecureProvider.replace("https://", "http://");
        check(std::holds_alternative<Error>(quickclash::infra::providerRequests(insecureProvider)),
              "insecure provider rejected");
        check(std::holds_alternative<Error>(quickclash::infra::expandProviders(providerConfig, {})),
              "missing provider cannot silently drop rules");
        std::cout << "PASS: domain defaults, stale contexts, operation transitions, config subset "
                     "and sessions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
