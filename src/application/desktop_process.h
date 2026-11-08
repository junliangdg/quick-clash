#pragma once
#include "domain/contracts.h"
#include <QJsonObject>
#include <QObject>
#include <QStringList>

namespace quickclash::application {
// Desktop process ownership is replaceable; no native handles cross this boundary.
class IDesktopProcess : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    [[nodiscard]] virtual domain::CommandResult start(const QString& executable,
                                                      const QStringList& arguments,
                                                      const QString& directory,
                                                      bool isolated = false) = 0;
    // Failure preserves ownership; callers must not clean files or launch a replacement.
    virtual domain::CommandResult stop() = 0;
    [[nodiscard]] virtual QJsonObject exitDiagnostics() const {
        return {};
    }
    // Resolve a child-created local pipe from the parent's namespace.
    [[nodiscard]] virtual QString localPipePath(const QString& path) const {
        return path;
    }
    [[nodiscard]] virtual bool running() const = 0;
    [[nodiscard]] virtual quint32 pid() const = 0;
    [[nodiscard]] virtual QJsonObject memory() const = 0;
    [[nodiscard]] virtual bool ownsListener(quint16 port) const = 0;
  signals:
    void finished(int exitCode);
};
} // namespace quickclash::application
