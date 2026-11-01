#include "application/desktop_runtime.h"
#include "domain/resource_snapshot.h"

namespace quickclash::application {
void DesktopRuntime::sampleResources() {
    if (pendingFeatureRequests_.contains(QStringLiteral("resources")))
        return;
    const auto host = resources_->host();
    const bool validatorExpected = validator_->running();
    const auto validator = validator_->memory();
    const auto commit = [this](const QJsonObject& snapshot) {
        for (auto it = snapshot.begin(); it != snapshot.end(); ++it)
            featureState_.insert(it.key(), it.value());
        featureState_.insert(QStringLiteral("memorySample"), clock_().toString(Qt::ISODateWithMs));
        publish();
    };
    if (!engine_.usingService()) {
        commit(domain::resourceSnapshot(host, resources_->service(), engine_.memory(),
                                        engine_.running(), validator, validatorExpected,
                                        coreArchive_.memory(), coreArchive_.running()));
        return;
    }
    // Publish known local values immediately; a failed service response cannot retain old core
    // data.
    commit(domain::resourceSnapshot(host, {}, {}, engine_.pid() != 0, validator, validatorExpected,
                                    coreArchive_.memory(), coreArchive_.running()));
    const auto ticket = beginFeatureRequest(QStringLiteral("resources"));
    engine_.serviceResources([this, ticket](domain::Result<QJsonObject> result) {
        if (!acceptFeatureRequest(ticket) || !engine_.usingService())
            return;
        QJsonObject serviceObservation;
        QJsonObject core;
        if (const auto* value = std::get_if<QJsonObject>(&result)) {
            serviceObservation = {
                {QStringLiteral("service"), value->value(QStringLiteral("service"))},
                {QStringLiteral("validator"), value->value(QStringLiteral("validator"))},
                {QStringLiteral("validatorExpected"),
                 value->value(QStringLiteral("validatorExpected"))},
                {QStringLiteral("complete"), value->value(QStringLiteral("complete"))}};
            core = value->value(QStringLiteral("core")).toObject();
            if (engine_.pid() &&
                core.value(QStringLiteral("pid")).toString() != QString::number(engine_.pid())) {
                core = {};
                serviceObservation.insert(QStringLiteral("complete"), false);
            }
        }
        const auto snapshot = domain::resourceSnapshot(
            resources_->host(), serviceObservation, core, engine_.pid() != 0, validator_->memory(),
            validator_->running(), coreArchive_.memory(), coreArchive_.running());
        for (auto it = snapshot.begin(); it != snapshot.end(); ++it)
            featureState_.insert(it.key(), it.value());
        featureState_.insert(QStringLiteral("memorySample"), clock_().toString(Qt::ISODateWithMs));
        publish();
    });
}
} // namespace quickclash::application
