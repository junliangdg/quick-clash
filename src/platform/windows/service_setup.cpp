#include "platform/desktop_services.h"
#include "platform/process_memory.h"
#include "platform/windows/trusted_client_root.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <utility>
// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
// clang-format on

namespace quickclash::platform {
namespace {
// A setup run registers or restarts one service. Ten minutes is a generous upper
// bound that still releases the UI if the elevated helper never returns.
constexpr int kSetupTimeout = 10 * 60 * 1000;

QString quoted(const QString& value) {
    // Windows paths cannot contain double quotes, so wrapping each argument is unambiguous.
    return QLatin1Char('"') + value + QLatin1Char('"');
}

QString installSource(const QString& clientRoot) {
    const QFileInfo service(QDir(clientRoot).filePath(QStringLiteral("quickclash_service.exe")));
    return service.isFile() ? QDir(clientRoot).absolutePath() : QString{};
}

// The GUI never elevates itself. It launches the same executable with a fixed setup action and
// reads the shared log; all privileged work stays in that short-lived child process.
class ServiceSetup final : public QObject, public application::IServiceSetup {
  public:
    explicit ServiceSetup(QObject* parent = nullptr) : QObject(parent) {
        timer_.setInterval(400);
        connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
    }
    ~ServiceSetup() override {
        closeProcess();
    }

    void start(application::ServiceSetupMode mode, const QString& clientRoot,
               std::function<void(int)> completed,
               std::function<void(const QString&)> failed) override {
        if (process_) {
            failed(QStringLiteral("ServiceSetupBusy"));
            return;
        }
        const auto trusted = makeTrustedClientRoot(clientRoot);
        if (!trusted) {
            failed(QStringLiteral("ServiceSetupClientRootRejected"));
            return;
        }
        if (installSource(trusted->root).isEmpty()) {
            failed(QStringLiteral("ServiceSetupPackageMissing"));
            return;
        }
        auto directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (directory.isEmpty())
            directory = QDir::tempPath();
        log_ = QDir(directory).filePath(
            QStringLiteral("quickclash-setup-%1.log").arg(QDateTime::currentMSecsSinceEpoch()));
        auto action = QStringLiteral("authorize");
        if (mode == application::ServiceSetupMode::Install)
            action = QStringLiteral("install");
        else if (mode == application::ServiceSetupMode::Update)
            action = QStringLiteral("update");
        const QStringList arguments{
            QStringLiteral("--service-setup"), action,
            QStringLiteral("--client-root"),   trusted->root,
            QStringLiteral("--log"),           log_,
        };
        QStringList rendered;
        rendered.reserve(arguments.size());
        for (const auto& argument : arguments)
            rendered.append(quoted(argument));
        const auto executable = QCoreApplication::applicationFilePath();
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        const auto registration = installedServiceRegistration(0);
        const bool reuse = mode == application::ServiceSetupMode::Update &&
                           (registration == ServiceRegistration::Verified ||
                            registration == ServiceRegistration::Stopped);
        info.lpVerb = reuse ? L"open" : L"runas";
        const auto file = executable.toStdWString();
        const auto parameters = rendered.join(QLatin1Char(' ')).toStdWString();
        const auto working = trusted->root.toStdWString();
        info.lpFile = file.c_str();
        info.lpParameters = parameters.c_str();
        info.lpDirectory = working.c_str();
        info.nShow = SW_HIDE;
        if (!ShellExecuteExW(&info)) {
            const auto failure = GetLastError();
            closeProcess();
            failed(failure == ERROR_CANCELLED ? QStringLiteral("ServiceSetupDeclined")
                                              : QStringLiteral("ServiceSetupLaunchFailed"));
            return;
        }
        process_ = info.hProcess;
        completed_ = std::move(completed);
        failed_ = std::move(failed);
        started_ = QDateTime::currentMSecsSinceEpoch();
        timer_.start();
    }

    [[nodiscard]] QString logPath() const override {
        return log_;
    }

  private:
    void poll() {
        if (!process_)
            return;
        if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
            DWORD exitCode = 1;
            if (!GetExitCodeProcess(process_, &exitCode))
                exitCode = 1;
            closeProcess();
            auto completed = std::move(completed_);
            completed_ = nullptr;
            failed_ = nullptr;
            timer_.stop();
            if (completed)
                completed(static_cast<int>(exitCode));
            return;
        }
        if (QDateTime::currentMSecsSinceEpoch() - started_ > kSetupTimeout) {
            closeProcess();
            auto failed = std::move(failed_);
            completed_ = nullptr;
            failed_ = nullptr;
            timer_.stop();
            if (failed)
                failed(QStringLiteral("ServiceSetupTimeout"));
        }
    }

    void closeProcess() {
        timer_.stop();
        if (process_) {
            CloseHandle(process_);
            process_ = nullptr;
        }
    }

    QTimer timer_;
    HANDLE process_ = nullptr;
    QString log_;
    qint64 started_ = 0;
    std::function<void(int)> completed_;
    std::function<void(const QString&)> failed_;
};
} // namespace

std::unique_ptr<application::IServiceSetup> makeServiceSetup() {
    return std::make_unique<ServiceSetup>();
}

} // namespace quickclash::platform
