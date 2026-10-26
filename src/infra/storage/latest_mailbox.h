#pragma once
#include "application/runtime_update.h"
#include <QObject>
#include <functional>
#include <memory>

namespace quickclash::infra {
// The receiver lives on the GUI thread. Publishers may outlive it and run on other threads.
class LatestMailbox final : public QObject {
    Q_OBJECT
  public:
    using Publisher = std::function<void(application::RuntimeUpdate)>;
    explicit LatestMailbox(QObject* parent = nullptr);
    ~LatestMailbox() override;
    [[nodiscard]] Publisher publisher() const;
    void setDemands(const application::RuntimeDemands& demands);
    void close();
  signals:
    void delivered(const quickclash::application::RuntimeUpdate& update);

  private:
    struct State;
    void drain();
    std::shared_ptr<State> state_;
};
} // namespace quickclash::infra
