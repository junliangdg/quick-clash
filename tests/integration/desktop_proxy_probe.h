#pragma once
#include <QJsonObject>

namespace quickclash {
class ApplicationHost;
namespace test {
bool desktopProxyProbe(ApplicationHost& host, QJsonObject& result);
int desktopExternalProxyMutation();
} // namespace test
} // namespace quickclash
