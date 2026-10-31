#include "platform/windows/tun_network.h"
#include <QCoreApplication>
#include <iostream>

using namespace quickclash::platform;
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    TunNetwork before;
    before.adapters.append({"QuickClash", "third_party", "10", true, true, {}, {}});
    before.adapters.append({"QuickClash-12345678", "other", "11", false, true, {}, {}});
    before.adapters.append(
        {"Meta", "third_party_meta", "20", true, true, {"2:00000000/0"}, {"198.18.0.1"}});
    if (!conflictingTunRoute(before) || !observedTunRecord("Meta", before, before).isEmpty() ||
        !tunRecordClean({{"name", "Meta"}, {"guid", "ours"}, {"luid", "12"}}, before))
        return 11; // Meta is third party; its name never grants ownership or blocks own cleanup.
    before.adapters.removeLast();
    if (availableTunName("QuickClash-12345678", "12345678abcdef", before) !=
            "QuickClash-12345678-1" ||
        conflictingTunRoute(before) || !tunRecordClean({}, before))
        return 1;
    before.adapters[0].defaultRoutes = {"2:00000000/0"};
    if (!conflictingTunRoute(before))
        return 2;
    before.adapters[0].virtualInterface = false;
    if (conflictingTunRoute(before))
        return 3;
    auto after = before;
    after.adapters.append({"QuickClash-12345678-1",
                           "ours",
                           "12",
                           true,
                           true,
                           {"2:00000000/1", "2:80000000/1"},
                           {"198.18.0.1"}});
    auto record = observedTunRecord("QuickClash-12345678-1", before, after);
    if (record.value("guid") != "ours" || tunRecordClean(record, after) ||
        !observedTunRecord("QuickClash", before, after).isEmpty() ||
        !tunRecordClean(record, before) || conflictingTunRoute(after, "ours"))
        return 4;
    after.adapters.last().up = false;
    after.adapters.last().defaultRoutes = {};
    if (tunRecordClean(record, after))
        return 5; // DNS remains on our interface.
    after.adapters.last().dnsServers = {};
    if (!tunRecordClean(record, after))
        return 6;
    if (availableTunName("QuickClash-12345678-1", "12345678abcdef", after, record) !=
        "QuickClash-12345678-1")
        return 12; // A proven, clean, persistent own interface keeps its stable name.
    auto restarted = after;
    restarted.adapters.last().up = true;
    restarted.adapters.last().defaultRoutes = {"2:00000000/0"};
    restarted.adapters.last().dnsServers = {"198.18.0.1"};
    if (observedTunRecord("QuickClash-12345678-1", after, restarted, record).isEmpty() ||
        !observedTunRecord("QuickClash-12345678-1", after, restarted).isEmpty())
        return 13;
    after.adapters.last().luid = "99";
    if (tunRecordClean(record, after))
        return 7;
    if (tunRecordClean({{"name", "QuickClash"}}, before))
        return 8; // Ambiguous pending creation never adopts by name.
    for (int i = 1; i < 16; ++i)
        before.adapters.append(
            {QStringLiteral("QuickClash-12345678-%1").arg(i), {}, {}, false, false, {}, {}});
    if (!availableTunName({}, "12345678abcdef", before).isEmpty())
        return 9;
    const auto inspected = inspectTunNetwork();
    if (const auto* error = std::get_if<quickclash::domain::Error>(&inspected)) {
        std::cerr << error->code.toStdString();
        return 10;
    }
    std::cout
        << "TUN name, identity, route, DNS cleanup policies and native snapshot passed; adapters="
        << std::get<TunNetwork>(inspected).adapters.size()
        << " default_route_conflict=" << conflictingTunRoute(std::get<TunNetwork>(inspected))
        << "\n";
    return 0;
}
