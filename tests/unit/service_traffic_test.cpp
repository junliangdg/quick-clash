#include "infra/core/service_traffic.h"
#include "infra/storage/json_store.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <memory>
#include <utility>

using quickclash::infra::ServiceTraffic;
using quickclash::infra::ServiceTrafficRelay;
namespace {
bool waitFor(const std::function<bool()>& condition, int timeout = 2000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    return condition();
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    {
        QList<ServiceTraffic::Handler> requests;
        QList<QJsonObject> samples;
        ServiceTraffic traffic(
            [&](ServiceTraffic::Handler handler) { requests.append(std::move(handler)); },
            [&](const QJsonObject& sample) { samples.append(sample); });
        traffic.setEnabled(true);
        if (requests.size() != 1)
            return 1;
        traffic.setEnabled(false);
        traffic.setEnabled(true);
        if (requests.size() != 1)
            return 2; // Reacquiring must not create a second outstanding request.
        auto old = requests.takeFirst();
        old(QJsonObject{{"up", 1}});
        if (!samples.isEmpty() || !waitFor([&] { return requests.size() == 1; }))
            return 3;
        auto current = requests.takeFirst();
        current(QJsonObject{{"up", 2}});
        if (samples.size() != 1 || samples.first().value("up") != 2 ||
            !waitFor([&] { return requests.size() == 1; }))
            return 4;
        auto failed = requests.takeFirst();
        QElapsedTimer retry;
        retry.start();
        failed(quickclash::infra::storageError("CoreRequestFailed"));
        if (!waitFor([&] { return requests.size() == 1; }, 1700) || retry.elapsed() < 900)
            return 5;
        auto previousSession = requests.takeFirst();
        traffic.reset();
        traffic.setEnabled(true);
        if (requests.size() != 1)
            return 6;
        previousSession(QJsonObject{{"up", 3}});
        if (samples.size() != 1 || requests.size() != 1)
            return 7;
        auto nextSession = requests.takeFirst();
        nextSession(QJsonObject{{"up", 4}});
        if (samples.size() != 2 || samples.last().value("up") != 4)
            return 8;
        traffic.reset();
    }
    {
        // A server produces one sample each second independently of requests. Keep a single
        // waiting request and exercise the same client controller with real event-loop timing.
        ServiceTraffic::Handler pending;
        int overlapping = 0;
        QList<qint64> received;
        QElapsedTimer clock;
        clock.start();
        ServiceTraffic traffic(
            [&](ServiceTraffic::Handler handler) {
                if (pending)
                    ++overlapping;
                pending = std::move(handler);
            },
            [&](const QJsonObject&) { received.append(clock.elapsed()); });
        QTimer stream;
        stream.setTimerType(Qt::PreciseTimer);
        stream.setInterval(1000);
        QObject::connect(&stream, &QTimer::timeout, &stream, [&] {
            auto callback = std::exchange(pending, {});
            if (callback)
                callback(QJsonObject{{"up", 1}, {"down", 2}});
        });
        traffic.setEnabled(true);
        stream.start();
        if (!waitFor([&] { return received.size() == 4; }, 5500) || overlapping)
            return 9;
        for (qsizetype i = 1; i < received.size(); ++i)
            if (received[i] - received[i - 1] < 750 || received[i] - received[i - 1] > 1300)
                return 10;
        traffic.setEnabled(false);
        auto late = std::exchange(pending, {});
        if (late)
            late(QJsonObject{{"up", 99}});
        if (received.size() != 4)
            return 11;
    }
    {
        ServiceTraffic::Handler late;
        int samples = 0;
        {
            auto traffic = std::make_unique<ServiceTraffic>(
                [&](ServiceTraffic::Handler handler) { late = std::move(handler); },
                [&](const QJsonObject&) { ++samples; });
            traffic->setEnabled(true);
        }
        late(QJsonObject{{"up", 1}});
        if (samples != 0)
            return 12; // A delayed transport callback cannot outlive its controller.
    }
    {
        std::function<void(const QJsonObject&)> emitSample;
        QPointer<QObject> stream;
        int opened = 0;
        int results = 0;
        int errors = 0;
        QString lastError;
        ServiceTrafficRelay relay([&](auto handler) -> QObject* {
            ++opened;
            emitSample = std::move(handler);
            auto* next = new QObject;
            stream = next;
            return next;
        });
        const auto receive = [&](quickclash::domain::Result<QJsonObject> value) {
            if (std::holds_alternative<QJsonObject>(value))
                ++results;
            else {
                ++errors;
                lastError = std::get<quickclash::domain::Error>(value).code;
            }
        };
        relay.request(receive);
        relay.request(receive);
        if (opened != 1 || errors != 1 || lastError != "ServiceBusy")
            return 13;
        emitSample(QJsonObject{{"up", 1}});
        if (results != 1 || !stream)
            return 14;
        relay.request(receive);
        relay.close();
        if (errors != 2 || lastError != "ServiceTrafficCancelled")
            return 15;
        emitSample(QJsonObject{{"up", 2}});
        if (results != 1)
            return 16;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (stream)
            return 17;
        relay.request(receive);
        if (opened != 2 || !stream)
            return 18;
        delete stream.data(); // Simulate an unexpected core stream disconnect.
        if (errors != 3 || lastError != "CoreRequestFailed")
            return 19;
        relay.request(receive);
        emitSample(QJsonObject{{"up", 3}});
        if (opened != 3 || results != 2)
            return 20;
        relay.request(receive);
        if (!waitFor([&] { return errors == 4; }, 4200) || lastError != "CoreRequestTimeout")
            return 21;
        relay.request(receive);
        emitSample(QJsonObject{{"up", 4}});
        if (opened != 4 || results != 3)
            return 22;
    }
    {
        std::function<void(const QJsonObject&)> emitSample;
        QPointer<QObject> stream;
        int results = 0;
        {
            ServiceTrafficRelay relay([&](auto handler) -> QObject* {
                emitSample = std::move(handler);
                auto* next = new QObject;
                stream = next;
                return next;
            });
            relay.request([&](quickclash::domain::Result<QJsonObject>) { ++results; });
            emitSample(QJsonObject{{"up", 1}});
            if (results != 1 || !waitFor([&] { return !stream; }, 2600))
                return 23; // An unused stream closes after demand is released.
            relay.request([&](quickclash::domain::Result<QJsonObject>) { ++results; });
            if (!stream)
                return 24;
        }
        if (stream)
            return 25; // Relay destruction owns and closes its live stream.
        emitSample(QJsonObject{{"up", 2}});
        if (results != 1)
            return 26;
    }
    {
        ServiceTrafficRelay relay([](auto) -> QObject* { return nullptr; });
        QString error;
        relay.request([&](quickclash::domain::Result<QJsonObject> result) {
            error = std::get<quickclash::domain::Error>(result).code;
        });
        if (error != "CoreRequestFailed")
            return 27;
    }
    {
        std::function<void(const QJsonObject&)> emitSample;
        QPointer<QObject> stream;
        bool aliveDuringDelivery = false;
        ServiceTrafficRelay relay([&](auto handler) -> QObject* {
            emitSample = std::move(handler);
            auto* next = new QObject;
            stream = next;
            return next;
        });
        relay.request([&](quickclash::domain::Result<QJsonObject>) {
            relay.close();
            aliveDuringDelivery = !stream.isNull();
        });
        emitSample(QJsonObject{{"up", 1}});
        if (!aliveDuringDelivery)
            return 28;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (stream)
            return 29;
    }
    std::cout << "Continuous traffic, relay lifecycle, retry, demand and session fences passed\n";
    return 0;
}
