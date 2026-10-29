#include "system_events.h"
#include <QCoreApplication>
#include <QNetworkInformation>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
// clang-format on
#endif

namespace quickclash {
SystemEvents::SystemEvents(QObject* parent) : QObject(parent) {
    QCoreApplication::instance()->installNativeEventFilter(this);
#ifdef Q_OS_WIN
    // Reachability/medium can stay unchanged when switching Ethernet or renewing an IP.
    // Callbacks run on an OS thread; deliver only a queued, context-bound notification.
    (void)NotifyIpInterfaceChange(
        AF_UNSPEC,
        [](void* context, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
            auto* events = static_cast<SystemEvents*>(context);
            QMetaObject::invokeMethod(
                events, [events] { emit events->reconcileRequested(); }, Qt::QueuedConnection);
        },
        this, FALSE, &interfaceNotification_);
    (void)NotifyUnicastIpAddressChange(
        AF_UNSPEC,
        [](void* context, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE) {
            auto* events = static_cast<SystemEvents*>(context);
            QMetaObject::invokeMethod(
                events, [events] { emit events->reconcileRequested(); }, Qt::QueuedConnection);
        },
        this, FALSE, &addressNotification_);
#endif
    if (QNetworkInformation::loadDefaultBackend()) {
        auto* information = QNetworkInformation::instance();
        connect(information, &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability) { emit reconcileRequested(); });
        connect(information, &QNetworkInformation::transportMediumChanged, this,
                [this](QNetworkInformation::TransportMedium) { emit reconcileRequested(); });
    }
}
SystemEvents::~SystemEvents() {
#ifdef Q_OS_WIN
    // Cancellation waits for outstanding callbacks before their QObject context is destroyed.
    if (interfaceNotification_)
        (void)CancelMibChangeNotify2(interfaceNotification_);
    if (addressNotification_)
        (void)CancelMibChangeNotify2(addressNotification_);
#endif
    QCoreApplication::instance()->removeNativeEventFilter(this);
}
bool SystemEvents::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
    Q_UNUSED(type)
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    const auto* event = static_cast<MSG*>(message);
    if (event->message == WM_SETTINGCHANGE)
        emit typographyChanged();
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
