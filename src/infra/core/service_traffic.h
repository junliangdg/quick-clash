#pragma once
#include "domain/contracts.h"
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace quickclash::infra {
// One outstanding long poll consumes each sample from the service's persistent core stream.
class ServiceTraffic final : public QObject {
  public:
    using Handler = std::function<void(domain::Result<QJsonObject>)>;
    using Request = std::function<void(Handler)>;
    using Sample = std::function<void(const QJsonObject&)>;
    ServiceTraffic(Request request, Sample sample, QObject* parent = nullptr);
    void setEnabled(bool enabled);
    void reset();

  private:
    void requestNext();
    Request request_;
    Sample sample_;
    QTimer retry_;
    quint64 generation_ = 0;
    quint64 requestId_ = 0;
    quint64 pending_ = 0;
    bool enabled_ = false;
};

// The privileged service owns one persistent core stream and responds to one waiting client.
class ServiceTrafficRelay final : public QObject {
  public:
    using Handler = ServiceTraffic::Handler;
    using Open = std::function<QObject*(std::function<void(const QJsonObject&)>)>;
    explicit ServiceTrafficRelay(Open open, QObject* parent = nullptr);
    void request(Handler handler);
    void close(const char* error = "ServiceTrafficCancelled");

  private:
    Open open_;
    QPointer<QObject> stream_;
    Handler pending_;
    QTimer idle_;
    QTimer deadline_;
    quint64 generation_ = 0;
};
} // namespace quickclash::infra
