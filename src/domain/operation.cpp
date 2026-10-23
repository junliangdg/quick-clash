#include "domain/operation.h"
#include <utility>

namespace quickclash::domain {
Operation::Operation(QString kind, MonotonicTime deadline)
    : kind_(std::move(kind)), deadline_(deadline) {}
bool Operation::begin() {
    if (state_ != OperationState::Pending) {
        return false;
    }
    state_ = OperationState::Running;
    return true;
}
bool Operation::finish(OperationState state, std::optional<Error> error) {
    if (state_ != OperationState::Pending && state_ != OperationState::Running) {
        return false;
    }
    if (state == OperationState::Pending || state == OperationState::Running) {
        return false;
    }
    if ((state == OperationState::Failed || state == OperationState::Uncertain) !=
        error.has_value()) {
        return false;
    }
    if (state == OperationState::Succeeded && state_ != OperationState::Running) {
        return false;
    }
    if (error) {
        error->operationId = id_;
    }
    state_ = state;
    error_ = std::move(error);
    return true;
}
} // namespace quickclash::domain
