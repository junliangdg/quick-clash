#pragma once
#include "domain/contracts.h"
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace quickclash::platform {
// Extracts only the reviewed entry of a catalog archive after verifying its full digest.
class CoreArchive final : public QObject {
  public:
    explicit CoreArchive(QObject* parent = nullptr);
    ~CoreArchive() override;
    void extract(const QString& archive, const QString& directory, const QString& id,
                 std::function<void(domain::CommandResult)> completion);
    void cancel();
    [[nodiscard]] bool running() const;
    [[nodiscard]] QJsonObject memory() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace quickclash::platform
