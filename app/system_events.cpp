#include "system_events.h"
#include <QCoreApplication>
#include <QNetworkInformation>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace quickclash {
SystemEvents::SystemEvents(QObject* parent) : QObject(parent) {
    QCoreApplication::instance()->installNativeEventFilter(this);
    if (QNetworkInformation::loadDefaultBackend()) {
        auto* information = QNetworkInformation::instance();
        connect(information, &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability) { emit reconcileRequested(); });
        connect(information, &QNetworkInformation::transportMediumChanged, this,
                [this](QNetworkInformation::TransportMedium) { emit reconcileRequested(); });
    }
}
SystemEvents::~SystemEvents() {
    QCoreApplication::instance()->removeNativeEventFilter(this);
}
bool SystemEvents::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
    Q_UNUSED(type)
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    const auto* event = static_cast<MSG*>(message);
    if (event->message == WM_POWERBROADCAST &&
        (event->wParam == PBT_APMRESUMEAUTOMATIC || event->wParam == PBT_APMRESUMESUSPEND)) {
        emit reconcileRequested();
    }
#else
    Q_UNUSED(message)
#endif
    return false;
}
} // namespace quickclash
