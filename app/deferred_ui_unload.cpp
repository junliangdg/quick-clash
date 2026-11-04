#include "deferred_ui_unload.h"

namespace quickclash {
DeferredUiUnload::DeferredUiUnload(QObject* parent) : QObject(parent) {
    timer_.setSingleShot(true);
    timer_.setInterval(0);
}
void DeferredUiUnload::cancel() {
    ++ticket_;
    timer_.stop();
    disconnect(timeoutConnection_);
    timeoutConnection_ = {};
}
void DeferredUiUnload::update(bool eligible, quint64 engineGeneration) {
    if (!eligible) {
        cancel();
        return;
    }
    if (pending() && engineGeneration_ == engineGeneration)
        return;
    cancel();
    engineGeneration_ = engineGeneration;
    const auto ticket = ticket_;
    timeoutConnection_ = connect(&timer_, &QTimer::timeout, this, [this, ticket, engineGeneration] {
        if (ticket != ticket_ || engineGeneration != engineGeneration_)
            return;
        cancel();
        emit due(engineGeneration);
    });
    timer_.start();
}
} // namespace quickclash
