#pragma once
#include <QJsonObject>

namespace quickclash {
class ApplicationHost;
namespace test {
bool desktopProxyProbe(ApplicationHost& host, QJsonObject& result);
bool desktopProxyHoldProbe(ApplicationHost& host, QJsonObject& result);
bool desktopProxyReopenProbe(ApplicationHost& host, QJsonObject& result);
int desktopExternalProxyMutation();
} // namespace test
} // namespace quickclash
