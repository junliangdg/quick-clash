#include "infra/core/listener_ports.h"
#include <QHostAddress>
#include <QUdpSocket>

namespace quickclash::infra {
namespace {
QJsonObject portIssue(QAbstractSocket::SocketError error, quint16 port, const QString& role) {
    const auto code = error == QAbstractSocket::AddressInUseError
                          ? QStringLiteral("CoreListenerAddressInUse")
                      : error == QAbstractSocket::SocketAccessError
                          ? QStringLiteral("CoreListenerAccessDenied")
                          : QStringLiteral("CoreListenerPortUnavailable");
    return {{QStringLiteral("code"), code},
            {QStringLiteral("port"), port},
            {QStringLiteral("role"), role}};
}
} // namespace
QJsonObject listenerPortIssue(const QTcpServer& socket, quint16 port, const QString& role) {
    return portIssue(socket.serverError(), port, role);
}
QJsonObject listenerPortIssue(const QAbstractSocket& socket, quint16 port, const QString& role) {
    return portIssue(socket.error(), port, role);
}
QJsonObject listenerPortIssue(quint16 port, const QString& role) {
    QTcpServer socket;
    if (!socket.listen(QHostAddress::LocalHost, port))
        return listenerPortIssue(socket, port, role);
    QUdpSocket udp;
    if (role == QStringLiteral("mixed") &&
        !udp.bind(QHostAddress::LocalHost, port, QAbstractSocket::DontShareAddress))
        return listenerPortIssue(udp, port, role);
    return {};
}
QJsonObject suggestListenerPorts(quint16 mixed, quint16 controller, quint16 ownedMixed,
                                 quint16 ownedController) {
    QTcpServer first;
    QTcpServer second;
    QUdpSocket udp;
    auto find = [&udp](QTcpServer& socket, quint16 requested, quint16 owned, quint16 excluded,
                       bool mixed) {
        for (int offset = 0; offset < 128; ++offset) {
            const auto port = static_cast<quint16>(1024 + (requested - 1024 + offset) % 64512);
            if (port == excluded)
                continue;
            if (port == owned)
                return port;
            if (!socket.listen(QHostAddress::LocalHost, port))
                continue;
            if (!mixed ||
                udp.bind(QHostAddress::LocalHost, port, QAbstractSocket::DontShareAddress))
                return port;
            socket.close();
        }
        return quint16{0};
    };
    if (mixed < 1024 || controller < 1024)
        return {};
    const auto proposedMixed = find(first, mixed, ownedMixed, ownedController, true);
    const auto proposedController = find(second, controller, ownedController, proposedMixed, false);
    if (!proposedMixed || !proposedController)
        return {};
    return {{QStringLiteral("mixedPort"), proposedMixed},
            {QStringLiteral("controllerPort"), proposedController}};
}
} // namespace quickclash::infra
