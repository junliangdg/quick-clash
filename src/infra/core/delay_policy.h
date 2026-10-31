#pragma once

namespace quickclash::infra {
// A usable node should answer within a few hundred milliseconds. Two seconds leaves room
// for slow but valid nodes while cutting the all-timeout tail of a large batch.
inline constexpr int kNodeDelayTimeoutMs = 2000;
} // namespace quickclash::infra
