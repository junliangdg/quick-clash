#pragma once
#include "domain/contracts.h"
#include <QJsonObject>
#include <QObject>
#include <functional>

namespace quickclash::infra {
// Only the LocalSystem service uses this transport to the System-only Mihomo pipe.
void pipeHttp(QObject* context, const QString& pipe, const QByteArray& method,
              const QByteArray& path, const QJsonObject& body,
              std::function<void(domain::Result<QJsonObject>)> handler);
// Owner deletes the returned stream when telemetry demand is released.
[[nodiscard]] QObject* pipeJsonStream(QObject* context, const QString& pipe, const QByteArray& path,
                                      std::function<void(const QJsonObject&)> handler);
} // namespace quickclash::infra
