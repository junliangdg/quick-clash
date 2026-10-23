#pragma once
#include "domain/contracts.h"

namespace quickclash::domain {
enum class OperationState { Pending, Running, Succeeded, Failed, Cancelled, Uncertain };
class Operation {
  public:
    explicit Operation(QString kind, MonotonicTime deadline);
    [[nodiscard]] const QUuid& id() const noexcept {
        return id_;
    }
    [[nodiscard]] OperationState state() const noexcept {
        return state_;
    }
    [[nodiscard]] const std::optional<Error>& error() const noexcept {
        return error_;
    }
    [[nodiscard]] bool expired(MonotonicTime now) const noexcept {
        return now >= deadline_;
    }
    [[nodiscard]] bool begin();
    [[nodiscard]] bool finish(OperationState state, std::optional<Error> error = std::nullopt);

  private:
    QUuid id_ = QUuid::createUuid();
    QString kind_;
    MonotonicTime deadline_;
    OperationState state_ = OperationState::Pending;
    std::optional<Error> error_;
};
} // namespace quickclash::domain
