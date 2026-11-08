#include "infra/config/config_metadata.h"
#include "infra/config/config_preflight.h"
#include "infra/io/bounded_read.h"

#include <QCryptographicHash>
#include <QFileDevice>
#include <QUrl>
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
namespace {
domain::Error providerError(const char* code) {
    domain::Error error;
    error.code = QString::fromLatin1(code);
    error.userMessageKey = error.code;
    error.stage = QStringLiteral("preflight");
    return error;
}
} // namespace

domain::Result<ProviderValidation> providerValidationConfig(const QByteArray& semantic,
                                                            const QString& pipe) {
    try {
        auto root = YAML::Load(semantic.toStdString());
        ProviderValidation result;
        for (const auto* kind : {"proxy-providers", "rule-providers"}) {
            auto providers = root[kind];
            if (!providers)
                continue;
            for (auto entry : providers) {
                auto value = entry.second;
                if (value["x-quickclash-body"]) {
                    value["type"] = "file";
                    value.remove("url");
                    value.remove("interval");
                    result.targets.append(
                        QByteArray("/providers/") +
                        (std::string(kind) == "proxy-providers" ? "proxies/" : "rules/") +
                        QUrl::toPercentEncoding(QString::fromStdString(entry.first.Scalar())));
                }
                if (value["health-check"])
                    value["health-check"]["enable"] = false;
            }
        }
        if (result.targets.isEmpty())
            return result;
        // The second offline check only loads managed bodies. No data listener,
        // resource download, health check, DNS listener or host setting is enabled.
        root["mixed-port"] = 0;
        root["external-controller"] = "";
        root["external-controller-pipe"] = pipe.toStdString();
        root["dns"]["enable"] = false;
        root["ntp"]["enable"] = false;
        root["sniffer"]["enable"] = false;
        root["find-process-mode"] = "off";
        YAML::Emitter output;
        output << root;
        if (!output.good() || output.size() > 16 * 1024 * 1024)
            return providerError("ProviderLimitExceeded");
        result.config = QByteArray(output.c_str(), static_cast<qsizetype>(output.size()));
        return result;
    } catch (const YAML::Exception&) {
        return providerError("InvalidProviderPayload");
    }
}

QString runtimeProviderPath(const QString& kind, const QString& name, const QString& url,
                            const QString& format, const QString& behavior) {
    const auto identity = (kind + ':' + name).toUtf8() + '\0' + url.toUtf8() + '\0' +
                          format.toUtf8() + '\0' + behavior.toUtf8();
    return QStringLiteral("providers/%1.yaml")
        .arg(QString::fromLatin1(
            QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()));
}
domain::Result<QByteArray> runtimeProviderConfig(const QByteArray& input,
                                                 const QHash<QString, QIODevice*>& payloads) {
    const auto metadata = configMetadata(input, true);
    if (const auto* error = std::get_if<domain::Error>(&metadata))
        return *error;
    const auto requests = providerRequests(input);
    if (const auto* error = std::get_if<domain::Error>(&requests))
        return *error;
    const bool nativeGraph = requiresRuntimeProviders(input);
    const auto invalid = []() -> domain::Result<QByteArray> {
        domain::Error error;
        error.code = QStringLiteral("InvalidProviderPayload");
        error.userMessageKey = error.code;
        error.stage = QStringLiteral("preflight");
        return error;
    };
    try {
        const auto& fields = std::get<ConfigMetadata>(metadata);
        auto root = YAML::Load(fields.fieldsYaml.toStdString());
        if (!root["proxies"])
            root["proxies"] = YAML::Node(YAML::NodeType::Sequence);
        for (auto group : root["proxy-groups"])
            if (!group["proxies"])
                group["proxies"] = YAML::Node(YAML::NodeType::Sequence);
        qint64 total = 0;
        for (const auto& value : std::get<QJsonArray>(requests)) {
            const auto request = value.toObject();
            const auto kind = request.value("kind").toString();
            const auto name = request.value("name").toString();
            auto* file =
                qobject_cast<QFileDevice*>(payloads.value(request.value("key").toString()));
            if (!file || !file->seek(0))
                return invalid();
            const auto loaded = readFileBounded(*file, 4 * 1024 * 1024);
            total += loaded.bytes.size();
            if (!loaded.ok || total > 16 * 1024 * 1024)
                return invalid();
            auto provider = root[kind.toStdString()][name.toStdString()];
            const auto format = provider["format"] ? provider["format"].as<std::string>() : "yaml";
            // Preserve exactly the downloaded bytes. Only the official core interprets
            // MRS, YAML/text rules and URI/Base64 proxy subscriptions.
            provider.remove("payload");
            provider["x-quickclash-body"] = loaded.bytes.toBase64().toStdString();
            provider["x-quickclash-source"] = request.value("url").toString().toStdString();
            // Retain the existing readable fallback for standard YAML/text sources.
            // Opaque native formats instead use the exact managed bytes above.
            YAML::Node payload;
            if (format == "text") {
                payload = YAML::Node(YAML::NodeType::Sequence);
                for (auto line : loaded.bytes.split('\n')) {
                    line = line.trimmed();
                    if (!line.isEmpty() && !line.startsWith('#') && !line.startsWith("//"))
                        payload.push_back(line.toStdString());
                }
            } else if (format == "yaml") {
                const auto parsed = configMetadata(loaded.bytes, true);
                if (const auto* document = std::get_if<ConfigMetadata>(&parsed)) {
                    const auto content = YAML::Load(document->fieldsYaml.toStdString());
                    if (content.IsMap())
                        payload = content[kind == "proxy-providers" ? "proxies" : "payload"];
                }
            }
            if (payload.IsSequence() && request.value("type") != "file" && !nativeGraph) {
                provider["payload"] = payload;
                provider.remove("x-quickclash-body");
                provider.remove("x-quickclash-source");
            }
            if (kind == QStringLiteral("rule-providers") && !provider["behavior"])
                provider["behavior"] = "classical";
            // A new source/format/behavior gets a distinct cache, including across profiles.
            provider["path"] =
                runtimeProviderPath(
                    kind, name, request.value("url").toString(), QString::fromStdString(format),
                    provider["behavior"]
                        ? QString::fromStdString(provider["behavior"].as<std::string>())
                        : QString{})
                    .toStdString();
            provider["size-limit"] = 4 * 1024 * 1024;
            // Manual refresh is explicit. Scheduled refresh remains a core-only write.
            if (!provider["interval"])
                provider["interval"] = 0;
        }
        root.remove("rules");
        root.SetStyle(YAML::EmitterStyle::Block);
        YAML::Emitter output;
        output << root;
        if (!output.good())
            return invalid();
        QByteArray bytes(output.c_str(), static_cast<qsizetype>(output.size()));
        bytes += "\nrules:\n";
        YAML::Emitter rules;
        rules << YAML::BeginSeq;
        for (const auto& rule : fields.rules)
            rules << rule;
        rules << YAML::EndSeq;
        if (!rules.good())
            return invalid();
        bytes.append(rules.c_str(), static_cast<qsizetype>(rules.size()));
        bytes += '\n';
        return bytes.size() <= 16 * 1024 * 1024 ? domain::Result<QByteArray>(bytes) : invalid();
    } catch (const YAML::Exception&) {
        return invalid();
    }
}
domain::Result<QByteArray> offlineProviderConfig(const QByteArray& input) {
    const auto metadata = configMetadata(input, true);
    if (const auto* failure = std::get_if<domain::Error>(&metadata))
        return *failure;
    try {
        const auto& fields = std::get<ConfigMetadata>(metadata);
        auto root = YAML::Load(fields.fieldsYaml.toStdString());
        QHash<QString, QByteArray> payloads;
        qsizetype total = 0;
        for (const auto* kind : {"proxy-providers", "rule-providers"})
            for (auto entry : root[kind]) {
                auto provider = entry.second;
                if (provider["type"] && provider["type"].as<std::string>() == "inline")
                    continue;
                if (provider["x-quickclash-body"]) {
                    // Native resources keep their exact immutable import bytes offline.
                    // File vehicles cannot fetch, and are seeded only at generated paths.
                    provider["type"] = "file";
                    provider.remove("url");
                    provider.remove("interval");
                    continue;
                }
                if (!provider["payload"].IsSequence())
                    return providerError("ProviderCacheUnavailable");
                YAML::Node document(YAML::NodeType::Map);
                document[std::string(kind) == "proxy-providers" ? "proxies" : "payload"] =
                    provider["payload"];
                YAML::Emitter output;
                output << document;
                total += static_cast<qsizetype>(output.size());
                if (!output.good() || output.size() > 4 * 1024 * 1024 || total > 16 * 1024 * 1024 ||
                    payloads.size() >= 32)
                    return providerError("ProviderLimitExceeded");
                payloads.insert(QString::fromLatin1(kind) + ':' +
                                    QString::fromStdString(entry.first.as<std::string>()),
                                QByteArray(output.c_str(), static_cast<qsizetype>(output.size())));
                if (std::string(kind) == "rule-providers")
                    provider["format"] = "yaml";
                provider.remove("payload");
            }
        if (payloads.isEmpty()) {
            YAML::Emitter output;
            root["rules"] = fields.rules;
            output << root;
            if (!output.good() || output.size() > 16 * 1024 * 1024)
                return providerError("ProviderLimitExceeded");
            return QByteArray(output.c_str(), static_cast<qsizetype>(output.size()));
        }
        YAML::Emitter source;
        root.remove("rules");
        source << root;
        QByteArray bytes(source.c_str(), static_cast<qsizetype>(source.size()));
        bytes += "\nrules:\n";
        for (const auto& rule : fields.rules) {
            YAML::Emitter scalar;
            scalar << YAML::DoubleQuoted << rule;
            bytes += "  - ";
            bytes.append(scalar.c_str(), static_cast<qsizetype>(scalar.size()));
            bytes += '\n';
        }
        if (fields.rules.empty())
            bytes += "  []\n";
        if (!source.good() || bytes.size() > 16 * 1024 * 1024)
            return providerError("ProviderLimitExceeded");
        return expandProviders(bytes, payloads);
    } catch (const YAML::Exception&) {
        return providerError("InvalidProviderPayload");
    }
}
} // namespace quickclash::infra
