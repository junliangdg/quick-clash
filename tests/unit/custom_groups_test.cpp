#include "infra/config/config_metadata.h"
#include "infra/config/config_preflight.h"
#include "infra/config/custom_groups.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QUuid>
#include <cstdio>

using namespace quickclash;
namespace {
QJsonObject definition(const QString& name = QStringLiteral("HK automatic")) {
    return {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"name", name},
            {"type", "url-test"},
            {"inline", true},
            {"providers", QJsonArray{}},
            {"mode", "keywords"},
            {"include", "香港,HK"},
            {"exclude", "trial"},
            {"url", "https://example.com/204"},
            {"interval", 300},
            {"timeout", 2000},
            {"tolerance", 50},
            {"lazy", false},
            {"strategy", "consistent-hashing"},
            {"parent", "manual"}};
}
const QByteArray source = R"(proxies:
- {name: HK-A, type: http, server: 127.0.0.1, port: 18080}
- {name: 香港-B, type: http, server: 127.0.0.1, port: 18081}
- {name: HK-trial, type: http, server: 127.0.0.1, port: 18082}
- {name: JP-A, type: http, server: 127.0.0.1, port: 18083}
proxy-groups:
- {name: manual, type: select, proxies: [DIRECT, HK-A]}
rules:
- MATCH,manual
)";
bool error(const domain::Result<infra::CustomGroupConfig>& result, const char* code) {
    const auto* value = std::get_if<domain::Error>(&result);
    return value && value->code == QLatin1String(code);
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "%s\n", label);
        }
    };
    auto spec = definition();
    const auto result = infra::composeCustomGroups(source, {spec});
    check(std::holds_alternative<infra::CustomGroupConfig>(result), "composition");
    if (!std::holds_alternative<infra::CustomGroupConfig>(result))
        return 1;
    const auto composed = std::get<infra::CustomGroupConfig>(result);
    const auto first = composed.view.value("groups").toArray().first().toObject();
    check(first.value("count") == 2 && first.value("members").toArray().contains("HK-A") &&
              !first.value("members").toArray().contains("HK-trial"),
          "Unicode/keywords/exclusion");
    check(composed.yaml.contains("REJECT") && composed.yaml.contains("MATCH,manual"),
          "safe sentinel and rules");
    check(std::holds_alternative<infra::PreflightConfig>(infra::preflightConfig(composed.yaml, {})),
          "schema accepts composed config");
    check(std::get<infra::CustomGroupConfig>(infra::composeCustomGroups(source, {})).yaml == source,
          "empty layer byte preserving");
    QJsonArray excessive;
    for (int index = 0; index < 33; ++index)
        excessive.append(definition(QStringLiteral("group %1").arg(index)));
    check(error(infra::composeCustomGroups(source, excessive), "CustomGroupLimitExceeded"),
          "group count bounded");
    auto oversized = spec;
    oversized.insert("sourceNames", QJsonArray{QString(256 * 1024, QLatin1Char('x'))});
    check(error(infra::composeCustomGroups(source, {oversized}), "CustomGroupLimitExceeded"),
          "definition byte budget bounded");
    auto invalid = spec;
    invalid.insert("name", "manual");
    check(error(infra::composeCustomGroups(source, {invalid}), "CustomGroupNameConflict"),
          "collision");
    invalid = spec;
    invalid.insert("parent", "missing");
    check(error(infra::composeCustomGroups(source, {invalid}), "CustomGroupParentMissing"),
          "parent reference");
    invalid = spec;
    invalid.insert("inline", false);
    check(error(infra::composeCustomGroups(source, {invalid}), "CustomGroupSourceMissing"),
          "empty source");
    invalid = spec;
    invalid.insert("providers", QJsonArray{"missing"});
    check(error(infra::composeCustomGroups(source, {invalid}), "CustomGroupSourceMissing"),
          "missing provider");
    for (const auto& bad : QStringList{"(", "(a+)+", "(?=HK)", "(a)\\1", "(a|aa)+", "a*a*", "a?a?",
                                       "a{9}", "HK`JP"}) {
        invalid = spec;
        invalid.insert("mode", "regex");
        invalid.insert("include", bad);
        check(error(infra::composeCustomGroups(source, {invalid}), "InvalidCustomGroupFilter"),
              "restricted regex");
    }
    invalid = spec;
    invalid.insert("mode", "regex");
    invalid.insert("include", "^hk-[ab]$");
    check(std::get<infra::CustomGroupConfig>(infra::composeCustomGroups(source, {invalid}))
                  .view.value("groups")
                  .toArray()
                  .first()
                  .toObject()
                  .value("count") == 1,
          "case insensitive regex");
    invalid = spec;
    invalid.insert("include", "HK.*");
    check(std::get<infra::CustomGroupConfig>(infra::composeCustomGroups(source, {invalid}))
                  .view.value("groups")
                  .toArray()
                  .first()
                  .toObject()
                  .value("count") == 0,
          "keyword metacharacters literal");
    for (const auto& key : QStringList{"interval", "timeout", "tolerance"}) {
        invalid = spec;
        invalid.insert(key, -1);
        check(error(infra::composeCustomGroups(source, {invalid}), "InvalidCustomGroup"),
              "numeric bounds");
    }
    invalid = spec;
    invalid.insert("interval", 1.2);
    check(error(infra::composeCustomGroups(source, {invalid}), "InvalidCustomGroup"),
          "integer not fractional");
    invalid = spec;
    invalid.insert("url", "https://secret@example.com/204");
    check(error(infra::composeCustomGroups(source, {invalid}), "InvalidCustomGroup"),
          "URL credential rejection");
    check(error(infra::composeCustomGroups(source, {spec, spec}), "InvalidCustomGroup"),
          "duplicate UUID");
    auto empty = spec;
    empty.insert("include", "no-such-node");
    const auto unavailable =
        std::get<infra::CustomGroupConfig>(infra::composeCustomGroups(source, {empty}));
    check(unavailable.view.value("groups").toArray().first().toObject().value("count") == 0 &&
              unavailable.yaml.contains("REJECT"),
          "empty update fail closed");
    QByteArray dynamic = source + R"(
proxy-providers:
  cloud:
    type: http
    url: https://provider.invalid/nodes
    path: providers/test.yaml
    size-limit: 4194304
    payload:
    - {name: HK-cloud, type: http, server: 127.0.0.1, port: 18100}
    - {name: JP-cloud, type: http, server: 127.0.0.1, port: 18101}
)";
    dynamic.replace("providers/test.yaml",
                    infra::runtimeProviderPath("proxy-providers", "cloud",
                                               "https://provider.invalid/nodes", "yaml", "")
                        .toUtf8());
    dynamic.replace("    size-limit: 4194304", "    interval: 0\n    size-limit: 4194304");
    auto provider = spec;
    provider.insert("inline", false);
    provider.insert("providers", QJsonArray{"cloud"});
    const auto observed =
        infra::composeCustomGroups(dynamic, {provider}, {{"cloud", QJsonArray{"US-new"}}});
    check(std::get<infra::CustomGroupConfig>(observed)
                  .view.value("groups")
                  .toArray()
                  .first()
                  .toObject()
                  .value("count") == 0,
          "live provider observation supersedes snapshot preview");
    check(std::holds_alternative<domain::Error>(
              infra::composeProfileConfig(source, {{"customGroups", "wrong-type"}})),
          "malformed definitions rejected");
    const auto live = infra::composeCustomGroups(dynamic, {provider});
    check(std::holds_alternative<infra::CustomGroupConfig>(live), "provider compose");
    if (const auto* value = std::get_if<infra::CustomGroupConfig>(&live)) {
        check(value->view.value("groups").toArray().first().toObject().value("count") == 1 &&
                  value->yaml.contains("filter:") && value->yaml.contains("health-check:"),
              "native filter and provider health");
        check(
            std::holds_alternative<infra::PreflightConfig>(infra::preflightConfig(value->yaml, {})),
            "provider schema");
        const auto frozen = infra::archiveCustomGroups({provider}, value->view);
        check(frozen.first().toObject().value("providers").toArray().isEmpty() &&
                  frozen.first().toObject().value("sourceNames").toArray().contains("HK-cloud"),
              "offline source scope");
    }
    auto second = provider;
    second.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
    second.insert("name", "another");
    second.insert("timeout", 1234);
    check(
        error(infra::composeCustomGroups(dynamic, {provider, second}), "CustomGroupHealthConflict"),
        "shared provider conflicting settings");
    second.insert("timeout", provider.value("timeout"));
    check(std::holds_alternative<infra::CustomGroupConfig>(
              infra::composeCustomGroups(dynamic, {provider, second})),
          "shared provider consistent settings");
    for (const auto& strategy : QStringList{"fallback", "load-balance"}) {
        auto policy = spec;
        policy.insert("type", strategy);
        check(std::holds_alternative<infra::PreflightConfig>(infra::preflightConfig(
                  std::get<infra::CustomGroupConfig>(infra::composeCustomGroups(source, {policy}))
                      .yaml,
                  {})),
              "policy schema");
    }
    std::printf("custom group failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
