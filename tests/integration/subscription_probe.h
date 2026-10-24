#pragma once
#include "application/desktop_runtime.h"
namespace quickclash::test {
// Explicit opt-in only; private URL is read from stdin, no raw state in diagnostics.
int subscriptionProbe(application::DesktopRuntime& runtime, infra::JsonStore& store);
} // namespace quickclash::test
