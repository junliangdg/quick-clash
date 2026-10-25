#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
namespace quickclash::test {
struct SubscriptionProbeRuntime {
    std::function<QJsonObject()> snapshot;
    std::function<void(int, int)> ports;
    std::function<void(const QString&)> subscribe;
    std::function<void()> start;
    std::function<void(const QString&)> update;
    std::function<void()> stop;
};
int subscriptionMemoryProbe();
int subscriptionMemoryProbe(const SubscriptionProbeRuntime& client);
} // namespace quickclash::test
