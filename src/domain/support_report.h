#pragma once
#include <QJsonObject>

namespace quickclash::domain {
// Deliberate allowlist: arbitrary runtime strings, log payloads and extension fields never pass.
[[nodiscard]] QJsonObject supportReport(const QJsonObject& runtime);
} // namespace quickclash::domain
