#pragma once
#include "domain/contracts.h"
#include <QJsonObject>
#include <QList>
#include <QStringList>

namespace quickclash::platform {
struct TunAdapter {
    QString name;
    QString guid;
    QString luid;
    bool up = false;
    bool virtualInterface = false;
    QStringList defaultRoutes;
    QStringList dnsServers;
};
struct TunNetwork {
    QList<TunAdapter> adapters;
};
[[nodiscard]] domain::Result<TunNetwork> inspectTunNetwork();
[[nodiscard]] QString availableTunName(const QString& preferred, const QString& installationId,
                                       const TunNetwork& network, const QJsonObject& reusable = {});
[[nodiscard]] bool conflictingTunRoute(const TunNetwork& network, const QString& ownedGuid = {});
[[nodiscard]] bool tunRecordClean(const QJsonObject& record, const TunNetwork& network);
[[nodiscard]] QJsonObject observedTunRecord(const QString& name, const TunNetwork& before,
                                            const TunNetwork& after,
                                            const QJsonObject& reusable = {});
} // namespace quickclash::platform
