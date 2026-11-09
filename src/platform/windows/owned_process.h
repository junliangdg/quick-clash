#pragma once
#include "application/desktop_process.h"
#include "platform/process_memory.h"
#include <QDateTime>
#include <QProcessEnvironment>
#include <memory>

namespace quickclash::platform {
class OwnedProcess final : public application::IDesktopProcess {
    Q_OBJECT
  public:
    explicit OwnedProcess(QObject* parent = nullptr);
    ~OwnedProcess() override;
    [[nodiscard]] domain::CommandResult start(const QString& executable,
                                              const QStringList& arguments,
                                              const QString& directory,
                                              bool validation = false) override;
    [[nodiscard]] domain::CommandResult start(const QString& executable,
                                              const QStringList& arguments,
                                              const QString& directory, bool validation,
                                              const QProcessEnvironment& environment);
    domain::CommandResult stop() override;
    [[nodiscard]] QJsonObject exitDiagnostics() const override;
    [[nodiscard]] bool running() const override;
    [[nodiscard]] quint32 pid() const override;
    [[nodiscard]] QJsonObject memory() const override;
    [[nodiscard]] bool ownsListener(quint16 port) const override;
    [[nodiscard]] qsizetype outputBytes() const {
        return outputBytes_;
    }
    // Bounded raw tail for diagnostics. Redaction belongs to the caller before persistence or
    // transport so this layer only captures what the child process actually wrote.
    [[nodiscard]] QByteArray outputTail() const {
        return outputTail_;
    }
    [[nodiscard]] int exitCode() const {
        return exitCode_;
    }
    [[nodiscard]] QDateTime exitedAt() const {
        return exitedAt_;
    }

  private:
    void poll();
    void drainOutput();
    struct State;
    std::unique_ptr<State> state_;
    qsizetype outputBytes_ = 0;
    QByteArray outputTail_;
    int exitCode_ = 0;
    QDateTime exitedAt_;
};
} // namespace quickclash::platform
