#include "infra/core/service_traffic.h"
#include "infra/storage/json_store.h"
#include <QTimer>
#include <utility>

namespace quickclash::infra {
namespace {
constexpr int kFailureRetryMs = 1000;
} // namespace
ServiceTraffic::ServiceTraffic(Request request, Sample sample, QObject* parent)
    : QObject(parent), request_(std::move(request)), sample_(std::move(sample)) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &ServiceTraffic::requestNext);
}
void ServiceTraffic::setEnabled(bool enabled) {
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    ++generation_;
    retry_.stop();
    if (enabled_)
        requestNext();
}
void ServiceTraffic::reset() {
    enabled_ = false;
    ++generation_;
    pending_ = 0;
    retry_.stop();
}
void ServiceTraffic::requestNext() {
    if (!enabled_ || pending_)
        return;
    const auto generation = generation_;
    const auto id = ++requestId_;
    pending_ = id;
    request_([guard = QPointer<ServiceTraffic>(this), generation,
              id](domain::Result<QJsonObject> result) {
        if (!guard || guard->pending_ != id)
            return;
        guard->pending_ = 0;
        if (generation != guard->generation_) {
            guard->requestNext();
            return;
        }
        if (!guard->enabled_)
            return;
        if (const auto* value = std::get_if<QJsonObject>(&result)) {
            guard->sample_(*value);
            // A new command must wait asynchronously even if a local error was synchronous.
            if (guard && generation == guard->generation_ && guard->enabled_)
                guard->retry_.start(0);
        } else {
            guard->retry_.start(kFailureRetryMs);
        }
    });
}
ServiceTrafficRelay::ServiceTrafficRelay(Open open, QObject* parent)
    : QObject(parent), open_(std::move(open)) {
    idle_.setSingleShot(true);
    deadline_.setSingleShot(true);
    connect(&idle_, &QTimer::timeout, this, [this] { close(); });
    connect(&deadline_, &QTimer::timeout, this, [this] { close("CoreRequestTimeout"); });
}
void ServiceTrafficRelay::close(const char* error) {
    ++generation_;
    idle_.stop();
    deadline_.stop();
    auto stream = stream_;
    stream_.clear();
    auto pending = std::exchange(pending_, {});
    // The waiting handler can close the stream from inside its readyRead callback.
    // Defer destruction until that callback unwinds; generation_ rejects late samples.
    if (stream)
        stream->deleteLater();
    if (pending)
        pending(storageError(error));
}
void ServiceTrafficRelay::request(Handler handler) {
    if (pending_) {
        handler(storageError("ServiceBusy"));
        return;
    }
    idle_.stop();
    pending_ = std::move(handler);
    const auto generation = generation_;
    if (!stream_) {
        stream_ = open_(
            [guard = QPointer<ServiceTrafficRelay>(this), generation](const QJsonObject& sample) {
                if (!guard || generation != guard->generation_ || !guard->pending_)
                    return;
                guard->deadline_.stop();
                auto pending = std::exchange(guard->pending_, {});
                // Start before delivery: a synchronous next request cancels the idle timer.
                guard->idle_.start(2000);
                pending(sample);
            });
        if (!stream_) {
            close("CoreRequestFailed");
            return;
        }
        // The relay owns the core stream, including when its enclosing service is destroyed.
        stream_->setParent(this);
        connect(stream_, &QObject::destroyed, this, [this, generation] {
            if (generation == generation_)
                close("CoreRequestFailed");
        });
    }
    deadline_.start(3500);
}
} // namespace quickclash::infra
