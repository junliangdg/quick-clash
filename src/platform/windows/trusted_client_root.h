#pragma once
#include <QString>
#include <optional>

namespace quickclash::platform {
// Fixed portable layout; packaged builds never fall back to checkout resources.
[[nodiscard]] QString portableCorePath(const QString& clientRoot);

// Administrator-owned client authorization. The configuration file is JSON:
// {"schemaVersion":1,"clientRoot":"D:/Apps/QuickClash/bin"}
// The root is canonicalized once; clients must resolve to root/quickclash.exe.
struct TrustedClientRoot {
    QString root;
    QString executable;
};

// Canonicalizes an existing directory and requires a real quickclash.exe below it.
// Short names, symbolic links and junctions are rejected rather than silently resolved.
[[nodiscard]] std::optional<TrustedClientRoot> makeTrustedClientRoot(const QString& root);

// Reads the protected configuration. A missing file keeps an existing installation usable by
// falling back to its own directory; an unreadable or malformed file fails closed.
[[nodiscard]] std::optional<TrustedClientRoot> readTrustedClientRoot(const QString& configFile,
                                                                     const QString& fallbackRoot);

// Returns the canonical image path when it is exactly the authorized quickclash.exe.
[[nodiscard]] std::optional<QString> trustedClientImage(const QString& imagePath,
                                                        const TrustedClientRoot& trusted);

} // namespace quickclash::platform
