#pragma once
#include "domain/contracts.h"
#include <QObject>
#include <QProcessEnvironment>
#include <QTimer>
#include <memory>

namespace quickclash::platform {
class OwnedProcess final : public QObject {
    Q_OBJECT
  public:
    explicit OwnedProcess(QObject* parent = nullptr);
    ~OwnedProcess() override;
    [[nodiscard]] domain::CommandResult
    start(const QString& executable, const QStringList& arguments, const QString& directory,
          bool isolated = false,
          const QProcessEnvironment& environment = QProcessEnvironment::systemEnvironment());
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] quint32 pid() const;
    [[nodiscard]] bool ownsListener(quint16 port) const;
    [[nodiscard]] qsizetype outputBytes() const {
        return outputBytes_;
    }
  signals:
    void finished(int exitCode);

  private:
    void poll();
    struct State;
    std::unique_ptr<State> state_;
    QTimer timer_;
    qsizetype outputBytes_ = 0;
};
} // namespace quickclash::platform
