#pragma once
#include <QString>
namespace quickclash::test {
bool nativeProxyRequest(const QString& host, quint16 port);
bool terminateOwnedFixture(quint32 pid);
} // namespace quickclash::test
