#pragma once
#include <QAbstractSocket>
#include <QJsonObject>
#include <QTcpServer>

namespace quickclash::infra {
// A successful probe releases the socket on return; startup still verifies owned listeners.
[[nodiscard]] QJsonObject listenerPortIssue(quint16 port, const QString& role);
[[nodiscard]] QJsonObject listenerPortIssue(const QTcpServer& socket, quint16 port,
                                            const QString& role);
[[nodiscard]] QJsonObject listenerPortIssue(const QAbstractSocket& socket, quint16 port,
                                            const QString& role);
[[nodiscard]] QJsonObject suggestListenerPorts(quint16 mixed, quint16 controller,
                                               quint16 ownedMixed = 0, quint16 ownedController = 0);
} // namespace quickclash::infra
