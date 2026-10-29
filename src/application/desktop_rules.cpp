#include "application/desktop_runtime.h"
#include "domain/rule_snapshot.h"
#include <QUrl>
#include <utility>

namespace quickclash::application {
namespace {
QString providerKey(const QString& kind, const QString& name) {
    return kind + QChar(0) + name;
}
} // namespace
void DesktopRuntime::requestPageData(const QString& stream) {
    if (!engine_.ready() || busy_ || !featureDemands_.contains(stream) ||
        pendingFeatureRequests_.contains(stream) ||
        (stream != QStringLiteral("rules") && stream != QStringLiteral("providers")))
        return;
    const auto ticket = beginFeatureRequest(stream);
    auto& state = stream == QStringLiteral("rules") ? rulePageState_ : providerPageState_;
    state.insert(QStringLiteral("loading"), true);
    publish();
    if (stream == QStringLiteral("rules")) {
        engine_.api("GET", "/rules", {}, [this, ticket](domain::Result<QJsonObject> result) {
            if (!ticket.matches(demandState_, epoch_, operationGeneration_,
                                featureRequests_.value(ticket.stream))) {
                (void)acceptFeatureRequest(ticket);
                return;
            }
            const auto parsed = std::holds_alternative<QJsonObject>(result)
                                    ? domain::ruleSnapshot(std::get<QJsonObject>(result))
                                    : domain::Result<QJsonArray>{std::get<domain::Error>(result)};
            if (const auto* failure = std::get_if<domain::Error>(&parsed)) {
                (void)acceptFeatureRequest(ticket);
                loadedFeatures_.insert(ticket.stream);
                rulePageState_ = {{QStringLiteral("stale"), true},
                                  {QStringLiteral("error"), failure->code}};
                publish();
                return;
            }
            // Fetch only reference metadata, never provider contents or unmanaged paths.
            engine_.api(
                "GET", "/providers/rules", {},
                [this, ticket,
                 rows = std::get<QJsonArray>(parsed)](domain::Result<QJsonObject> providers) {
                    if (!acceptFeatureRequest(ticket))
                        return;
                    loadedFeatures_.insert(ticket.stream);
                    const auto references =
                        domain::providerCategorySnapshot(providers, {}, QStringLiteral("rules"));
                    rules_ = std::get<QJsonArray>(
                        domain::ruleSnapshot({{QStringLiteral("rules"), rows}}, references.rows));
                    rulePageState_ = {{QStringLiteral("sampleAt"), clock_().toString(Qt::ISODate)},
                                      {QStringLiteral("error"), references.error}};
                    publish();
                });
        });
        return;
    }
    providerDraft_ = {};
    // Snapshot resources belong to the selected catalog revision and may be pending apply.
    for (const auto& value :
         profile(selectedId_).value(QStringLiteral("managedProviders")).toArray()) {
        const auto row = value.toObject();
        if (row.value(QStringLiteral("mode")) == QStringLiteral("snapshot"))
            providerDraft_.append(row);
    }
    engine_.api(
        "GET", "/providers/proxies", {}, [this, ticket](domain::Result<QJsonObject> result) {
            if (!ticket.matches(demandState_, epoch_, operationGeneration_,
                                featureRequests_.value(ticket.stream))) {
                (void)acceptFeatureRequest(ticket);
                return;
            }
            const auto proxies =
                domain::providerCategorySnapshot(result, providers_, QStringLiteral("proxies"));
            for (const auto& row : proxies.rows)
                providerDraft_.append(row);
            engine_.api(
                "GET", "/providers/rules", {},
                [this, ticket, proxyError = proxies.error](domain::Result<QJsonObject> result) {
                    if (!acceptFeatureRequest(ticket))
                        return;
                    loadedFeatures_.insert(ticket.stream);
                    const auto rules = domain::providerCategorySnapshot(result, providers_,
                                                                        QStringLiteral("rules"));
                    for (const auto& row : rules.rows)
                        providerDraft_.append(row);
                    providers_ = std::move(providerDraft_);
                    providerDraft_ = {};
                    QJsonObject retained;
                    for (const auto& value : providers_) {
                        const auto row = value.toObject();
                        const auto key = providerKey(row.value(QStringLiteral("kind")).toString(),
                                                     row.value(QStringLiteral("name")).toString());
                        if (providerResults_.contains(key))
                            retained.insert(key, providerResults_.value(key));
                    }
                    providerResults_ = retained;
                    decorateProviderResults();
                    providerPageState_ = {
                        {QStringLiteral("sampleAt"), clock_().toString(Qt::ISODate)},
                        {QStringLiteral("proxyError"), proxyError},
                        {QStringLiteral("ruleError"), rules.error},
                        {QStringLiteral("stale"), !proxyError.isEmpty() || !rules.error.isEmpty()}};
                    publish();
                });
        });
}
void DesktopRuntime::decorateProviderResults() {
    for (qsizetype index = 0; index < providers_.size(); ++index) {
        auto row = providers_[index].toObject();
        const auto key = providerKey(row.value(QStringLiteral("kind")).toString(),
                                     row.value(QStringLiteral("name")).toString());
        if (providerResults_.contains(key)) {
            const auto result = providerResults_.value(key).toObject();
            row.insert(QStringLiteral("lastResult"), result.value(QStringLiteral("result")));
            row.insert(QStringLiteral("lastError"), result.value(QStringLiteral("error")));
        }
        providers_[index] = row;
    }
}
void DesktopRuntime::updateProvider(const QString& kind, const QString& name) {
    if (pendingFeatureRequests_.contains(QStringLiteral("providers")))
        return;
    bool present = false;
    for (const auto& value : providers_) {
        const auto row = value.toObject();
        present |=
            row.value(QStringLiteral("kind")) == kind && row.value(QStringLiteral("name")) == name;
    }
    if (!present)
        return;
    const auto key = providerKey(kind, name);
    providerResults_.insert(key,
                            QJsonObject{{QStringLiteral("result"), QStringLiteral("Updating")}});
    decorateProviderResults();
    if (kind == QStringLiteral("proxy-providers") || kind == QStringLiteral("rule-providers")) {
        snapshotProviderRefresh_ = {
            {QStringLiteral("kind"), kind},
            {QStringLiteral("name"), name},
            {QStringLiteral("profileId"), selectedId_},
            {QStringLiteral("demand"),
             QString::number(demandState_.generations.value(QStringLiteral("providers")))}};
        lastError_.clear();
        refreshSnapshotProvider(kind, name);
        publish();
        return;
    }
    if (kind != QStringLiteral("proxies") && kind != QStringLiteral("rules"))
        return;
    busy_ = true;
    publish();
    const auto demand = demandState_.generations.value(QStringLiteral("providers"));
    engine_.api("PUT", "/providers/" + kind.toUtf8() + '/' + QUrl::toPercentEncoding(name), {},
                [this, key, demand, epoch = epoch_,
                 generation = operationGeneration_](domain::Result<QJsonObject> result) {
                    if (epoch != epoch_ || generation != operationGeneration_)
                        return;
                    busy_ = false;
                    lastError_.clear();
                    QString code;
                    if (const auto* failure = std::get_if<domain::Error>(&result)) {
                        code = failure->code == QStringLiteral("CoreRequestTimeout") ||
                                       failure->code == QStringLiteral("ServiceTimeout")
                                   ? QStringLiteral("ProviderRefreshUncertain")
                                   : failure->code;
                    }
                    if (demandState_.accepts(QStringLiteral("providers"), demand)) {
                        providerResults_.insert(
                            key, QJsonObject{{QStringLiteral("result"),
                                              code.isEmpty() ? QStringLiteral("Updated")
                                              : code == QStringLiteral("ProviderRefreshUncertain")
                                                  ? QStringLiteral("Uncertain")
                                                  : QStringLiteral("Failed")},
                                             {QStringLiteral("error"), code}});
                        decorateProviderResults();
                        requestPageData(QStringLiteral("providers"));
                    }
                    if (!code.isEmpty())
                        error(code);
                    engine_.refreshGroups();
                    publish();
                });
}
void DesktopRuntime::finishSnapshotProviderRefresh() {
    if (snapshotProviderRefresh_.isEmpty() || busy_)
        return;
    const auto operation = std::exchange(snapshotProviderRefresh_, {});
    if (operation.value(QStringLiteral("profileId")) != selectedId_ ||
        !demandState_.accepts(QStringLiteral("providers"),
                              operation.value(QStringLiteral("demand")).toString().toULongLong()))
        return;
    const auto pending = profile(selectedId_).value(QStringLiteral("pendingApply")).toBool();
    providerResults_.insert(
        providerKey(operation.value(QStringLiteral("kind")).toString(),
                    operation.value(QStringLiteral("name")).toString()),
        QJsonObject{{QStringLiteral("result"), !lastError_.isEmpty() ? QStringLiteral("Failed")
                                               : pending ? QStringLiteral("PendingApply")
                                                         : QStringLiteral("Updated")},
                    {QStringLiteral("error"), lastError_}});
    decorateProviderResults();
    loadedFeatures_.remove(QStringLiteral("providers"));
}
} // namespace quickclash::application
