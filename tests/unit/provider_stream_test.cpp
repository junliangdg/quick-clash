#include "infra/config/config_metadata.h"
#include "infra/config/config_preflight.h"
#include "infra/storage/provider_stage.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
using namespace quickclash;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> bool good(const T& result) {
    return !std::holds_alternative<domain::Error>(result);
}
bool feed(infra::ProviderStage& stage, QByteArray bytes) {
    QBuffer input(&bytes);
    check(input.open(QIODevice::ReadOnly), "feed open");
    return good(stage.consume(input));
}
QByteArray configuration(const QByteArray& behavior = "classical",
                         const QByteArray& format = "yaml") {
    return "proxies: []\nproxy-groups: [{name: Main, type: select, proxies: [DIRECT]}]\n"
           "rule-providers: {a: {type: http, url: 'https://example.invalid/a', behavior: " +
           behavior + ", format: " + format + "}}\nrules: ['RULE-SET,a,Main', 'MATCH,DIRECT']\n";
}
void clean(const QString& root) {
    check(QDir(root + "/cache/config_stage").entryList(QDir::Files | QDir::Hidden).isEmpty(),
          "all staging files removed");
}
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        infra::JsonStore store(directory.path());
        check(!good(store.beginConfigTemporary()), "unopened store cannot create staging");
        check(good(store.open()), "store open");
        const auto digest = [](QByteArrayView bytes) {
            return QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        };
        const QByteArray verified(64 * 1024 + 17, 'v');
        check(good(store.writeBytes("data/verify.yaml", verified, true)) &&
                  good(store.verifyBytes("data/verify.yaml", digest(verified))),
              "chunked verification crosses chunk boundary");
        check(!good(store.verifyBytes("data/verify.yaml", digest("different"))) &&
                  !good(store.verifyBytes("../outside", digest(verified))) &&
                  !good(store.verifyBytes("data/missing.yaml", digest(verified))),
              "digest mismatch, unsafe path and missing file rejected");
        check(good(store.writeBytes("data/verify.yaml", "tampered")) &&
                  !good(store.verifyBytes("data/verify.yaml", digest(verified))),
              "persisted tampering rejected");
        {
            const QByteArray exact(16 * 1024 * 1024, 'b');
            check(good(store.writeBytes("data/boundary.yaml", exact, true)) &&
                      good(store.verifyBytes("data/boundary.yaml", digest(exact))),
                  "verification exact 16 MiB boundary");
            QFile grown(std::get<QString>(store.path("data/boundary.yaml")));
            check(grown.open(QIODevice::Append) && grown.write("x", 1) == 1, "grow fixture");
            grown.close();
            check(!good(store.verifyBytes("data/boundary.yaml", digest(exact))),
                  "verification rejects oversized persisted file");
        }
        {
            infra::ProviderStage stage(store);
            check(good(stage.begin("rule-providers:a")), "begin first serial file");
            check(!good(stage.begin("rule-providers:b")), "parallel item rejected");
            check(feed(stage, "pay") && feed(stage, "load: ['DOMAIN,example.invalid']"),
                  "short chunks append to disk");
            check(good(stage.finish()), "seal first file");
            const auto devices = stage.devices();
            const auto* device = devices.value("rule-providers:a");
            check(devices.size() == 1 && device && device->pos() == 0, "sealed file borrowed");
            check(!good(stage.begin("rule-providers:a")), "duplicate key not overwritten");
            stage.cancel();
            check(stage.fileCount() == 0 && stage.devices().isEmpty() && !feed(stage, "late") &&
                      !good(stage.finish()) && !good(stage.begin("late")),
                  "cancel clears files and prevents late resurrection");
        }
        clean(directory.path());
        {
            infra::ProviderStage stage(store);
            check(good(stage.begin("partial")) && feed(stage, "abandoned"), "partial file");
        }
        clean(directory.path());
        {
            infra::ProviderStage stage(store);
            check(good(stage.begin("empty")) && !good(stage.finish()), "empty item rejected");
        }
        clean(directory.path());
        const QByteArray chunk(64 * 1024, '#');
        for (const bool overflow : {false, true}) {
            infra::ProviderStage stage(store);
            for (int item = 0; item < 4; ++item) {
                check(good(stage.begin(QString::number(item))), "begin cumulative item");
                for (int block = 0; block < 64; ++block)
                    check(feed(stage, chunk), "up to exact 4 MiB item");
                if (overflow && item == 0) {
                    check(!feed(stage, "x"), "per-item 4 MiB plus one rejected");
                    break;
                }
                check(good(stage.finish()), "exact item boundary seals");
            }
            if (!overflow) {
                check(stage.totalBytes() == 16 * 1024 * 1024 && good(stage.begin("fifth")) &&
                          !feed(stage, "x"),
                      "cumulative 16 MiB plus one rejected");
            }
        }
        clean(directory.path());
        {
            infra::ProviderStage stage(store);
            for (int i = 0; i < 32; ++i)
                check(good(stage.begin(QString::number(i))) && feed(stage, "x") &&
                          good(stage.finish()),
                      "32 serial items");
            check(!good(stage.begin("33")), "33rd item rejected");
        }
        clean(directory.path());
        const QByteArray source =
            "proxy-providers: {p: {type: http, url: 'https://example.invalid/p'}}\n"
            "rule-providers: {a: {type: http, url: 'https://example.invalid/a'}, "
            "d: {type: http, url: 'https://example.invalid/d', behavior: domain}, "
            "i: {type: http, url: 'https://example.invalid/i', behavior: ipcidr, format: text}}\n"
            "proxy-groups: [{name: Main, type: select, use: [p]}]\n"
            "rules: ['DOMAIN,first.invalid,DIRECT', 'RULE-SET,a,Main', 'RULE-SET,d,Main', "
            "'RULE-SET,i,DIRECT,no-resolve', 'RULE-SET,a,REJECT', 'MATCH,DIRECT']\n";
        const QHash<QString, QByteArray> payloads{
            {"proxy-providers:p",
             "proxies: [{name: N, type: http, server: 127.0.0.1, port: 8080}]"},
            {"rule-providers:a",
             "payload: ['IP-CIDR,192.0.2.0/24,no-resolve', 'DOMAIN,c.invalid']"},
            {"rule-providers:d", "payload: ['+.d.invalid', '*.e.invalid', 'f.invalid']"},
            {"rule-providers:i", "# fixture\n192.0.2.0/24\n2001:db8::/32\n"}};
        const auto expanded = infra::expandProviders(source, payloads);
        check(good(expanded), "multiple providers expand");
        const auto bytes = std::get<QByteArray>(expanded);
        const auto metadata = infra::configMetadata(bytes, true);
        check(good(metadata), "expanded metadata");
        const auto& rules = std::get<infra::ConfigMetadata>(metadata).rules;
        check(rules ==
                  std::vector<std::string>{"DOMAIN,first.invalid,DIRECT",
                                           "IP-CIDR,192.0.2.0/24,Main,no-resolve",
                                           "DOMAIN,c.invalid,Main", "DOMAIN-SUFFIX,d.invalid,Main",
                                           "DOMAIN-SUFFIX,e.invalid,Main", "DOMAIN,f.invalid,Main",
                                           "IP-CIDR,192.0.2.0/24,DIRECT,no-resolve",
                                           "IP-CIDR6,2001:db8::/32,DIRECT,no-resolve",
                                           "IP-CIDR,192.0.2.0/24,REJECT,no-resolve",
                                           "DOMAIN,c.invalid,REJECT", "MATCH,DIRECT"},
              "original order, duplicate references, behaviors and no-resolve");
        check(good(infra::preflightConfig(bytes, {})) &&
                  !bytes.contains("https://example.invalid") && !bytes.contains("use:"),
              "full schema follows expansion and use becomes members");
        check(infra::nodeIdentities(bytes, QByteArray(32, 'k')).contains("N"),
              "node identity preserved");
        for (const auto& invalid :
             {QByteArray("payload: [x]\npayload: [y]"), QByteArray("payload: [*missing]"),
              QByteArray("payload: [x]\n---\npayload: [y]"), QByteArray("payload: [{nested: x}]"),
              QByteArray("payload: !unsafe [x]"),
              QByteArray("payload: [!<tag:yaml.org,2002:made-up> x]"),
              QByteArray("payload: [null]"), QByteArray("payload: [")}) {
            check(
                !good(infra::expandProviders(configuration(), {{"rule-providers:a", invalid}})),
                "duplicate keys, aliases, multiple documents, tags and malformed payload rejected");
        }
        auto nested = QByteArray("payload: ") + QByteArray(40, '[') + "x" + QByteArray(40, ']');
        check(!good(infra::expandProviders(configuration(), {{"rule-providers:a", nested}})),
              "depth bounded");
        check(!good(infra::expandProviders(configuration(), {})), "missing provider rejected");
        check(!good(infra::expandProviders(
                  configuration(), {{"rule-providers:a", QByteArray(4 * 1024 * 1024 + 1, 'x')}})),
              "expander independently checks item size");
        QByteArray many = "payload: [";
        for (int i = 0; i < 20000; ++i)
            many += (i ? ", 'DOMAIN,x.invalid'" : "'DOMAIN,x.invalid'");
        many += ']';
        auto exactSource = configuration();
        exactSource.replace(", 'MATCH,DIRECT'", "");
        check(good(infra::expandProviders(exactSource, {{"rule-providers:a", many}})),
              "exact 20000 rules");
        check(!good(infra::expandProviders(configuration(), {{"rule-providers:a", many}})),
              "inline plus provider cumulative rule limit");
        auto repeated = configuration("domain", "text");
        repeated.replace("'RULE-SET,a,Main'",
                         QByteArray("'RULE-SET,a,Main', ").repeated(8) + "'RULE-SET,a,Main'");
        const auto large = (QByteArray(1000, 'a') + ".invalid\n").repeated(2000);
        check(!good(infra::expandProviders(repeated, {{"rule-providers:a", large}})),
              "expanded byte limit rejects during output despite small input and rule count");
        auto emptyReferences = configuration("domain", "text");
        emptyReferences.replace("'RULE-SET,a,Main'",
                                QByteArray("'RULE-SET,a,Main', ").repeated(99) +
                                    "'RULE-SET,a,Main'");
        const auto comments = (QByteArray("#") + QByteArray(7999, 'a') + '\n').repeated(500);
        check(!good(infra::expandProviders(emptyReferences, {{"rule-providers:a", comments}})),
              "cumulative rescan work bounded even for empty repeated providers");
        check(good(store.writeBytes("data/old.yaml", "old", true)), "old fixture persisted");
        {
            infra::ProviderStage stage(store);
            check(good(stage.begin("rule-providers:a")) && feed(stage, "bad") &&
                      good(stage.finish()),
                  "bad candidate staged");
            QBuffer input;
            input.setData(configuration());
            input.open(QIODevice::ReadOnly);
            auto temporary = store.beginConfigTemporary();
            auto output = std::get<std::unique_ptr<QTemporaryFile>>(std::move(temporary));
            check(!good(infra::expandProviderFiles(input, stage.devices(), *output)),
                  "file path rejects bad candidate");
        }
        check(std::get<QByteArray>(store.readBytes("data/old.yaml")) == "old",
              "failed candidate leaves old file");
        clean(directory.path());
        std::cout << "PASS: provider staging, file streaming, order, bounds and rejection\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
