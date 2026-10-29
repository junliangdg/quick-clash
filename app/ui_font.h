#pragma once
#include <QFont>

namespace quickclash {
// Logical pixels only: Qt applies the screen's device pixel ratio separately.
[[nodiscard]] QFont platformUiFont();
} // namespace quickclash
