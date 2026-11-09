#pragma once
#include <QHostAddress>
#include <QTcpServer>
#include <QUdpSocket>

namespace quickclash::test {
// Mixed listeners need both transports. Stay below Windows' default dynamic range
// so validation/controller traffic cannot consume a released candidate as a source port.
inline quint16 availableListenerPort() {
    static quint16 next = 20000;
    for (int attempt = 0; attempt < 29000; ++attempt) {
        const auto port = next;
        next = next == 48999 ? 20000 : static_cast<quint16>(next + 1);
        QTcpServer tcp;
        QUdpSocket udp;
        if (tcp.listen(QHostAddress::LocalHost, port) &&
            udp.bind(QHostAddress::LocalHost, port, QAbstractSocket::DontShareAddress))
            return port;
    }
    return 0;
}
} // namespace quickclash::test
