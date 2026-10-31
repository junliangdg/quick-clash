#pragma once

#include <QString>
#include <QStringList>

namespace quickclash::platform {

// The fixed setup actions the ordinary-user GUI may request. First authorization elevates;
// an update of the registered folder only restarts the service. No scripts or arbitrary commands.
enum class ServiceSetupAction { Install, Update, Uninstall, Authorize, Validate };

struct ServiceSetupRequest {
    ServiceSetupAction action = ServiceSetupAction::Install;
    // Folder the ordinary user launched QuickClash from. It contains quickclash.exe and, for a
    // packaged build, sits next to core/ and geo/.
    QString clientRoot;
    // Appended plain-text log shared with the unelevated UI.
    QString logFile;
};

struct ServiceSetupInvocation {
    bool present = false;
    bool valid = true;
    QString problem;
    ServiceSetupRequest request;
};

// Parses "--service-setup <action> [--client-root <dir>] [--log <file>]" from the raw
// command line. Unsupported actions stay present so the caller can report a usage error.
[[nodiscard]] ServiceSetupInvocation parseServiceSetupCommandLine(const QStringList& arguments);

// Runs setup with a fixed action table and required rights. Returns 0 on success and 1 on any
// failure; the reason is written to the log and to stderr.
int runServiceSetup(const ServiceSetupRequest& request);

} // namespace quickclash::platform
