#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>

namespace quickclash {
class SystemEvents final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit SystemEvents(QObject* parent = nullptr);
    ~SystemEvents() override;
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
  signals:
    void reconcileRequested();
    void typographyChanged();

  private:
#ifdef Q_OS_WIN
    void* interfaceNotification_ = nullptr;
    void* addressNotification_ = nullptr;
#endif
};
} // namespace quickclash
