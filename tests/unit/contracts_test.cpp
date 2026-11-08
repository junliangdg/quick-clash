#include "application/ports.h"
#include "domain/connection_snapshot.h"
#include "domain/json_lookup.h"
#include "domain/log_buffer.h"
#include "domain/proxy_latency.h"
#include "domain/resource_snapshot.h"
#include "domain/rule_snapshot.h"
#include "domain/subscription_name.h"
#include "domain/subscription_usage.h"
#include "domain/support_report.h"
#include "infra/config/config_metadata.h"
#include "infra/config/config_overlay.h"
#include "infra/config/config_preflight.h"
#include "infra/config/geo_resources.h"
#include "infra/core/provider_command.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimeZone>
#include <algorithm>
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
        {
            // Exercise the client lookup with absent, prefix and Unicode names.
            const auto name = QStringLiteral("中文节点测速记录长名称");
            const QJsonObject values{{name, 42}};
            check(
                jsonValue(values, QString{}).isUndefined() &&
                    jsonValue(values, QStringLiteral("中文")).isUndefined() &&
                    jsonValue(values, QStringLiteral("中文节点测速记录长名称更长")).isUndefined() &&
                    jsonValue(values, name).toInt() == 42,
                "client lookup matches the complete Unicode node name");
            QJsonObject mixed{{QString{}, 0},
                              {QStringLiteral("é"), 1},
                              {QStringLiteral("🛰️节点"), 2},
                              {QStringLiteral("中"), 3}};
            for (int index = 0; index < 256; ++index)
                mixed.insert(QStringLiteral("node_%1_中文").arg(index), index + 10);
            const auto bytes = QJsonDocument(mixed).toJson();
            for (const auto& object : {mixed, QJsonDocument::fromJson(bytes).object()}) {
                for (auto entry = object.constBegin(); entry != object.constEnd(); ++entry)
                    check(jsonValue(object, entry.key()) == entry.value(),
                          "client lookup preserves every key across JSON roundtrip");
                for (const auto& absent : {QStringLiteral("n"), QStringLiteral("node_99"),
                                           QStringLiteral("中文"), QStringLiteral("🛰️")})
                    check(jsonValue(object, absent).isUndefined(),
                          "prefix names cannot borrow another node's value");
            }
            check(jsonValue({}, name).isUndefined(), "empty snapshot has no node value");
            ProxyLatency latency;
            const auto now = QDateTime::fromString("2026-11-05T00:00:00Z", Qt::ISODate);
            const QStringList names{name, QStringLiteral("中文"),
                                    QStringLiteral("中文节点测速记录长名称更长")};
            for (int index = 0; index < names.size(); ++index)
                latency.manual(names[index], "https://fixture.invalid/", "Success",
                               now.addSecs(index), 40 + index);
            const auto details = latency.details(now.addSecs(3));
            for (int index = 0; index < names.size(); ++index)
                check(jsonValue(details, names[index])
                              .toObject()
                              .value("latest")
                              .toObject()
                              .value("delayMs") == 40 + index,
                      "latency records keep short and long Unicode node identities distinct");
        }
        {
            QDirIterator files(QStringLiteral(QUICKCLASH_SOURCE_DIR "/qml"), {"*.qml"}, QDir::Files,
                               QDirIterator::Subdirectories);
            const QRegularExpression bareScroll(QStringLiteral("\\bScrollView\\s*\\{"));
            int scanned = 0, safeScrolls = 0;
            while (files.hasNext()) {
                const auto path = files.next();
                QFile file(path);
                check(file.open(QIODevice::ReadOnly), "QML layout source readable");
                const auto source = QString::fromUtf8(file.readAll());
                if (!path.endsWith("/themed_scroll_view.qml"))
                    check(!bareScroll.match(source).hasMatch(),
                          "forms must use ThemedScrollView, not a clipped bare ScrollView");
                safeScrolls += source.count(QStringLiteral("ThemedScrollView {"));
                ++scanned;
            }
            check(scanned >= 40 && safeScrolls >= 13, "layout guard scans real application forms");
        }
        {
            ProxyLatency latency;
            const auto now = QDateTime::fromString("2026-11-01T00:00:00Z", Qt::ISODate);
            const auto history = [&](int delay, int offset) {
                return QJsonArray{QJsonObject{
                    {"delay", delay}, {"time", now.addSecs(offset).toString(Qt::ISODateWithMs)}}};
            };
            const auto objects = [&](int delay, int offset) {
                return QJsonObject{
                    {"node",
                     QJsonObject{
                         {"extra", QJsonObject{{"https://private.invalid/?secret=x",
                                                QJsonObject{{"history", history(delay, offset)}}},
                                               {"https://other.invalid/",
                                                QJsonObject{{"history", history(0, -2)}}}}}}}};
            };
            const auto latest = [&] {
                return latency.details(now).value("node").toObject().value("latest").toObject();
            };
            check(latency.ingest(objects(41, -1), now),
                  "first core observations change the cached latency data");
            const auto originalDetails = latency.details(now);
            check(!latency.ingest(objects(41, -1), now) && latency.details(now) == originalDetails,
                  "identical core histories leave the latency projection unchanged");
            check(latest().value("delayMs") == 41 && latest().value("source") == "Core" &&
                      latency.details(now)
                              .value("node")
                              .toObject()
                              .value("records")
                              .toArray()
                              .size() == 2,
                  "core latency retains separate targets and latest timestamp");
            check(!QJsonDocument(latency.details(now)).toJson().contains("secret=x"),
                  "probe targets do not leak into shared data");
            check(!latency.ingest(objects(12, -10), now),
                  "older core histories do not invalidate the cached projection");
            check(latest().value("delayMs") == 41,
                  "older core response cannot overwrite newer observation");
            latency.manual("node", "https://other.invalid/", "Testing", now);
            check(latency.ingest(objects(21, 1), now.addSecs(1)),
                  "a newer core observation invalidates the cached projection");
            check(latest().value("state") == "Testing", "refresh preserves active manual test");
            latency.manual("node", "https://other.invalid/", "Cancelled", now.addSecs(2));
            auto cancelled = objects(21, 1);
            auto node = cancelled.value("node").toObject();
            node.insert("extra", QJsonObject{{"https://other.invalid/",
                                              QJsonObject{{"history", history(9, 3)}}}});
            cancelled.insert("node", node);
            latency.ingest(cancelled, now.addSecs(3));
            check(latest().value("state") == "Cancelled",
                  "late controller probe cannot undo cancellation");
            latency.manual("node", "https://manual.invalid/", "Success", now.addSecs(4), 88);
            latency.invalidateManual();
            check(latest().value("state") == "Stale",
                  "manual target change invalidates manual result");
            check(latency.details(now.addSecs(700))
                          .value("node")
                          .toObject()
                          .value("latest")
                          .toObject()
                          .value("state") == "Stale",
                  "results expire by observation time rather than refresh time");
            latency.clear();
            latency.ingest({{"node", QJsonObject{{"history", history(0, 0)}}}}, now);
            check(latest().value("state") == "Failed" && ProxyLatency::text(latest()) != "0 ms",
                  "zero in core history denotes failure");
            // A real core observation may be a few seconds newer than an injected clock.
            latency.clear();
            latency.ingest({{"node", QJsonObject{{"history", history(34, 4)}}}}, now);
            check(latency.details(now.addSecs(603))
                          .value("node")
                          .toObject()
                          .value("latest")
                          .toObject()
                          .value("state") == "Success",
                  "latency stays fresh until ten minutes after its observation timestamp");
            check(latency.details(now.addSecs(604))
                          .value("node")
                          .toObject()
                          .value("latest")
                          .toObject()
                          .value("state") == "Stale",
                  "latency expires exactly ten minutes after its observation timestamp");
            latency.clear();
            latency.ingest({{"node", QJsonObject{{"history", history(34, 200)}}}}, now);
            check(latency.details(now).isEmpty(), "future observations are rejected");
            for (int i = 0; i < 20; ++i)
                latency.manual("node", QString("https://target%1.invalid/").arg(i), "Success",
                               now.addSecs(i), i + 1);
            check(latency.details(now.addSecs(20))
                          .value("node")
                          .toObject()
                          .value("records")
                          .toArray()
                          .size() == 8,
                  "per-node target cache is bounded");
            check(latency.ingest({}, now) && latency.details(now).isEmpty() &&
                      !latency.ingest({}, now),
                  "removed nodes release and invalidate their observations only once");
            const auto record = [&](const QString& target) {
                const auto records =
                    latency.details(now).value("node").toObject().value("records").toArray();
                for (const auto& value : records)
                    if (value.toObject().value("targetHash") == ProxyLatency::targetHash(target))
                        return value.toObject();
                return QJsonObject{};
            };
            latency.manual("node", "https://one.invalid/", "Testing", now);
            latency.manual("node", "https://two.invalid/", "Queued", now);
            check(record("https://one.invalid/").value("state") == "Testing" &&
                      record("https://two.invalid/").value("state") == "Queued",
                  "same node exposes independent in-flight states for each target");
            latency.ingest({}, now);
            check(record("https://one.invalid/").value("state") == "Testing" &&
                      record("https://two.invalid/").value("state") == "Queued",
                  "a missing node in a concurrent refresh cannot erase pending tests");
            latency.manual("node", "https://one.invalid/", "Cancelled", now.addSecs(1));
            latency.manual("node", "https://two.invalid/", "Success", now.addSecs(2), 62);
            latency.ingest(
                {{"node",
                  QJsonObject{{"extra", QJsonObject{{"https://one.invalid/",
                                                     QJsonObject{{"history", history(9, 3)}}}}}}}},
                now.addSecs(3));
            check(record("https://one.invalid/").value("state") == "Cancelled" &&
                      record("https://two.invalid/").value("delayMs") == 62,
                  "cancelled target stays fenced while another target completes");
            latency.invalidateManual("https://two.invalid/");
            check(record("https://one.invalid/").value("state") == "Cancelled" &&
                      record("https://two.invalid/").value("state") == "Stale",
                  "changing global target invalidates only its manual observations");
            latency.manual("node", "https://one.invalid/", "Success", now.addSecs(4), 38);
            latency.invalidate("node", "https://one.invalid/");
            latency.manual("node", "https://one.invalid/", "Queued", now.addSecs(5));
            latency.manual("node", "https://one.invalid/", "Cancelled", now.addSecs(6));
            check(record("https://one.invalid/").value("state") == "Cancelled" &&
                      record("https://two.invalid/").value("state") == "Stale",
                  "cancelling a network-restarted batch cannot revive the old target result");
            latency.fenceCoreHistory("node", "https://one.invalid/", now.addSecs(10));
            latency.manual("node", "https://one.invalid/", "Success", now.addSecs(7), 41);
            latency.ingest(
                {{"node",
                  QJsonObject{{"extra", QJsonObject{{"https://one.invalid/",
                                                     QJsonObject{{"history", history(900, 9)}}},
                                                    {"https://two.invalid/",
                                                     QJsonObject{{"history", history(63, 9)}}}}}}}},
                now.addSecs(10));
            check(record("https://one.invalid/").value("delayMs") == 41 &&
                      record("https://two.invalid/").value("delayMs") == 63,
                  "late old history cannot replace a retry or block another target");
            latency.ingest(
                {{"node", QJsonObject{{"extra",
                                       QJsonObject{{"https://one.invalid/",
                                                    QJsonObject{{"history", history(42, 11)}}}}}}}},
                now.addSecs(11));
            check(record("https://one.invalid/").value("delayMs") == 42,
                  "core observations after the fence can update normally");
            check(latency.ingest({}, now.addSecs(4)) && latency.details(now).isEmpty(),
                  "missing nodes are released after all pending tests finish or cancel");
            latency.clear();
            for (int i = 0; i < 20; ++i)
                latency.manual("node", QString("https://cancel%1.invalid/").arg(i), "Cancelled",
                               now.addSecs(i));
            check(latency.details(now).value("node").toObject().value("records").toArray().size() ==
                      8,
                  "cancelled target records also obey the eight-target bound");
        }
        {
            const QString secret =
                "private-marker https://user:password@internal.test/path?token=123\nAuthorization: "
                "Bearer abc C:/Users/private";
            const QJsonObject hostile{
                {"status", secret},
                {"error", secret},
                {"platform", secret},
                {"architecture", secret},
                {"dataDirectory", secret},
                {"profiles", QJsonArray{QJsonObject{{"name", secret}, {"secretRef", secret}}}},
                {"errors", QJsonArray{QJsonObject{{"code", secret}, {"message", secret}}}},
                {"logs", QJsonArray{QJsonObject{{"source", secret},
                                                {"level", secret},
                                                {"time", secret},
                                                {"message", secret},
                                                {"unknown", secret}}}},
                {"features", QJsonObject{{"memory", QJsonObject{{"path", secret}}},
                                         {"unknown", secret},
                                         {"ipInfo", QJsonObject{{"ip", "203.0.113.24"},
                                                                {"organization", "Private Org"},
                                                                {"interfaces", secret}}}}},
                {"settingsState", QJsonObject{{"saved", QJsonObject{{"mode", secret},
                                                                    {"dnsMode", secret},
                                                                    {"proxy", secret}}}}}};
            const auto report = supportReport(hostile);
            const auto encoded = QJsonDocument(report).toJson();
            for (const auto* forbidden :
                 {"private-marker", "internal.test", "password", "Authorization", "Bearer",
                  "C:/Users", "secretRef", "203.0.113.24", "Private Org", "ipInfo"})
                check(!encoded.contains(forbidden),
                      "support report excludes malicious arbitrary fields");
            check(report.value("status") == "Unknown" && report.value("profileCount") == 1 &&
                      report.value("recentErrors").toArray().first() == "Unknown",
                  "support projection keeps bounded facts");
        }
        {
            const auto source = fixture("inline_valid.yaml");
            const auto overlay = QJsonObject{
                {"set",
                 QJsonObject{{"log-level", "debug"}, {"dns", QJsonObject{{"enable", false}}}}},
                {"rules", QJsonObject{{"mode", "prepend"},
                                      {"items", QJsonArray{"DOMAIN,extra.invalid,DIRECT"}}}}};
            const auto composed = quickclash::infra::composeOverlay(source, overlay);
            check(std::holds_alternative<QByteArray>(composed), "bounded overlay composes");
            const auto checked =
                quickclash::infra::preflightConfig(std::get<QByteArray>(composed), {});
            check(std::holds_alternative<quickclash::infra::PreflightConfig>(checked),
                  "overlay remains valid policy YAML");
            const auto semantic =
                std::get<quickclash::infra::PreflightConfig>(checked).semanticYaml;
            check(semantic.contains("debug") &&
                      semantic.indexOf("DOMAIN,extra.invalid") <
                          semantic.indexOf("DOMAIN-SUFFIX,example.invalid"),
                  "scalar merge and prepend preserve rule ordering");
            for (const auto& invalid :
                 {QJsonObject{{"set", QJsonObject{{"tun", QJsonObject{{"enable", true}}}}}},
                  QJsonObject{{"set", QJsonObject{{"dns", QJsonValue::Null}}}},
                  QJsonObject{{"set", QJsonObject{{"find-process-mode", "off"}}}},
                  QJsonObject{{"remove", QJsonArray{"secret"}}},
                  QJsonObject{{"remove", QJsonArray{"find-process-mode"}}},
                  QJsonObject{{"rules", QJsonObject{{"mode", "guess"}, {"items", QJsonArray{}}}}},
                  QJsonObject{{"arbitrary", true}}})
                check(std::holds_alternative<Error>(
                          quickclash::infra::composeOverlay(source, invalid)),
                      "reserved, null and unknown overlay operations reject");
            check(std::get<QByteArray>(quickclash::infra::composeOverlay(source, {})) == source,
                  "no overlay preserves source bytes including comments");
        }
        {
            const QByteArray source("proxies: []\nproxy-groups:\n"
                                    "  - {name: A, type: select, proxies: [DIRECT]}\n"
                                    "rule-providers:\n  sites: {type: inline, behavior: domain, "
                                    "payload: ['example.com']}\n"
                                    "rules:\n  - MATCH,DIRECT\n");
            const auto rule = [](const char* id, const char* type, const char* value,
                                 const char* target, bool enabled = true) {
                return QJsonObject{{"id", id},
                                   {"type", type},
                                   {"value", value},
                                   {"target", target},
                                   {"enabled", enabled}};
            };
            const QJsonObject book{
                {"global", QJsonArray{rule("g", "DOMAIN-SUFFIX", "example.com", "PROXY"),
                                      rule("disabled", "GEOIP", "CN", "DIRECT", false)}},
                {"profiles",
                 QJsonObject{
                     {"first",
                      QJsonObject{
                          {"proxy", "A"},
                          {"rules", QJsonArray{rule("l", "DOMAIN", "example.com", "DIRECT"),
                                               rule("set", "RULE-SET", "sites", "REJECT")}}}}}}};
            const auto composed = quickclash::infra::composeUserRules(source, book, "first");
            check(std::holds_alternative<QByteArray>(composed), "user rules compose");
            const auto yaml = std::get<QByteArray>(composed);
            check(yaml.indexOf("DOMAIN,example.com,DIRECT") <
                          yaml.indexOf("DOMAIN-SUFFIX,example.com,A") &&
                      yaml.indexOf("DOMAIN-SUFFIX,example.com,A") < yaml.indexOf("MATCH,DIRECT") &&
                      yaml.contains("RULE-SET,sites,REJECT") && !yaml.contains("GEOIP,CN,DIRECT"),
                  "local precedes global and base; disabled omitted");
            const auto missing = quickclash::infra::composeUserRules(source, book, "other");
            check(std::holds_alternative<Error>(missing) &&
                      std::get<Error>(missing).code == "UserRuleProxyMissing",
                  "missing profile proxy does not silently route direct");
            auto bad = book;
            bad.insert("global", QJsonArray{rule("g", "IP-CIDR", "not-a-network", "DIRECT")});
            check(std::holds_alternative<Error>(
                      quickclash::infra::composeUserRules(source, bad, "first")),
                  "invalid CIDR rejected");
            const QJsonArray common{rule("d", "DOMAIN", "example.com", "DIRECT"),
                                    rule("s", "DOMAIN-SUFFIX", "example.com", "REJECT"),
                                    rule("k", "DOMAIN-KEYWORD", "example", "DIRECT"),
                                    rule("v4", "IP-CIDR", "192.0.2.0/24", "DIRECT"),
                                    rule("v6", "IP-CIDR6", "2001:db8::/32", "DIRECT"),
                                    rule("geoip", "GEOIP", "CN", "DIRECT"),
                                    rule("geosite", "GEOSITE", "CN", "DIRECT")};
            check(std::holds_alternative<QByteArray>(
                      quickclash::infra::composeUserRules(source, {{"global", common}}, "first")),
                  "all common global rule types compose");
            const auto missingProvider = quickclash::infra::composeUserRules(
                source,
                {{"profiles",
                  QJsonObject{
                      {"first", QJsonObject{{"rules", QJsonArray{rule("x", "RULE-SET", "missing",
                                                                      "DIRECT")}}}}}}},
                "first");
            check(std::holds_alternative<Error>(missingProvider) &&
                      std::get<Error>(missingProvider).code == "UserRuleProviderMissing",
                  "missing local provider rejected");
            check(std::holds_alternative<Error>(quickclash::infra::composeUserRules(
                      source, {{"global", QJsonArray{rule("x", "RULE-SET", "sites", "DIRECT")}}},
                      "first")),
                  "global rules cannot refer to a profile provider");
            const auto missingGroup = quickclash::infra::composeUserRules(
                source,
                {{"profiles",
                  QJsonObject{
                      {"first", QJsonObject{{"rules", QJsonArray{rule("x", "DOMAIN", "example.com",
                                                                      "absent")}}}}}}},
                "first");
            check(std::holds_alternative<Error>(missingGroup) &&
                      std::get<Error>(missingGroup).code == "UserRuleTargetMissing",
                  "missing local group rejected");
            check(std::get<QByteArray>(quickclash::infra::composeUserRules(source, {}, "first")) ==
                      source,
                  "empty user rules preserve source bytes");
        }
        {
            const auto counters = [](quint64 value) {
                return QJsonObject{{"privateWorkingSet", QString::number(value)},
                                   {"workingSet", QString::number(value + 1)},
                                   {"privateCommit", QString::number(value + 2)}};
            };
            const auto host = counters(100), service = counters(200), core = counters(1000);
            const auto snapshot = quickclash::domain::resourceSnapshot(
                host, {{"service", service}, {"complete", true}}, core, true, counters(3000), true);
            const auto memory = snapshot.value("memory").toObject();
            check(snapshot.value("memoryComplete").toBool() &&
                      memory.value("client").toObject().value("workingSet") == "302" &&
                      memory.value("core").toObject().value("workingSet") == "1001" &&
                      memory.value("validator").toObject().value("workingSet") == "3001",
                  "client includes service; core and temporary validator remain separate");
            const auto partial =
                quickclash::domain::resourceSnapshot(host, {}, {}, true, {}, false);
            check(!partial.value("memoryComplete").toBool() &&
                      partial.value("memory").toObject().value("client").toObject() == host &&
                      !partial.value("memory").toObject().contains("core"),
                  "missing service and core retain known host without inventing zeros");
            auto bad = counters(1);
            bad.insert("workingSet", "-1");
            const auto rejected = quickclash::domain::resourceSnapshot(
                host, {{"service", bad}, {"complete", true}}, core, true, {}, false);
            const auto rejectedMemory = rejected.value("memory").toObject();
            check(!rejected.value("memoryComplete").toBool() &&
                      !rejectedMemory.value("service").toObject().contains("workingSet") &&
                      rejectedMemory.value("client").toObject().value("workingSet") == "101" &&
                      rejectedMemory.value("client").toObject().value("privateCommit") == "105",
                  "invalid resource counter is omitted while other known counters remain");
            auto partialHost = host;
            partialHost.remove("privateWorkingSet");
            auto partialService = service;
            partialService.remove("privateCommit");
            auto partialCore = core;
            partialCore.remove("privateWorkingSet");
            const auto partialMetrics = quickclash::domain::resourceSnapshot(
                partialHost, {{"service", partialService}, {"complete", true}}, partialCore, true,
                {}, false);
            const auto knownMemory = partialMetrics.value("memory").toObject();
            const auto knownClient = knownMemory.value("client").toObject();
            check(!partialMetrics.value("memoryComplete").toBool() &&
                      !knownClient.contains("privateWorkingSet") &&
                      knownClient.value("workingSet") == "302" &&
                      knownClient.value("privateCommit") == "102" &&
                      knownMemory.value("service").toObject().value("privateWorkingSet") == "200" &&
                      knownMemory.value("core").toObject().value("workingSet") == "1001" &&
                      !knownMemory.value("core").toObject().contains("privateWorkingSet"),
                  "missing metrics retain known process values and partial client totals");
            auto overflowingHost = host;
            overflowingHost.insert("workingSet", "18446744073709551615");
            const auto overflow = quickclash::domain::resourceSnapshot(
                overflowingHost, {{"service", service}, {"complete", true}}, core, true, {}, false,
                counters(1), true);
            const auto overflowClient =
                overflow.value("memory").toObject().value("client").toObject();
            check(!overflow.value("memoryComplete").toBool() &&
                      !overflowClient.contains("workingSet") &&
                      overflowClient.value("privateWorkingSet") == "301" &&
                      overflowClient.value("privateCommit") == "307",
                  "overflowed client metric stays unavailable while other totals continue");
            const auto absentHost = quickclash::domain::resourceSnapshot(
                {}, {{"service", service}, {"complete", true}}, core, true, {}, false);
            check(!absentHost.value("memoryComplete").toBool() &&
                      !absentHost.value("memory").toObject().contains("client") &&
                      absentHost.value("memory").toObject().value("service").toObject() == service,
                  "known helper remains visible when the host cannot be sampled");
            const auto missingValidator = quickclash::domain::resourceSnapshot(
                host, {{"complete", true}, {"validatorExpected", true}}, {}, false, {}, false);
            check(!missingValidator.value("memoryComplete").toBool(),
                  "missing expected service validator makes the resource sample incomplete");
            const auto extracting = quickclash::domain::resourceSnapshot(
                host, {{"service", service}, {"complete", true}}, core, true, {}, false,
                counters(500), true);
            check(extracting.value("memoryComplete").toBool() &&
                      extracting.value("memory").toObject().value("client").toObject().value(
                          "workingSet") == "803" &&
                      extracting.value("memory")
                              .toObject()
                              .value("coreExtractor")
                              .toObject()
                              .value("workingSet") == "501",
                  "required extractor contributes to client while core stays separate");
            check(!quickclash::domain::resourceSnapshot(host, {{"complete", true}}, {}, false, {},
                                                        false, {}, true)
                       .value("memoryComplete")
                       .toBool(),
                  "missing expected extractor makes the sample incomplete");
            const auto relaying = quickclash::domain::resourceSnapshot(
                host, {{"service", service}, {"complete", true}}, core, true, {}, false, {}, false,
                counters(50), true);
            check(relaying.value("memoryComplete").toBool() &&
                      relaying.value("memory").toObject().value("client").toObject().value(
                          "workingSet") == "353" &&
                      relaying.value("memory")
                              .toObject()
                              .value("networkRelay")
                              .toObject()
                              .value("workingSet") == "51",
                  "network transport contributes to the client total while core stays separate");
            check(!quickclash::domain::resourceSnapshot(host, {{"complete", true}}, {}, false, {},
                                                        false, {}, false, {}, true)
                       .value("memoryComplete")
                       .toBool(),
                  "missing expected network transport makes the sample incomplete");
        }
        {
            const auto safe =
                redactedLogEntry(1, "time", "Core", "Warn",
                                 "https://user:LEAK_URL@example.test/path?token=LEAK_QUERY "
                                 "Authorization: Bearer LEAK_BEARER\n{\"password\":\"LEAK_JSON\"} "
                                 "token LEAK_SPACE; private-key='LEAK_PRIVATE'; uuid=LEAK_UUID");
            const auto bytes = QJsonDocument(safe).toJson();
            check(!bytes.contains("LEAK"), "log credential variants are redacted before storage");
            check(safe.value("level") == "warning" && safe.value("originalLevel") == "Warn",
                  "log level normalization preserves safe original value");
            const auto tail =
                redactedLogEntry(2, "time", "Core", "error",
                                 QString(4096, 'x') + "\ntail_marker password=LEAK_TAIL", true);
            check(tail.value("message").toString().size() <= 2048 &&
                      tail.value("message").toString().contains("tail_marker") &&
                      !QJsonDocument(tail).toJson().contains("LEAK_TAIL"),
                  "exit diagnostics keep the final output after redacting the whole tail");
            LogBuffer countRing(3, 4096);
            for (quint64 i = 0; i < 10; ++i)
                countRing.append(redactedLogEntry(i, "time", "App", "info", "small"));
            check(countRing.entries().size() == 3 &&
                      countRing.entries().first().toObject().value("localSeq") == "7" &&
                      countRing.status().value("droppedLogs") == "7",
                  "log count budget drops oldest entries");
            LogBuffer appRing(100, 512 * 1024);
            for (quint64 i = 0; i < 110; ++i)
                appRing.append(redactedLogEntry(i, "time", "App", "info", "small"));
            check(appRing.entries().size() == 100 && appRing.status().value("droppedLogs") == "10",
                  "application log budget retains the latest 100 entries");
            LogBuffer byteRing(2000, 4 * 1024 * 1024);
            for (quint64 i = 0; i < 2100; ++i)
                byteRing.append(
                    redactedLogEntry(i, "time", "Core", "debug", QString(3000, QChar('\n'))));
            check(byteRing.entries().size() < 2000 &&
                      QJsonDocument(byteRing.entries()).toJson(QJsonDocument::Compact).size() <=
                          4 * 1024 * 1024 &&
                      byteRing.status().value("logBytes").toString().toLongLong() <=
                          4 * 1024 * 1024 &&
                      byteRing.status().value("truncatedLogs") == "2100",
                  "escaped UTF-8 bytes bound log ring and truncation remains visible");
            byteRing.markGap();
            byteRing.addDropped(3);
            check(byteRing.status().value("logGaps") == "1", "paused receive gap recorded");
            byteRing.clear();
            check(byteRing.entries().isEmpty() && byteRing.status().value("logBytes") == "2" &&
                      byteRing.status().value("logGaps") == "0",
                  "log release clears budget and counters");
        }
        {
            QJsonArray entries;
            for (int i = 0; i < 1000; ++i)
                entries.append(QJsonObject{{"id", QString::number(i)},
                                           {"metadata", QJsonObject{{"host", "example.test"}}},
                                           {"upload", 100},
                                           {"download", 200}});
            const auto first = connectionSnapshot({{"connections", entries}}, {}, "one", 1000);
            check(std::holds_alternative<QJsonArray>(first), "1000 connection snapshot accepted");
            auto changed = entries[0].toObject();
            changed.insert("download", 3200);
            entries[0] = changed;
            const auto next = std::get<QJsonArray>(connectionSnapshot(
                {{"connections", entries}}, std::get<QJsonArray>(first), "one", 1500));
            check(next[0].toObject().value("downloadRate").toDouble() == 2000 &&
                      !std::get<QJsonArray>(first)[0].toObject().contains("downloadRate"),
                  "rates use elapsed monotonic time, first rate unknown");
            check(!std::get<QJsonArray>(
                       connectionSnapshot({{"connections", entries}}, next, "two", 1000))[0]
                       .toObject()
                       .contains("downloadRate"),
                  "old epoch never produces rates");
            changed.insert("download", 1);
            entries[0] = changed;
            check(!std::get<QJsonArray>(
                       connectionSnapshot({{"connections", entries}}, next, "one", 1000))[0]
                       .toObject()
                       .contains("downloadRate"),
                  "counter reset stays unknown");
            check(!std::get<QJsonArray>(
                       connectionSnapshot({{"connections", entries}}, next, "one", 10001))[0]
                       .toObject()
                       .contains("downloadRate"),
                  "stale interval stays unknown");
            entries.append(entries[0]);
            check(std::holds_alternative<Error>(
                      connectionSnapshot({{"connections", entries}}, {}, "one", 0)),
                  "duplicate ids reject entire snapshot");
            check(std::holds_alternative<Error>(connectionSnapshot({}, {}, "one", 0)),
                  "missing collection is not an empty snapshot");
            check(std::get<QJsonArray>(
                      connectionSnapshot({{"connections", QJsonValue::Null}}, {}, "one", 0))
                      .isEmpty(),
                  "official nil slice is an empty complete snapshot");
        }
        {
            const auto now = QDateTime::fromSecsSinceEpoch(1800000000, QTimeZone::UTC);
            const auto usage =
                parseSubscriptionUsage("upload=10; download=80; total=100; expire=1800086400");
            const auto status = subscriptionUsageStatus(usage, now);
            check(status.value("used") == "90" && status.value("quotaState") == "Low" &&
                      status.value("expiryState") == "Soon",
                  "subscription warning thresholds");
            check(subscriptionUsageStatus(parseSubscriptionUsage("download=90; total=100"), now)
                          .value("quotaState") == "Unknown",
                  "missing upload is unknown, never a zero counter");
            check(parseSubscriptionUsage("upload=1; upload=2; download=-1; total=+12; expire=1e9")
                      .isEmpty(),
                  "ambiguous, signed and exponent fields are rejected");
            check(parseSubscriptionUsage(
                      "upload=9007199254740992; download=18446744073709551616; expire=253402300800")
                      .isEmpty(),
                  "unsafe integer, overflow and unrepresentable date rejected");
            check(parseSubscriptionUsage(QByteArray(1025, '1')).isEmpty(), "usage header limit");
            using quickclash::domain::subscriptionName;
            check(subscriptionName("base64:5py65Zy6", "attachment; filename=fallback.yaml") ==
                      QString::fromUtf8("机场"),
                  "base64 title takes precedence");
            check(subscriptionName("  Plain title  ", {}) == "Plain title", "plain title");
            check(subscriptionName({}, "attachment; filename=old.yaml; "
                                       "filename*=UTF-8'zh'%E6%9C%BA%E5%9C%BA.yml") ==
                      QString::fromUtf8("机场"),
                  "extended UTF-8 filename preferred");
            check(subscriptionName("base64:!", "attachment; filename=\"Team; West.yaml\"") ==
                      "Team; West",
                  "invalid base64 falls back to quoted filename");
            check(subscriptionName({}, "attachment; filename=fallback.yml; "
                                       "filename*=UTF-8''bad%XY") == "fallback",
                  "invalid percent encoding falls back");
            check(subscriptionName({}, "attachment; filename=../Team.YAML") == "Team",
                  "filename has no path or YAML suffix");
            check(subscriptionName("base64:/w==", {}).isEmpty() &&
                      subscriptionName({}, "attachment; filename*=UTF-8''%FF").isEmpty(),
                  "invalid UTF-8 ignored");
            check(subscriptionName(QByteArray(4097, 'a'), QByteArray(4097, 'b')).isEmpty() &&
                      subscriptionName(QByteArray(90, 'x'), {}).size() == 80 &&
                      subscriptionName("\tTitle\r\n", {}) == "Title",
                  "name size and control characters bounded");
            const auto exhausted = subscriptionUsageStatus(
                parseSubscriptionUsage("upload = 10; download=90; total=100; expire=1800000000"),
                now);
            check(exhausted.value("quotaState") == "Exhausted" &&
                      exhausted.value("expiryState") == "Expired",
                  "exact quota and expiry boundaries");
            check(subscriptionUsageStatus(
                      parseSubscriptionUsage("upload=0;download=0;total=0;expire=0"), now)
                          .value("quotaState") == "Unknown",
                  "zero quota is not unlimited");
            check(subscriptionUsageStatus({{"upload", "10;download=80;total=100"}}, now)
                          .value("quotaState") == "Unknown",
                  "saved fields cannot inject metadata");
            const auto threshold =
                parseSubscriptionUsage("upload=0;download=89;total=100;expire=1800604800");
            check(subscriptionUsageStatus(threshold, now).value("expiryState") == "Soon" &&
                      subscriptionUsageStatus(threshold, now.addSecs(-1)).value("expiryState") ==
                          "Available" &&
                      subscriptionUsageStatus(threshold, now).value("quotaState") == "Available",
                  "seven-day boundary and below quota threshold");
        }
        using quickclash::infra::subscriptionPayloadError;
        check(subscriptionPayloadError(fixture("inline_valid.yaml")).isEmpty(),
              "standard YAML subscription recognized independently of MIME");
        check(subscriptionPayloadError(QByteArray::fromHex("efbbbf") + fixture("inline_valid.yaml"))
                  .isEmpty(),
              "UTF-8 BOM subscription recognized");
        check(subscriptionPayloadError(" \n") == "EmptySubscription", "empty subscription");
        for (const auto& body : {"<html>Login required</html>", "{\"error\":\"expired\"}"})
            check(subscriptionPayloadError(body) == "UnsupportedSubscriptionFormat",
                  "non-configuration content rejected clearly");
        check(subscriptionPayloadError("ss://public-fixture").isEmpty() &&
                  subscriptionPayloadError("c3M6Ly9wdWJsaWM=").isEmpty(),
              "URI and Base64 node subscriptions reach the official provider parser");
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
        QJsonObject snapshot;
        for (const auto* name : {"geoip.dat", "geosite.dat"}) {
            const QByteArray bytes = QByteArray("updated-") + name;
            const auto hash = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            const auto ref = QStringLiteral("data/geo/%1/%2").arg(hash, QLatin1String(name));
            const auto path = QDir(geoRoot.path()).filePath(ref);
            check(QDir().mkpath(QFileInfo(path).absolutePath()), "create snapshot directory");
            QFile file(path);
            check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                  "write snapshot");
            snapshot.insert(QLatin1String(name), QJsonObject{{"sha256", hash}, {"sourceRef", ref}});
        }
        int opened = 0;
        const auto copied = quickclash::infra::prepareGeoResources(
            "dns: {enable: true}\nrules: []\n", geoDestination.path(), snapshot, geoRoot.path(),
            [&](QFile& source) {
                ++opened;
                return source.open(QIODevice::ReadOnly);
            });
        check(std::holds_alternative<std::monostate>(copied) && opened == 2,
              "both updated Geo files use caller-authorized source handles");
        QFile copiedGeo(QDir(geoDestination.path()).filePath(QStringLiteral("GeoIP.dat")));
        check(copiedGeo.open(QIODevice::ReadOnly) && copiedGeo.readAll() == "updated-geoip.dat",
              "updated snapshot bytes are used instead of bundled Geo");
        check(std::holds_alternative<Error>(quickclash::infra::prepareGeoResources(
                  "dns: {enable: true}\nrules: []\n", geoDestination.path(), snapshot,
                  geoRoot.path(), [](QFile&) { return false; })),
              "source permission denial fails closed");
        check(!history.activeRevision && !history.lastGoodRevision,
              "first run has no confirmed history");
        const RuntimePreferences preferences;
        const auto routingConfig = quickclash::infra::configurationSummary(
            "proxies: [{name: node, type: ss, udp: true}]\nproxy-groups:\n"
            "- {name: Z-primary, type: select, proxies: [A-auto]}\n"
            "- {name: A-auto, type: fallback, proxies: [node]}\nrules: [\"MATCH,Z-primary\"]\n");
        check(routingConfig.value("nodes").toArray().first().toObject().value("type") == "ss" &&
                  routingConfig.value("nodes").toArray().first().toObject().value("udp").toBool(),
              "node cards receive protocol and explicit UDP capability");
        const auto automaticGroup = routingConfig.value("groups").toArray()[2].toObject();
        check(automaticGroup.value("name") == "A-auto" &&
                  automaticGroup.value("automatic").toBool() &&
                  automaticGroup.value("current").isNull(),
              "automatic groups do not invent an offline current member");
        const auto automaticSources = quickclash::infra::configurationSummary(
            "proxies: []\nproxy-providers: {remote: {type: http, url: "
            "https://example.invalid/nodes.yaml, path: ./nodes.yaml}}\n"
            "proxy-groups: [{name: auto, type: url-test, use: [remote]}]\n"
            "rules: [\"MATCH,auto\"]\n");
        const auto sourced = automaticSources.value("groups").toArray()[1].toObject();
        check(sourced.value("sources").toArray() == QJsonArray{"remote"} &&
                  sourced.value("current").isNull(),
              "provider-backed automatic groups expose their source without a current member");
        const auto optionalHealthUrl = quickclash::infra::preflightConfig(
            "proxies: []\nproxy-groups: [\n"
            "  {name: auto, type: url-test, proxies: [DIRECT, REJECT]},\n"
            "  {name: failover, type: fallback, proxies: [DIRECT, REJECT]}]\n"
            "rules: [\"MATCH,auto\"]\n",
            {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(optionalHealthUrl),
              "url-test and fallback groups may use the core health-check default");
        QJsonObject live;
        for (const auto& value : routingConfig.value("groups").toArray()) {
            const auto group = value.toObject();
            const auto name = group.value("name").toString();
            live.insert(
                name,
                QJsonObject{{"type", group.value("canSelect").toBool() ? "Selector" : "Fallback"},
                            {"now", name == "A-auto" ? QJsonValue{"node"} : group.value("current")},
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
        const auto balancedGroups =
            quickclash::infra::orderedProxyGroups(routingConfig.value("groups").toArray(), live);
        check(balancedGroups.last().toObject().value("current").isNull() &&
                  !balancedGroups.last().toObject().value("canSelect").toBool(),
              "balancing group never exposes one current member or manual selection");
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
                  std::get<QByteArray>(session).contains("secret:") &&
                  std::get<QByteArray>(session).contains("find-process-mode: always"),
              "session includes secret and process lookup");
        for (const auto* mode : {"always", "strict", "off", "future-core-value"}) {
            const auto processSource =
                fixture("inline_valid.yaml") + "\nfind-process-mode: " + mode;
            const auto prepared = quickclash::infra::preflightConfig(processSource, {});
            check(std::holds_alternative<quickclash::infra::PreflightConfig>(prepared) &&
                      !std::get<quickclash::infra::PreflightConfig>(prepared).semanticYaml.contains(
                          "find-process-mode"),
                  "application-owned fields are overridden before core validation");
        }
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
            {"{proxies: [], proxy-groups: [], rules: ['MATCH,DIRECT'], "
             "find-process-mode: off}",
             {}},
            QByteArray(32, 'x'));
        check(std::holds_alternative<QByteArray>(legacySession) &&
                  std::get<QByteArray>(legacySession).contains("find-process-mode: always") &&
                  std::holds_alternative<quickclash::infra::PreflightConfig>(
                      quickclash::infra::preflightConfig(std::get<QByteArray>(legacySession), {})),
              "legacy flow artifact enables process lookup after upgrade");
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
                      quickclash::infra::configMetadata("proxies: []\n---\nproxies: []\n")),
              "metadata keeps duplicate-key and document rejection in skipped fields");
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
        const auto coreBytes = fixture("core_compatible.yaml");
        const auto coreCompatible = quickclash::infra::preflightConfig(coreBytes, {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(coreCompatible),
              "protocol/transport/group/rule/DNS options are delegated to official core");
        const auto coreSemantic =
            std::get<quickclash::infra::PreflightConfig>(coreCompatible).semanticYaml;
        check(coreSemantic.contains("anytls") && coreSemantic.contains("alterId") &&
                  coreSemantic.contains("wireguard") && coreSemantic.contains("PROCESS-NAME") &&
                  coreSemantic.contains("quic://") && coreSemantic.contains("sniffer"),
              "core options survive preflight without client allowlists");
        const auto corePreview = quickclash::infra::redactedConfig(coreSemantic);
        check(!corePreview.contains("public_dummy") && !corePreview.contains("AQEBAQEBAQ") &&
                  !corePreview.contains("AgICAgICAg") && !corePreview.contains("127.0.0.1"),
              "new protocol credentials and unclassified subtrees remain redacted");
        const auto aliasSource =
            QByteArray("defaults: &node {type: anytls, server: 127.0.0.1, port: 443, "
                       "password: public_dummy}\nproxies: [{<<: *node, name: merged}]\n"
                       "proxy-groups: [{name: Main, type: select, proxies: [merged]}]\n"
                       "rules: ['MATCH,Main']\n");
        const auto aliases = quickclash::infra::preflightConfig(aliasSource, {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(aliases) &&
                  std::get<quickclash::infra::PreflightConfig>(aliases).semanticYaml.contains(
                      "anytls"),
              "YAML aliases and merge defaults expand with bounded lifetime");
        expectError("proxies: &nodes [*nodes]", "ConfigLimitExceeded");
        QByteArray aliasedRules =
            "rules: ['DOMAIN,one.invalid,DIRECT', &final 'MATCH,DIRECT', *final]\n";
        QBuffer aliasInput(&aliasedRules);
        check(aliasInput.open(QIODevice::ReadOnly), "open alias rule input");
        std::vector<std::string> emittedRules;
        const auto aliasScan = quickclash::infra::scanConfigScalars(
            aliasInput, "rules",
            [&](const auto& rule) {
                emittedRules.push_back(rule);
                return true;
            },
            16 * 1024 * 1024);
        check(std::holds_alternative<quickclash::infra::ConfigMetadata>(aliasScan) &&
                  emittedRules == std::vector<std::string>{"DOMAIN,one.invalid,DIRECT",
                                                           "MATCH,DIRECT", "MATCH,DIRECT"},
              "alias normalization publishes each rule once");
        const auto managed = quickclash::infra::preflightConfig(
            coreBytes + "\nlisteners: [{name: public, type: mixed, port: 18080}]\n"
                        "external-controller-pipe: public_pipe\n"
                        "external-controller-tls: '0.0.0.0:18081'\n"
                        "tun: {enable: true, dns-hijack: ['any:53'], device: public_tun}\n"
                        "ntp: {enable: false, write-to-system: true}\n",
            {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(managed),
              "managed listener and TUN inputs import without taking control");
        const auto managedYaml = std::get<quickclash::infra::PreflightConfig>(managed).semanticYaml;
        check(!managedYaml.contains("listeners:") && !managedYaml.contains("public_pipe") &&
                  !managedYaml.contains("public_tun") && !managedYaml.contains("dns-hijack") &&
                  managedYaml.contains("write-to-system: false"),
              "import cannot create extra listeners or change managed TUN/system clock");
        for (const auto& mode : {QStringLiteral("fake-ip"), QStringLiteral("redir-host")}) {
            quickclash::domain::RuntimePreferences dnsPreferences;
            dnsPreferences.dnsMode = mode;
            const auto configured = quickclash::infra::preflightConfig(
                "proxies: []\nproxy-groups: []\nrules: ['MATCH,DIRECT']\n", dnsPreferences);
            check(std::holds_alternative<quickclash::infra::PreflightConfig>(configured),
                  "DNS preference supplies bounded defaults");
            const auto semantic =
                std::get<quickclash::infra::PreflightConfig>(configured).semanticYaml;
            check(semantic.contains(mode.toUtf8()) &&
                      semantic.contains("https://1.1.1.1/dns-query") &&
                      semantic.contains("default-nameserver") && !semantic.contains("0.0.0.0:53"),
                  "DNS override supplies IP bootstrap without external listener");
        }
        expectError(fixture("invalid.yaml"), "InvalidYaml");
        expectError("proxies: []\nproxies: []", "DuplicateConfigKey");
        check(
            std::holds_alternative<quickclash::infra::PreflightConfig>(
                quickclash::infra::preflightConfig("proxies: &nodes []\nproxy-groups: *nodes", {})),
            "ordinary YAML anchors accepted");
        expectError(QByteArray(40, '[') + "0" + QByteArray(40, ']'), "ConfigLimitExceeded");
        expectError(QByteArray(16 * 1024 * 1024 + 1, ' '), "ConfigLimitExceeded");
        const auto mainstreamBytes = fixture("mainstream_valid.yaml");
        const auto advancedBytes = fixture("advanced_supported.yaml");
        const auto advanced = quickclash::infra::preflightConfig(advancedBytes, {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(advanced),
              "TUIC v4/v5 and all three load-balance strategies accepted");
        const auto advancedSemantic =
            std::get<quickclash::infra::PreflightConfig>(advanced).semanticYaml;
        const auto advancedSummary = quickclash::infra::configurationSummary(advancedSemantic);
        for (const auto& node : advancedSummary.value("nodes").toArray())
            check(node.toObject().value("type") == "tuic" && node.toObject().value("udp").toBool(),
                  "TUIC has native UDP regardless of absent optional flag");
        for (const auto& group : advancedSummary.value("groups").toArray())
            if (group.toObject().value("name") != "GLOBAL")
                check(!group.toObject().value("canSelect").toBool() &&
                          group.toObject().value("current").isNull(),
                      "offline load balancing never invents a selected first member");
        const auto advancedPreview = quickclash::infra::redactedConfig(advancedSemantic);
        check(!advancedPreview.contains("public_fixture_password") &&
                  !advancedPreview.contains("public_fixture_token") &&
                  !advancedPreview.contains("00000000-0000"),
              "TUIC authentication is redacted");
        const auto mainstream = quickclash::infra::preflightConfig(mainstreamBytes, {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(mainstream),
              "mainstream protocols, transports and nested automatic groups accepted");
        const auto semantic = std::get<quickclash::infra::PreflightConfig>(mainstream).semanticYaml;
        const auto preview = quickclash::infra::redactedConfig(semantic);
        check(!preview.contains("public_dummy") && !preview.contains("AAAAAAAAAAAAAAAA") &&
                  !preview.contains("00000000-0000") && !preview.contains("/public-fixture") &&
                  !preview.contains("example.invalid"),
              "nested secrets and health URLs redacted");
        auto compatibleSource = mainstreamBytes;
        compatibleSource.replace("type: hysteria2",
                                 "type: hysteria2\n    x-vendor-node-id: public_dummy_metadata");
        compatibleSource +=
            "\ngeox-url: {geoip: 'https://example.invalid/geo?token=public_dummy'}\n";
        const auto compatible = quickclash::infra::preflightConfig(compatibleSource, {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(compatible),
              "managed Geo and scalar vendor node metadata accepted");
        const auto& compatibleConfig = std::get<quickclash::infra::PreflightConfig>(compatible);
        const auto& compatibleYaml = compatibleConfig.semanticYaml;
        check(compatibleSource.contains("x-vendor-node-id") &&
                  compatibleSource.contains("geox-url"),
              "raw source retained");
        check(!compatibleYaml.contains("geox-url") &&
                  !compatibleYaml.contains("x-vendor-node-id") &&
                  !compatibleYaml.contains("public_dummy_metadata"),
              "managed Geo and metadata not forwarded");
        check(compatibleYaml.contains("encryption") && compatibleYaml.contains("ip-version"),
              "core protocol options retained");
        check(std::find(compatibleConfig.overriddenFields.begin(),
                        compatibleConfig.overriddenFields.end(),
                        QStringLiteral("geox-url")) != compatibleConfig.overriddenFields.end() &&
                  std::find(compatibleConfig.overriddenFields.begin(),
                            compatibleConfig.overriddenFields.end(),
                            QStringLiteral("proxies.<extension>")) !=
                      compatibleConfig.overriddenFields.end(),
              "managed Geo and ignored metadata reported");
        auto futureOption = compatibleSource;
        futureOption.replace("x-vendor-node-id: public_dummy_metadata",
                             "future-option: {nested: public_dummy_metadata}");
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(
                  quickclash::infra::preflightConfig(futureOption, {})),
              "new core options are not rejected by a stale client schema");
        const QByteArray identityKey(32, 'k');
        auto changed = semantic;
        changed.replace("public_dummy_password", "changed_dummy_password");
        check(quickclash::infra::nodeIdentities(semantic, identityKey).value("hy2_fixture") !=
                  quickclash::infra::nodeIdentities(changed, identityKey).value("hy2_fixture"),
              "protocol credential change invalidates saved identity");
        const auto malformed = quickclash::infra::preflightConfig(
            "proxies: [{name: [private_secret], type: anytls}]\n", {});
        check(std::holds_alternative<Error>(malformed) &&
                  !std::get<Error>(malformed).configIssues.empty() &&
                  !std::get<Error>(malformed).configIssues.front().path.contains("private_secret"),
              "projection failures expose structural paths only");
        QByteArray pendingProviders = "proxies: []\nproxy-groups: []\nrule-providers:\n";
        for (int index = 0; index < 32; ++index)
            pendingProviders +=
                "  private_provider_" + QByteArray::number(index) + ": {type: http}\n";
        const auto bounded = quickclash::infra::preflightConfig(pendingProviders, {});
        check(std::holds_alternative<Error>(bounded) &&
                  std::get<Error>(bounded).configIssues.size() == 1 &&
                  std::get<Error>(bounded).configIssues.front().occurrences == 32 &&
                  !std::get<Error>(bounded).configIssues.front().path.contains("private_provider"),
              "resource diagnostics remain bounded and do not expose provider names");
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
        auto implicitMembers = providerConfig;
        implicitMembers.replace("use: [remote]", "include-all-proxies: true");
        check(quickclash::infra::requiresRuntimeProviders(implicitMembers),
              "implicit group membership retains native provider semantics");
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
        const auto managedPath = quickclash::infra::runtimeProviderPath(
            "proxy-providers", "remote", "https://example.invalid/proxies", "yaml", "");
        const QByteArray runtimeProvider =
            "proxies: []\nproxy-providers:\n  remote:\n    type: http\n"
            "    url: https://example.invalid/proxies\n    path: " +
            managedPath.toUtf8() +
            "\n    size-limit: 4194304\n    interval: 0\n"
            "    payload: [{name: remote_node, type: http, server: 127.0.0.1, port: 8080, "
            "x-vendor-id: public_dummy}]\n"
            "proxy-groups: [{name: Main, type: select, proxies: [], use: [remote]}]\n"
            "rules: ['MATCH,Main']\n";
        const auto checkedProvider = quickclash::infra::preflightConfig(runtimeProvider, {});
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(checkedProvider),
              "runtime provider scalar metadata accepted");
        const auto& providerEffective =
            std::get<quickclash::infra::PreflightConfig>(checkedProvider);
        check(!providerEffective.semanticYaml.contains("x-vendor-id") &&
                  !providerEffective.semanticYaml.contains("public_dummy") &&
                  std::find(providerEffective.overriddenFields.begin(),
                            providerEffective.overriddenFields.end(),
                            QStringLiteral("proxies.<extension>")) !=
                      providerEffective.overriddenFields.end(),
              "runtime provider metadata removed and disclosed");
        const auto offlineProvider = quickclash::infra::offlineProviderConfig(runtimeProvider);
        check(
            std::holds_alternative<QByteArray>(offlineProvider) &&
                std::holds_alternative<quickclash::infra::PreflightConfig>(
                    quickclash::infra::preflightConfig(std::get<QByteArray>(offlineProvider), {})),
            "offline provider expansion accepts the same metadata");
        auto invalidProviderExtension = runtimeProvider;
        invalidProviderExtension.replace("x-vendor-id: public_dummy",
                                         "x-vendor-id: {path: /private}");
        check(std::holds_alternative<quickclash::infra::PreflightConfig>(
                  quickclash::infra::preflightConfig(invalidProviderExtension, {})),
              "managed provider nodes use the same core-delegated schema");
        auto insecureProvider = providerConfig;
        insecureProvider.replace("https://", "http://");
        check(std::holds_alternative<QJsonArray>(
                  quickclash::infra::providerRequests(insecureProvider)),
              "explicit HTTP provider accepted without automatic TLS downgrade");
        check(std::holds_alternative<Error>(quickclash::infra::expandProviders(providerConfig, {})),
              "missing provider cannot silently drop rules");
        const QSet<QString> allowedProviders{"proxies:node/a?b", "rules:domains"};
        check(quickclash::infra::providerRefreshPath({{"kind", "proxies"}, {"name", "node/a?b"}},
                                                     allowedProviders) ==
                      QByteArray("/providers/proxies/node%2Fa%3Fb") &&
                  !quickclash::infra::providerRefreshPath(
                      {{"kind", "proxies"}, {"name", "unknown"}}, allowedProviders) &&
                  !quickclash::infra::providerRefreshPath(
                      {{"kind", "proxies"}, {"name", "node/a?b"}, {"url", "https://invalid"}},
                      allowedProviders) &&
                  !quickclash::infra::providerRefreshPath(
                      {{"kind", "proxies:node/a?b"}, {"name", ""}}, allowedProviders),
              "service provider refresh is an exact active identity with encoded fixed path");
        const QJsonObject providerResponse{
            {"providers", QJsonObject{{"domains", QJsonObject{{"vehicleType", "HTTP"},
                                                              {"ruleCount", 2},
                                                              {"updatedAt", "2026-10-29T00:00:00Z"},
                                                              {"url", "https://secret.invalid"},
                                                              {"path", "private/path"}}},
                                      {"inline", QJsonObject{{"vehicleType", "Compatible"}}}}}};
        const auto projectedProviders =
            quickclash::domain::providerCategorySnapshot(providerResponse, {}, "rules");
        check(projectedProviders.error.isEmpty() && projectedProviders.rows.size() == 1 &&
                  projectedProviders.rows.first().toObject().value("count") == 2 &&
                  !QJsonDocument(projectedProviders.rows).toJson().contains("secret.invalid") &&
                  !QJsonDocument(projectedProviders.rows).toJson().contains("private/path"),
              "provider projection excludes contents, URLs and paths");
        const auto failedProviders = quickclash::domain::providerCategorySnapshot(
            Error{QStringLiteral("CoreRequestTimeout")}, projectedProviders.rows, "rules");
        check(!failedProviders.error.isEmpty() && failedProviders.rows.size() == 1 &&
                  failedProviders.rows.first().toObject().value("count") == 2 &&
                  failedProviders.rows.first().toObject().value("stale").toBool(),
              "failed category retains count and marks stale");
        const auto malformedProviders = quickclash::domain::providerCategorySnapshot(
            QJsonObject{{"providers", QJsonArray{}}}, projectedProviders.rows, "rules");
        check(!malformedProviders.error.isEmpty() && malformedProviders.rows.size() == 1,
              "invalid provider response cannot erase prior category");
        const auto emptyProviders = quickclash::domain::providerCategorySnapshot(
            QJsonObject{{"providers", QJsonObject{}}}, projectedProviders.rows, "rules");
        check(emptyProviders.error.isEmpty() && emptyProviders.rows.isEmpty(),
              "successful empty provider category replaces prior rows");
        const auto unknownCount = quickclash::domain::providerCategorySnapshot(
            QJsonObject{
                {"providers", QJsonObject{{"unknown", QJsonObject{{"vehicleType", "HTTP"}}}}}},
            {}, "rules");
        check(!unknownCount.rows.first().toObject().contains("count"),
              "missing provider count remains unknown");
        const auto projectedRules = quickclash::domain::ruleSnapshot(
            {{"rules",
              QJsonArray{
                  QJsonObject{{"type", "RuleSet"}, {"payload", "domains"}, {"proxy", "Main"}},
                  QJsonObject{{"type", "Match"}, {"payload", ""}, {"proxy", "DIRECT"}}}}},
            projectedProviders.rows);
        check(std::holds_alternative<QJsonArray>(projectedRules) &&
                  std::get<QJsonArray>(projectedRules).last().toObject().value("position") == 2 &&
                  std::get<QJsonArray>(projectedRules).first().toObject().value("count") == 2 &&
                  std::get<QJsonArray>(projectedRules).first().toObject().value("providerName") ==
                      "domains",
              "rule order and provider reference metadata projected without invented hit counts");
        check(std::holds_alternative<Error>(quickclash::domain::ruleSnapshot({})) &&
                  std::holds_alternative<Error>(quickclash::domain::ruleSnapshot(
                      {{"rules", QJsonArray{QJsonObject{{"type", "Match"}}}}})),
              "malformed rules fail as a complete snapshot");
        std::cout << "PASS: domain defaults, stale contexts, operation transitions, config subset "
                     "and sessions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
