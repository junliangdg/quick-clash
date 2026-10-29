#pragma once
#include <QJsonObject>
#include <QString>
namespace quickclash {
class ApplicationHost;
namespace test {
int desktopFlowServer(const QString& output);
int desktopFlowClient();
bool desktopFlowProbe(ApplicationHost& host, bool reopen, QJsonObject& result);
} // namespace test
} // namespace quickclash
