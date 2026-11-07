#pragma once
#include <QJsonObject>
#include <QNetworkProxy>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <memory>

namespace quickclash::infra {
// Owns one small, idle-expiring transport process. TLS and HTTP remain in the caller;
// the relay only moves encrypted bytes and never receives subscription credentials.
class NetworkRelay final : public QObject {
    Q_OBJECT
  public:
    explicit NetworkRelay(QObject* parent = nullptr, const QString& executable = {});
    ~NetworkRelay() override;
    void acquire();
    void release();
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] QNetworkProxy proxy() const;
    [[nodiscard]] QJsonObject memory() const;

  signals:
    void available();
    void failed();

  private:
    void start();
    void fail();
    struct State;
    std::unique_ptr<State> state_;
    QString executable_;
    QTimer idle_, deadline_;
    QByteArray response_, password_;
    int users_ = 0;
    quint16 port_ = 0;
    bool stopping_ = false;
    // Destroy the process before the state used by its callbacks.
    QProcess process_;
};
} // namespace quickclash::infra
