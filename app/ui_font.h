#pragma once
#include <QFont>

namespace quickclash {
// Call before constructing the GUI application. Explicit QPA settings take precedence.
void configureUiFontBackend();
// Logical pixels only: Qt applies the screen's device pixel ratio separately.
[[nodiscard]] QFont platformUiFont();
} // namespace quickclash
