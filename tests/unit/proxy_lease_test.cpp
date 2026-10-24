#include "platform/windows/system_proxy.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

namespace {
class Settings final : public quickclash::platform::ProxySettings {
  public:
    QJsonObject value{{QStringLiteral("flags"), 13},
                      {QStringLiteral("server"), QStringLiteral("old:8888")},
                      {QStringLiteral("bypass"), QStringLiteral("old.local;*.example")},
                      {QStringLiteral("pac"), QStringLiteral("https://pac.example/config")}};
    int writes = 0;
    bool fail = false;
    quickclash::domain::Result<QJsonObject> read() override {
        return value;
    }
    quickclash::domain::CommandResult write(const QJsonObject& next) override {
        ++writes;
        if (fail)
            return quickclash::infra::storageError("InjectedWriteFailure");
        value = next;
        return std::monostate{};
    }
};
bool passed(const quickclash::domain::CommandResult& result) {
    return std::holds_alternative<std::monostate>(result);
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    quickclash::infra::JsonStore store(directory.path());
    if (!passed(store.open()))
        return 1;
    Settings settings;
    const auto original = settings.value;
    quickclash::platform::ProxyLease lease(store, settings);
    if (!passed(lease.acquire(17890, QStringLiteral("epoch1"))) || !lease.active() ||
        settings.value.value("flags").toInt() != 3)
        return 2;
    if (!passed(lease.acquire(17890, QStringLiteral("epoch1"))) || settings.writes != 1)
        return 3;
    if (!passed(lease.suspend()) || settings.value != original ||
        !passed(lease.acquire(17891, QStringLiteral("epoch2"))))
        return 4;
    if (!passed(lease.release()) || settings.value != original || !passed(lease.release()))
        return 5;
    if (!passed(lease.acquire(17892, QStringLiteral("epoch3"))))
        return 6;
    settings.value.insert(QStringLiteral("server"), QStringLiteral("third-party:9999"));
    const auto external = settings.value;
    if (passed(lease.release()) || settings.value != external || !lease.conflict() ||
        passed(lease.acquire(17892, QStringLiteral("epoch3"))))
        return 7;
    // A new process must neither overwrite an external change nor claim that it recovered it.
    quickclash::platform::ProxyLease reopened(store, settings);
    if (passed(reopened.release()) || settings.value != external)
        return 8;
    // Simulate settings still owned after a process death, then reconcile exactly once.
    const auto loaded = store.read(QStringLiteral("data/leases/windows_lan.json"));
    settings.value = std::get<std::optional<QJsonObject>>(loaded)->value("applied").toObject();
    if (!passed(reopened.release()) || settings.value != original)
        return 9;
    settings.fail = true;
    if (passed(lease.acquire(17893, QStringLiteral("epoch4"))) || settings.value != original)
        return 10;
    settings.fail = false;
    if (!passed(reopened.release()) || settings.value != original)
        return 11;
    if (!passed(lease.acquire(17894, QStringLiteral("epoch5"))))
        return 12;
    settings.value = external;
    if (passed(lease.release()) || !passed(lease.reapply(17895, QStringLiteral("epoch6"))) ||
        !passed(lease.release()) || settings.value != external)
        return 14;
    settings.value = original;
    if (!passed(lease.acquire(17896, QStringLiteral("epoch7"))))
        return 15;
    store.freeze();
    // Journal failure must be reported, but cannot prevent an exactly owned restore.
    if (passed(lease.release()) || settings.value != original || lease.active())
        return 13;
    std::cout
        << "Lease intent, idempotency, suspend/migration, PAC/bypass restoration, external "
           "conflict, interrupted recovery and write failure passed. OS settings untouched.\n";
    return 0;
}
