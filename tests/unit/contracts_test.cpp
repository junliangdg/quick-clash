#include "application/ports.h"
#include "infra/config/config_preflight.h"
#include <QCoreApplication>
#include <QFile>
#include <iostream>
#include <limits>
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
void expectError(const QByteArray& input, const char* code) {
    const auto result = quickclash::infra::preflightConfig(input, {});
    check(std::holds_alternative<quickclash::domain::Error>(result), "input must fail");
    check(std::get<quickclash::domain::Error>(result).code == QString::fromLatin1(code), code);
}
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    using namespace quickclash::domain;
    try {
        const ConfigHistory history;
        check(!history.activeRevision && !history.lastGoodRevision,
              "first run has no confirmed history");
        const RuntimePreferences preferences;
        check(!preferences.autoStart && !preferences.tunDesired && !preferences.systemProxyDesired,
              "safe defaults");
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
        expectError(fixture("provider_unsupported.yaml"), "UnsupportedConfigFeature");
        expectError(fixture("geo_unsupported.yaml"), "UnsupportedConfigFeature");
        expectError(fixture("fake_ip_unsupported.yaml"), "UnsupportedConfigFeature");
        expectError(fixture("invalid.yaml"), "InvalidYaml");
        expectError("proxies: []\nproxies: []", "DuplicateConfigKey");
        expectError("external-controller-pipe: arbitrary", "UnsupportedConfigFeature");
        expectError("proxies: &nodes []\nproxy-groups: *nodes", "UnsupportedConfigFeature");
        expectError(QByteArray(40, '[') + "0" + QByteArray(40, ']'), "ConfigLimitExceeded");
        expectError(QByteArray(16 * 1024 * 1024 + 1, ' '), "ConfigLimitExceeded");
        std::cout << "PASS: domain defaults, stale contexts, operation transitions, config subset "
                     "and sessions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
