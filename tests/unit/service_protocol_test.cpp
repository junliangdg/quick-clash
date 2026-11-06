#include "infra/core/delay_policy.h"
#include "infra/core/proxy_catalog.h"
#include "infra/core/service_protocol.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <iostream>

using quickclash::infra::ServiceProtocol;
namespace {
QJsonObject request(int id, QString action = QStringLiteral("stop")) {
    return {{QStringLiteral("protocolVersion"), 1},
            {QStringLiteral("requestId"), QString::number(id)},
            {QStringLiteral("sessionId"), QStringLiteral("session")},
            {QStringLiteral("engineEpoch"), QStringLiteral("epoch")},
            {QStringLiteral("action"), action},
            {QStringLiteral("payload"), QJsonObject{}}};
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto valid = request(1);
    for (const auto& field : valid.keys()) {
        auto bad = valid;
        bad.remove(field);
        if (ServiceProtocol::validEnvelope(bad))
            return 1;
        bad = valid;
        bad.insert(field, QJsonArray{});
        if (ServiceProtocol::validEnvelope(bad))
            return 2;
    }
    for (const QJsonValue value :
         {QJsonValue{QStringLiteral("01")}, QJsonValue{0}, QJsonValue{QStringLiteral("0")},
          QJsonValue{1.5}, QJsonValue{QStringLiteral("18446744073709551616")}}) {
        auto bad = valid;
        bad.insert(QStringLiteral("requestId"), value);
        if (ServiceProtocol::validEnvelope(bad))
            return 3;
    }
    auto bad = valid;
    bad.insert(QStringLiteral("protocolVersion"), 1.5);
    if (ServiceProtocol::validEnvelope(bad))
        return 4;
    bad = valid;
    bad.insert(QStringLiteral("executable"), QStringLiteral("evil.exe"));
    if (ServiceProtocol::validEnvelope(bad))
        return 5;
    ServiceProtocol protocol;
    using Kind = ServiceProtocol::Disposition;
    if (protocol.accept(valid).disposition != Kind::Execute ||
        protocol.accept(valid).disposition != Kind::Pending)
        return 6;
    QJsonObject response{
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("requestId"), QStringLiteral("1")},
        {QStringLiteral("result"), QJsonObject{{QStringLiteral("stopped"), true}}}};
    protocol.complete(QStringLiteral("1"), response);
    protocol.complete(QStringLiteral("1"), {});
    const auto replay = protocol.accept(valid);
    if (replay.disposition != Kind::Cached || replay.response != response)
        return 7; // Lost responses return the exact result, no second mutation.
    if (protocol.accept(request(1, QStringLiteral("start"))).disposition != Kind::Disconnect)
        return 8;
    for (int i = 2; i <= quickclash::infra::kServiceRequestLimit + 1; ++i)
        if (protocol.accept(request(i)).disposition != Kind::Execute)
            return 9;
    if (protocol.accept(request(quickclash::infra::kServiceRequestLimit + 2)).disposition !=
        Kind::Reject)
        return 10;
    for (int i = 2; i <= quickclash::infra::kServiceRequestLimit + 1; ++i)
        protocol.complete(QString::number(i), response);
    for (int i = quickclash::infra::kServiceRequestLimit + 2;
         i <= quickclash::infra::kServiceRequestLimit + 36; ++i) {
        if (protocol.accept(request(i)).disposition != Kind::Execute)
            return 11;
        protocol.complete(QString::number(i), response);
    }
    if (protocol.accept(valid).disposition != Kind::Reject)
        return 12; // Eviction cannot permit an old ID to mutate again.
    {
        ServiceProtocol bounded;
        const auto largeResponse = [](int id, qsizetype size) {
            return QJsonObject{{"protocolVersion", 1},
                               {"requestId", QString::number(id)},
                               {"result", QJsonObject{{"text", QString(size, QLatin1Char('x'))}}}};
        };
        for (int id = 1; id <= 5; ++id) {
            const auto value = largeResponse(id, 1024 * 1024);
            if (bounded.accept(request(id, "groups")).disposition != Kind::Execute)
                return 20;
            bounded.complete(QString::number(id), value);
            const auto cached = bounded.accept(request(id, "groups"));
            if (cached.disposition != Kind::Cached || cached.response != value)
                return 21;
        }
        if (bounded.accept(request(1, "groups")).disposition != Kind::Reject ||
            bounded.accept(request(2, "groups")).disposition != Kind::Reject)
            return 22; // Byte eviction happens before the 32-entry limit.
        if (bounded.accept(request(6)).disposition != Kind::Execute)
            return 23;
        bounded.complete("6", largeResponse(6, quickclash::infra::kServiceReplayByteLimit));
        if (bounded.accept(request(6)).disposition != Kind::Reject ||
            bounded.accept(request(5, "groups")).disposition != Kind::Cached)
            return 24; // Oversized delivery neither repeats nor clears unrelated cached results.
        bounded.clear();
        if (bounded.accept(request(1)).disposition != Kind::Execute)
            return 25;
        bounded.complete("1", response);
        if (bounded.accept(request(1)).disposition != Kind::Cached)
            return 26;
    }
    {
        const auto catalog = quickclash::infra::proxySelectionCatalog(
            {{"choose", QJsonObject{{"type", "Selector"},
                                    {"all", QJsonArray{"DIRECT", "node"}},
                                    {"now", "node"},
                                    {"history", QJsonArray{QJsonObject{{"delay", 42}}}}}},
             {"node", QJsonObject{{"type", "HTTP"}, {"extra", QJsonObject{{"large", 1}}}}},
             {"DIRECT", QJsonObject{{"type", "Direct"}}}});
        if (catalog.size() != 3 || !catalog.contains("node") || !catalog.contains("DIRECT") ||
            !catalog.value("node").toObject().isEmpty() ||
            catalog.value("choose").toObject() !=
                QJsonObject{{"type", "Selector"}, {"all", QJsonArray{"DIRECT", "node"}}})
            return 27;
    }
    {
        const QJsonObject catalog{{"node /?", QJsonObject{}}};
        for (const auto& target : {QStringLiteral("http://fixture.invalid/health?x=1"),
                                   QStringLiteral("https://fixture.invalid/health")}) {
            const auto path = quickclash::infra::serviceDelayPath(
                {{"name", "node /?"}, {"url", target}}, catalog);
            if (!path || !path->startsWith("/proxies/node%20%2F%3F/delay?") ||
                QUrlQuery(QUrl(QStringLiteral("http://localhost") + QString::fromLatin1(*path)))
                        .queryItemValue("url", QUrl::FullyDecoded) != target)
                return 28;
        }
        for (const auto& target :
             {QStringLiteral("http:///missing-host"), QStringLiteral("ftp://fixture.invalid/"),
              QStringLiteral("http://user@fixture.invalid/"),
              QStringLiteral("http://fixture.invalid/#fragment"),
              QStringLiteral("https://fixture.invalid/") + QString(8192, 'x')})
            if (quickclash::infra::serviceDelayPath({{"name", "node /?"}, {"url", target}},
                                                    catalog))
                return 29;
        if (quickclash::infra::serviceDelayPath(
                {{"name", "unknown"}, {"url", "http://fixture.invalid/"}}, catalog) ||
            quickclash::infra::serviceDelayPath({{"name", "node /?"}, {"url", 12}}, catalog))
            return 30;
    }
    if (!ServiceProtocol::validResponse(response))
        return 13;
    response.insert(QStringLiteral("error"), QStringLiteral("error"));
    if (ServiceProtocol::validResponse(response))
        return 14;
    const auto capabilities = ServiceProtocol::capabilities(QStringLiteral("test-build"));
    if (!ServiceProtocol::capabilityError(capabilities, QStringLiteral("test-build")).isEmpty() ||
        ServiceProtocol::capabilityError(capabilities, QStringLiteral("new-build")) !=
            QStringLiteral("ServiceProtocolMismatch") ||
        ServiceProtocol::capabilityError(capabilities, {}) !=
            QStringLiteral("ServiceProtocolMismatch"))
        return 27;
    auto incompatible = capabilities;
    incompatible.remove(QStringLiteral("modePatch"));
    incompatible.remove(QStringLiteral("trafficStream"));
    if (ServiceProtocol::capabilityError(incompatible, QStringLiteral("test-build")) !=
        QStringLiteral("ServicePackageMismatch"))
        return 28;
    incompatible.remove(QStringLiteral("serviceBuild"));
    if (ServiceProtocol::capabilityError(incompatible, QStringLiteral("test-build")) !=
        QStringLiteral("ServiceProtocolMismatch"))
        return 29;
    if (capabilities.value(QStringLiteral("proxyRecovery")) != QStringLiteral("NotAvailable") ||
        capabilities.value(QStringLiteral("serviceBuild")) != QStringLiteral("test-build"))
        return 15;
    QTemporaryFile first;
    QTemporaryFile second;
    if (!first.open() || !second.open())
        return 18;
    first.write("first");
    second.write("second");
    first.flush();
    second.flush();
    if (ServiceProtocol::executableBuild(first.fileName()).isEmpty() ||
        ServiceProtocol::executableBuild(first.fileName()) ==
            ServiceProtocol::executableBuild(second.fileName()) ||
        !ServiceProtocol::executableBuild(QStringLiteral("missing-file")).isEmpty())
        return 19;
    QJsonObject portFailure{
        {"protocolVersion", 1},
        {"requestId", "42"},
        {"error", "CoreListenerAddressInUse"},
        {"detail",
         QJsonObject{{"code", "CoreListenerAddressInUse"}, {"port", 7890}, {"role", "mixed"}}}};
    if (!ServiceProtocol::validResponse(portFailure))
        return 16;
    auto detail = portFailure.value("detail").toObject();
    detail.insert("path", "not_allowed");
    portFailure.insert("detail", detail);
    if (ServiceProtocol::validResponse(portFailure))
        return 17;
    std::cout
        << "PASS: strict envelopes, response schema, bounded replay and capability downgrade\n";
    return 0;
}
