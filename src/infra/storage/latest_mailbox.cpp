#include "infra/storage/latest_mailbox.h"
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <utility>

namespace quickclash::infra {
struct LatestMailbox::State {
    QMutex mutex;
    LatestMailbox* receiver = nullptr;
    application::RuntimeUpdate latest;
    application::RuntimeDemands demands;
    bool pending = false;
};
LatestMailbox::LatestMailbox(QObject* parent) : QObject(parent), state_(std::make_shared<State>()) {
    state_->receiver = this;
}
LatestMailbox::~LatestMailbox() {
    close();
}
LatestMailbox::Publisher LatestMailbox::publisher() const {
    return [state = state_](application::RuntimeUpdate update) {
        QMutexLocker lock(&state->mutex);
        if (!state->receiver || update.generation < state->latest.generation)
            return;
        if (update.generation > state->latest.generation) {
            state->latest = {update.generation, update.epoch, {}};
        }
        if (state->latest.epoch != update.epoch)
            return;
        const auto accepts = [&](const QString& channel, bool invalidation) {
            const auto stream = application::channelStream(channel);
            if (stream.isEmpty())
                return true;
            const auto revision = update.demandGenerations.value(stream);
            if (revision == 0 || revision != state->demands.generations.value(stream))
                return false;
            return invalidation || state->demands.active.contains(stream);
        };
        for (const auto& channel : update.invalidated) {
            if (!accepts(channel, true))
                continue;
            state->latest.values.remove(channel);
            state->latest.invalidated.insert(channel);
        }
        for (auto it = update.values.constBegin(); it != update.values.constEnd(); ++it) {
            if (!accepts(it.key(), false))
                continue;
            state->latest.invalidated.remove(it.key());
            state->latest.values.insert(it.key(), it.value());
        }
        state->latest.demandGenerations = state->demands.generations;
        if (state->latest.values.isEmpty() && state->latest.invalidated.isEmpty())
            return;
        if (state->pending)
            return;
        state->pending = true;
        auto* receiver = state->receiver;
        // Enqueue under the same lock used by close(), so no producer invokes a dead QObject.
        QMetaObject::invokeMethod(
            receiver, [receiver] { receiver->drain(); }, Qt::QueuedConnection);
    };
}
void LatestMailbox::setDemands(const application::RuntimeDemands& demands) {
    Q_ASSERT(QThread::currentThread() == thread());
    QMutexLocker lock(&state_->mutex);
    state_->demands = demands;
    for (const auto& stream : application::RuntimeDemands::streams()) {
        if (state_->latest.demandGenerations.value(stream) == demands.generations.value(stream) &&
            demands.active.contains(stream))
            continue;
        const auto keys = state_->latest.values.keys();
        for (const auto& channel : keys)
            if (application::channelStream(channel) == stream)
                state_->latest.values.remove(channel);
        const auto invalidated = state_->latest.invalidated;
        for (const auto& channel : invalidated)
            if (application::channelStream(channel) == stream)
                state_->latest.invalidated.remove(channel);
    }
    state_->latest.demandGenerations = demands.generations;
}
void LatestMailbox::close() {
    Q_ASSERT(QThread::currentThread() == thread());
    QMutexLocker lock(&state_->mutex);
    state_->receiver = nullptr;
    state_->latest.values = {};
    state_->latest.invalidated.clear();
}
void LatestMailbox::drain() {
    Q_ASSERT(QThread::currentThread() == thread());
    application::RuntimeUpdate update;
    {
        QMutexLocker lock(&state_->mutex);
        state_->pending = false;
        if (!state_->receiver)
            return;
        update = state_->latest;
        state_->latest.values = {};
        state_->latest.invalidated.clear();
    }
    // Do not hold the mutex while consumers mutate models or destroy the receiver.
    if (!update.values.isEmpty() || !update.invalidated.isEmpty())
        emit delivered(update);
}
} // namespace quickclash::infra
