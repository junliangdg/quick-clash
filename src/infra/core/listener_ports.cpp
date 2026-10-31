#include "infra/core/listener_ports.h"
#include <QHostAddress>

namespace quickclash::infra {
QJsonObject listenerPortIssue(const QTcpServer& socket, quint16 port, const QString& role) {
    const auto code = socket.serverError() == QAbstractSocket::AddressInUseError
                          ? QStringLiteral("CoreListenerAddressInUse")
                      : socket.serverError() == QAbstractSocket::SocketAccessError
                          ? QStringLiteral("CoreListenerAccessDenied")
                          : QStringLiteral("CoreListenerPortUnavailable");
    return {{QStringLiteral("code"), code},
            {QStringLiteral("port"), port},
            {QStringLiteral("role"), role}};
}
QJsonObject listenerPortIssue(quint16 port, const QString& role) {
    QTcpServer socket;
    return socket.listen(QHostAddress::LocalHost, port) ? QJsonObject{}
                                                        : listenerPortIssue(socket, port, role);
}
QJsonObject suggestListenerPorts(quint16 mixed, quint16 controller, quint16 ownedMixed,
                                 quint16 ownedController) {
    QTcpServer first;
    QTcpServer second;
    auto find = [](QTcpServer& socket, quint16 requested, quint16 owned, quint16 excluded) {
        for (int offset = 0; offset < 128; ++offset) {
            const auto port = static_cast<quint16>(1024 + (requested - 1024 + offset) % 64512);
            if (port != excluded && (port == owned || socket.listen(QHostAddress::LocalHost, port)))
                return port;
        }
        return quint16{0};
    };
    if (mixed < 1024 || controller < 1024)
        return {};
    const auto proposedMixed = find(first, mixed, ownedMixed, ownedController);
    const auto proposedController = find(second, controller, ownedController, proposedMixed);
    if (!proposedMixed || !proposedController)
        return {};
    return {{QStringLiteral("mixedPort"), proposedMixed},
            {QStringLiteral("controllerPort"), proposedController}};
}
} // namespace quickclash::infra
