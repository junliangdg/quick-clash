#pragma once
#include "application/desktop_process.h"
#include "platform/process_memory.h"
#include <QProcessEnvironment>
#include <QTimer>
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
                                              bool isolated = false) override;
    [[nodiscard]] domain::CommandResult start(const QString& executable,
                                              const QStringList& arguments,
                                              const QString& directory, bool isolated,
                                              const QProcessEnvironment& environment);
    void stop() override;
    [[nodiscard]] bool running() const override;
    [[nodiscard]] quint32 pid() const override;
    [[nodiscard]] QJsonObject memory() const override;
    [[nodiscard]] bool ownsListener(quint16 port) const override;
    [[nodiscard]] qsizetype outputBytes() const {
        return outputBytes_;
    }

  private:
    void poll();
    struct State;
    std::unique_ptr<State> state_;
    QTimer timer_;
    qsizetype outputBytes_ = 0;
};
} // namespace quickclash::platform
