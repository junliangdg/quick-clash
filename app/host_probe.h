#pragma once
namespace quickclash {
class ApplicationHost;
// Isolated integration probe; no persistent settings, instance lock or system proxy.
int runHostProbe(ApplicationHost& host);
int runTrayProbe(ApplicationHost& host);
#ifdef QUICKCLASH_TEST_PROBES
int runKeyboardProbe(ApplicationHost& host);
int runResponsiveProbe(ApplicationHost& host);
#endif
} // namespace quickclash
