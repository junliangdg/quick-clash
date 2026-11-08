#include "infra/config/config_metadata.h"
#include "infra/config/config_preflight.h"
#include "infra/io/device_stream.h"
#include <QBuffer>
#include <QStringList>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
namespace {
constexpr qint64 kSourceLimit = 16 * 1024 * 1024;
constexpr qint64 kProviderLimit = 4 * 1024 * 1024;
struct ExpansionFailure {
    domain::Error error;
};
void require(bool condition, const char* code = "InvalidProviderPayload") {
    if (!condition) {
        domain::Error error;
        error.code = QString::fromLatin1(code);
        error.userMessageKey = error.code;
        error.stage = QStringLiteral("preflight");
        throw ExpansionFailure{error};
    }
}
struct ScanBudget {
    qint64 bytes = 0;
    std::size_t events = 0;
    void reserve(QIODevice& device) {
        const auto size = device.size();
        require(size >= 0 && size <= 128 * 1024 * 1024 - bytes, "ProviderLimitExceeded");
        bytes += size;
    }
    void parsed(std::size_t count) {
        require(count <= 200000 - events, "ConfigLimitExceeded");
        events += count;
    }
};
ConfigMetadata scan(ScanBudget& budget, QIODevice& device, const std::string& field,
                    const std::function<bool(const std::string&)>& sink, qint64 limit) {
    budget.reserve(device);
    require(device.seek(0), "DataReadFailed");
    auto result = scanConfigScalars(device, field, sink, limit);
    if (const auto* error = std::get_if<domain::Error>(&result))
        throw ExpansionFailure{*error};
    budget.parsed(std::get<ConfigMetadata>(result).events);
    return std::get<ConfigMetadata>(std::move(result));
}
struct RuleProvider {
    QIODevice* file;
    std::string format;
    std::string behavior;
};
// Re-scan a bounded file on repeated RULE-SET references instead of retaining all rule strings.
std::size_t visitRules(ScanBudget& budget, const RuleProvider& provider,
                       const std::function<bool(const std::string&)>& sink) {
    std::size_t count = 0;
    const auto accept = [&](const std::string& item) {
        require(++count <= 20000 && !item.empty() && item.size() <= 8192, "ProviderLimitExceeded");
        auto value = item;
        if (provider.behavior == "domain") {
            if (value.starts_with("+.") || value.starts_with("*."))
                value = "DOMAIN-SUFFIX," + value.substr(2);
            else
                value = "DOMAIN," + value;
        } else if (provider.behavior == "ipcidr") {
            value = (value.find(':') == std::string::npos ? "IP-CIDR," : "IP-CIDR6,") + value;
        }
        require(value.size() <= 8192, "ProviderLimitExceeded");
        return sink(value);
    };
    if (provider.format == "text") {
        auto& file = *provider.file;
        budget.reserve(file);
        require(file.seek(0) && !file.isSequential(), "DataReadFailed");
        const auto expected = file.size();
        require(expected >= 0 && expected <= kProviderLimit, "ProviderLimitExceeded");
        qint64 total = 0;
        while (!file.atEnd()) {
            const auto line = file.readLine(8194);
            require(!line.isEmpty() && line.size() <= 8193, "ProviderLimitExceeded");
            total += line.size();
            require(total <= kProviderLimit, "ProviderLimitExceeded");
            const auto value = QString::fromUtf8(line).trimmed();
            if (!value.isEmpty() && !value.startsWith('#') && !value.startsWith("//"))
                require(accept(value.toStdString()), "DataWriteFailed");
        }
        const auto* disk = qobject_cast<QFileDevice*>(&file);
        require(total == expected && file.size() == expected &&
                    (!disk || disk->error() == QFileDevice::NoError),
                "DataReadFailed");
        budget.parsed(count);
        return count;
    }
    const auto fields = scan(budget, *provider.file, "payload", accept, kProviderLimit);
    const auto root = YAML::Load(fields.fieldsYaml.toStdString());
    require(root.IsMap() && root["payload"].IsSequence());
    return fields.events;
}
} // namespace
domain::CommandResult expandProviderFiles(QIODevice& input,
                                          const QHash<QString, QIODevice*>& payloads,
                                          QIODevice& output) {
    try {
        require(payloads.size() <= 32, "ProviderLimitExceeded");
        ScanBudget budget;
        auto fields =
            scan(budget, input, "rules", [](const std::string&) { return true; }, kSourceLimit);
        std::size_t events = fields.events;
        qint64 structuredBytes = fields.fieldsYaml.size();
        auto root = YAML::Load(fields.fieldsYaml.toStdString());
        require(root.IsMap());
        require(!root["rules"] || root["rules"].IsSequence(), "InvalidProviderConfig");
        require(!root["proxies"] || root["proxies"].IsSequence());
        require(!root["proxy-groups"] || root["proxy-groups"].IsSequence());
        require(root["proxies"].size() <= 5000 && root["proxy-groups"].size() <= 500,
                "ProviderLimitExceeded");
        if (!root["proxies"])
            root["proxies"] = YAML::Node(YAML::NodeType::Sequence);
        fields = {}; // Release serialized source fields before importing provider objects.
        std::map<std::string, std::vector<std::string>> proxyNames;
        std::map<std::string, RuleProvider> rules;
        std::set<std::string> inlineRules;
        qint64 providerBytes = 0;
        std::size_t providerCount = 0;
        for (const auto* kind : {"proxy-providers", "rule-providers"}) {
            const auto providers = root[kind];
            require(!providers || providers.IsMap(), "InvalidProviderConfig");
            for (const auto& entry : providers) {
                require(++providerCount <= 32, "ProviderLimitExceeded");
                const auto name = entry.first.as<std::string>();
                const auto key = QString::fromLatin1(kind) + ':' + QString::fromStdString(name);
                const auto value = entry.second;
                if (value["x-quickclash-body"] ||
                    (value["type"] && value["type"].as<std::string>() == "inline")) {
                    if (std::string(kind) == "rule-providers")
                        inlineRules.insert(name);
                    continue; // The official core consumes inline providers without a download.
                }
                auto* file = payloads.value(key, nullptr);
                require(file && file->isOpen() && !file->isSequential(), "ProviderSnapshotMissing");
                require(file->size() >= 0 && file->size() <= kProviderLimit,
                        "ProviderLimitExceeded");
                providerBytes += file->size();
                require(providerBytes <= kSourceLimit, "ProviderLimitExceeded");
                const auto format = value["format"] ? value["format"].as<std::string>() : "yaml";
                const auto behavior =
                    value["behavior"] ? value["behavior"].as<std::string>() : "classical";
                require(behavior == "classical" || behavior == "domain" || behavior == "ipcidr",
                        "UnsupportedProviderFormat");
                if (std::string(kind) == "rule-providers") {
                    require(format == "text" || format == "yaml", "UnsupportedProviderFormat");
                    const RuleProvider provider{file, format, behavior};
                    events += visitRules(budget, provider, [](const std::string&) { return true; });
                    rules.emplace(name, provider);
                } else {
                    require(format == "yaml", "UnsupportedProviderFormat");
                    auto metadata = scan(
                        budget, *file, "payload", [](const std::string&) { return true; },
                        kProviderLimit);
                    events += metadata.events;
                    structuredBytes += metadata.fieldsYaml.size();
                    require(structuredBytes <= kProviderLimit, "ProviderLimitExceeded");
                    const auto document = YAML::Load(metadata.fieldsYaml.toStdString());
                    const auto proxies = document["proxies"];
                    require(proxies.IsSequence() && proxies.size() <= 5000 - root["proxies"].size(),
                            "ProviderLimitExceeded");
                    auto& names = proxyNames[name];
                    for (const auto& proxy : proxies) {
                        require(proxy.IsMap() && proxy["name"].IsScalar() &&
                                proxy["name"].Scalar().size() <= 512);
                        names.push_back(proxy["name"].Scalar());
                        root["proxies"].push_back(proxy);
                    }
                }
                require(events <= 100000, "ConfigLimitExceeded");
            }
            std::vector<std::string> materialized;
            for (const auto& entry : providers)
                if (!entry.second["x-quickclash-body"] &&
                    (!entry.second["type"] || entry.second["type"].as<std::string>() != "inline"))
                    materialized.push_back(entry.first.as<std::string>());
            for (const auto& name : materialized)
                root[kind].remove(name);
            if (root[kind].size() == 0)
                root.remove(kind);
        }
        std::size_t members = 0;
        for (auto group : root["proxy-groups"]) {
            require(group.IsMap());
            require(!group["proxies"] || group["proxies"].IsSequence());
            members += group["proxies"].size();
            require(group["proxies"].size() <= 5000 && members <= 100000, "ProviderLimitExceeded");
            if (group["use"]) {
                require(group["use"].IsSequence() && group["use"].size() <= 32,
                        "InvalidProviderConfig");
                YAML::Node retained(YAML::NodeType::Sequence);
                for (const auto& reference : group["use"]) {
                    const auto name = reference.as<std::string>();
                    if (root["proxy-providers"] && root["proxy-providers"][name]) {
                        retained.push_back(name);
                        continue;
                    }
                    require(proxyNames.contains(name), "ProviderSnapshotMissing");
                    for (const auto& member : proxyNames.at(name)) {
                        structuredBytes += static_cast<qint64>(member.size() * 6 + 32);
                        require(structuredBytes <= kProviderLimit, "ProviderLimitExceeded");
                        require(group["proxies"].size() < 5000 && ++members <= 100000,
                                "ProviderLimitExceeded");
                        group["proxies"].push_back(member);
                    }
                }
                if (retained.size())
                    group["use"] = retained;
                else
                    group.remove("use");
            }
        }
        root.remove("rules");
        DeviceOutputBuffer buffer(output, kSourceLimit);
        std::ostream stream(&buffer);
        const auto write = [&](QByteArrayView bytes) {
            require(bytes.size() <= kSourceLimit - buffer.written(), "ProviderLimitExceeded");
            stream.write(bytes.data(), bytes.size());
            require(stream.good(), "DataWriteFailed");
        };
        // Fields have cumulative byte/event/node/member bounds before this small graph is emitted.
        root.SetStyle(YAML::EmitterStyle::Block);
        YAML::Emitter header(stream);
        header << root;
        require(header.good() && stream.good(), "ProviderLimitExceeded");
        root.reset();
        proxyNames.clear();
        write(QByteArrayView("\nrules:\n", 8));
        std::size_t ruleCount = 0;
        const auto emitRule = [&](const std::string& value) {
            require(++ruleCount <= 20000 && value.size() <= 8192, "ProviderLimitExceeded");
            YAML::Emitter scalar;
            scalar << YAML::DoubleQuoted << value;
            require(scalar.good());
            write(QByteArrayView("  - ", 4));
            write(QByteArrayView(scalar.c_str(), static_cast<qsizetype>(scalar.size())));
            write(QByteArrayView("\n", 1));
            return true;
        };
        (void)scan(
            budget, input, "rules",
            [&](const std::string& value) {
                const auto parts = QString::fromStdString(value).split(',');
                if (parts.first() != QStringLiteral("RULE-SET"))
                    return emitRule(value);
                require(parts.size() == 3 || (parts.size() == 4 && parts[3] == "no-resolve"),
                        "InvalidProviderConfig");
                const auto name = parts[1].toStdString();
                // Inline resources remain in the emitted header for the official core.
                // They have no downloaded snapshot to flatten here.
                if (inlineRules.contains(name))
                    return emitRule(value);
                require(rules.contains(name), "ProviderSnapshotMissing");
                (void)visitRules(budget, rules.at(name), [&](const std::string& item) {
                    auto rule = item;
                    const bool noResolve = rule.ends_with(",no-resolve") || parts.size() == 4;
                    if (rule.ends_with(",no-resolve"))
                        rule.resize(rule.size() - 11);
                    rule += ',' + parts[2].toStdString();
                    if (noResolve)
                        rule += ",no-resolve";
                    return emitRule(rule);
                });
                return true;
            },
            kSourceLimit);
        if (ruleCount == 0)
            write(QByteArrayView("  []\n", 5));
        return std::monostate{};
    } catch (const ExpansionFailure& failure) {
        return failure.error;
    } catch (const YAML::Exception&) {
        domain::Error error;
        error.code = QStringLiteral("InvalidProviderPayload");
        error.userMessageKey = error.code;
        error.stage = QStringLiteral("preflight");
        return error;
    }
}
domain::Result<QByteArray> expandProviders(const QByteArray& input,
                                           const QHash<QString, QByteArray>& payloads) {
    QBuffer source;
    source.setData(input);
    source.open(QIODevice::ReadOnly);
    QHash<QString, QIODevice*> borrowed;
    std::vector<std::unique_ptr<QBuffer>> buffers;
    for (auto it = payloads.begin(); it != payloads.end(); ++it) {
        auto file = std::make_unique<QBuffer>();
        file->setData(it.value());
        file->open(QIODevice::ReadOnly);
        borrowed.insert(it.key(), file.get());
        buffers.push_back(std::move(file));
    }
    QBuffer output;
    output.open(QIODevice::WriteOnly);
    const auto expanded = expandProviderFiles(source, borrowed, output);
    if (const auto* failure = std::get_if<domain::Error>(&expanded))
        return *failure;
    return output.data();
}
} // namespace quickclash::infra
