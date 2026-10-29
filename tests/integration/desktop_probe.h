#pragma once
#include <QString>

namespace quickclash {
class ApplicationHost;
namespace test {
// An explicit marker is required before using a persistent isolated store.
QString desktopProbeSettings(const QString& root);
int runDesktopProbe(ApplicationHost& host);
} // namespace test
} // namespace quickclash
