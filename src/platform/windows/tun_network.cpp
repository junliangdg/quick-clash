#include "platform/windows/tun_network.h"
#include "infra/storage/json_store.h"
#include <QJsonArray>
#include <algorithm>
// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
// clang-format on

namespace quickclash::platform {
namespace {
bool recordedAdapter(const TunAdapter& adapter, const QJsonObject& record) {
    return !adapter.guid.isEmpty() && !adapter.luid.isEmpty() &&
           record.value(QStringLiteral("guid")) == adapter.guid &&
           record.value(QStringLiteral("luid")) == adapter.luid &&
           record.value(QStringLiteral("name")) == adapter.name;
}
} // namespace
domain::Result<TunNetwork> inspectTunNetwork() {
    ULONG size = 16384;
    QByteArray buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        if (!size || size > 4 * 1024 * 1024)
            return infra::storageError("ServiceNetworkInspectionFailed");
        buffer.resize(static_cast<qsizetype>(size));
        result =
            GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_ALL_INTERFACES, nullptr,
                                 reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR)
        return infra::storageError("ServiceNetworkInspectionFailed");
    TunNetwork network;
    for (auto* item = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); item;
         item = item->Next) {
        MIB_IF_ROW2 row{};
        row.InterfaceLuid = item->Luid;
        if (GetIfEntry2(&row) != NO_ERROR)
            return infra::storageError("ServiceNetworkInspectionFailed");
        TunAdapter adapter;
        adapter.name = QString::fromWCharArray(item->FriendlyName);
        adapter.guid = QString::fromLatin1(item->AdapterName).toLower();
        adapter.luid = QString::number(item->Luid.Value);
        adapter.up = item->OperStatus == IfOperStatusUp;
        adapter.virtualInterface = !row.InterfaceAndOperStatusFlags.HardwareInterface &&
                                   item->IfType != IF_TYPE_SOFTWARE_LOOPBACK;
        for (auto* dns = item->FirstDnsServerAddress; dns; dns = dns->Next) {
            wchar_t address[128]{};
            const auto* socket = dns->Address.lpSockaddr;
            const auto* ip = socket->sa_family == AF_INET
                                 ? static_cast<const void*>(
                                       &reinterpret_cast<const sockaddr_in*>(socket)->sin_addr)
                                 : static_cast<const void*>(
                                       &reinterpret_cast<const sockaddr_in6*>(socket)->sin6_addr);
            if (!InetNtopW(socket->sa_family, ip, address, 128))
                return infra::storageError("ServiceNetworkInspectionFailed");
            adapter.dnsServers.append(QString::fromWCharArray(address));
        }
        network.adapters.append(adapter);
    }
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    if (GetIpForwardTable2(AF_UNSPEC, &table) != NO_ERROR)
        return infra::storageError("ServiceNetworkInspectionFailed");
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto& route = table->Table[i];
        // /0 and split-default /1 cover broad traffic; ordinary split VPN routes do not.
        if (route.DestinationPrefix.PrefixLength > 1)
            continue;
        for (auto& adapter : network.adapters)
            if (adapter.luid == QString::number(route.InterfaceLuid.Value)) {
                const auto* address = reinterpret_cast<const unsigned char*>(
                    route.DestinationPrefix.Prefix.si_family == AF_INET
                        ? static_cast<const void*>(&route.DestinationPrefix.Prefix.Ipv4.sin_addr)
                        : static_cast<const void*>(&route.DestinationPrefix.Prefix.Ipv6.sin6_addr));
                const auto bytes = route.DestinationPrefix.Prefix.si_family == AF_INET ? 4 : 16;
                adapter.defaultRoutes.append(
                    QStringLiteral("%1:%2/%3")
                        .arg(route.DestinationPrefix.Prefix.si_family)
                        .arg(QString::fromLatin1(
                            QByteArray(reinterpret_cast<const char*>(address), bytes).toHex()))
                        .arg(route.DestinationPrefix.PrefixLength));
            }
    }
    FreeMibTable(table);
    return network;
}
QString availableTunName(const QString& preferred, const QString& installationId,
                         const TunNetwork& network, const QJsonObject& reusable) {
    const auto available = [&network](const QString& name) {
        return std::none_of(network.adapters.cbegin(), network.adapters.cend(),
                            [&name](const auto& adapter) {
                                return adapter.name.compare(name, Qt::CaseInsensitive) == 0;
                            });
    };
    if (!preferred.isEmpty() && available(preferred))
        return preferred;
    for (const auto& adapter : network.adapters)
        if (adapter.name == preferred && recordedAdapter(adapter, reusable) && !adapter.up &&
            adapter.defaultRoutes.isEmpty() && adapter.dnsServers.isEmpty())
            return preferred;
    const auto base = QStringLiteral("QuickClash-") + installationId.first(8);
    for (int i = 0; i < 16; ++i) {
        const auto name = i ? base + QStringLiteral("-%1").arg(i) : base;
        if (available(name))
            return name;
    }
    return {};
}
bool conflictingTunRoute(const TunNetwork& network, const QString& ownedGuid) {
    return std::any_of(
        network.adapters.cbegin(), network.adapters.cend(), [&ownedGuid](const auto& adapter) {
            return adapter.up && adapter.virtualInterface && !adapter.defaultRoutes.isEmpty() &&
                   (ownedGuid.isEmpty() || adapter.guid != ownedGuid);
        });
}
bool tunRecordClean(const QJsonObject& record, const TunNetwork& network) {
    if (record.isEmpty())
        return true;
    const auto guid = record.value(QStringLiteral("guid")).toString();
    const auto name = record.value(QStringLiteral("name")).toString();
    for (const auto& adapter : network.adapters) {
        if (!guid.isEmpty() ? adapter.guid != guid
                            : adapter.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        // A pending create without a GUID has ambiguous ownership after a crash.
        if (guid.isEmpty() || adapter.luid != record.value(QStringLiteral("luid")).toString() ||
            adapter.up || !adapter.defaultRoutes.isEmpty() || !adapter.dnsServers.isEmpty())
            return false;
    }
    return true;
}
QJsonObject observedTunRecord(const QString& name, const TunNetwork& before,
                              const TunNetwork& after, const QJsonObject& reusable) {
    for (const auto& adapter : after.adapters) {
        const bool ipv4Default = adapter.defaultRoutes.contains(QStringLiteral("2:00000000/0")) ||
                                 (adapter.defaultRoutes.contains(QStringLiteral("2:00000000/1")) &&
                                  adapter.defaultRoutes.contains(QStringLiteral("2:80000000/1")));
        if (adapter.name != name || !adapter.up || !ipv4Default || adapter.dnsServers.isEmpty())
            continue;
        if (std::any_of(before.adapters.cbegin(), before.adapters.cend(),
                        [&adapter, &reusable](const auto& old) {
                            const bool same =
                                old.guid == adapter.guid || old.luid == adapter.luid ||
                                old.name.compare(adapter.name, Qt::CaseInsensitive) == 0;
                            const bool ours = recordedAdapter(old, reusable) &&
                                              recordedAdapter(adapter, reusable) && !old.up &&
                                              old.defaultRoutes.isEmpty() &&
                                              old.dnsServers.isEmpty();
                            return same && !ours;
                        }))
            return {};
        return {{QStringLiteral("name"), name},
                {QStringLiteral("guid"), adapter.guid},
                {QStringLiteral("luid"), adapter.luid},
                {QStringLiteral("routes"), QJsonArray::fromStringList(adapter.defaultRoutes)},
                {QStringLiteral("dns"), QJsonArray::fromStringList(adapter.dnsServers)}};
    }
    return {};
}
} // namespace quickclash::platform
