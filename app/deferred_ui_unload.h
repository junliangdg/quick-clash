#pragma once
#include <QMetaObject>
#include <QObject>
#include <QTimer>

namespace quickclash {
// The host supplies current capability and visibility; this object owns only the deferred lease.
class DeferredUiUnload final : public QObject {
    Q_OBJECT
  public:
    explicit DeferredUiUnload(int delayMilliseconds = 5000, QObject* parent = nullptr);
    void update(bool eligible, quint64 engineGeneration);
    void cancel();
    [[nodiscard]] bool pending() const {
        return timer_.isActive();
    }
  signals:
    void due(quint64 engineGeneration);

  private:
    QTimer timer_;
    QMetaObject::Connection timeoutConnection_;
    quint64 ticket_ = 0;
    quint64 engineGeneration_ = 0;
};
} // namespace quickclash
