#pragma once
#include "domain/contracts.h"
#include <QJsonObject>
#include <QLockFile>
#include <memory>

namespace quickclash::infra {
// Owned by one worker. The lock covers settings, manifests, credentials and journals.
class JsonStore final {
  public:
    explicit JsonStore(QString root);
    [[nodiscard]] domain::CommandResult open();
    [[nodiscard]] bool readOnly() const {
        return readOnly_;
    }
    [[nodiscard]] const QString& root() const {
        return root_;
    }
    [[nodiscard]] domain::Result<QString> path(const QString& reference) const;
    [[nodiscard]] domain::Result<std::optional<QJsonObject>> read(const QString& reference);
    [[nodiscard]] domain::CommandResult write(const QString& reference, QJsonObject value);
    [[nodiscard]] domain::Result<QByteArray> readBytes(const QString& reference,
                                                       qsizetype limit = 16 * 1024 * 1024) const;
    [[nodiscard]] domain::CommandResult writeBytes(const QString& reference,
                                                   const QByteArray& bytes, bool immutable = false);
    [[nodiscard]] domain::CommandResult remove(const QString& reference);
    [[nodiscard]] domain::Result<QStringList> files(const QString& directory) const;
    void freeze() {
        readOnly_ = true;
    }

  private:
    QString root_;
    std::unique_ptr<QLockFile> lock_;
    bool opened_ = false;
    bool readOnly_ = false;
};
[[nodiscard]] domain::Error storageError(const char* code);
// Windows protected current-user/SYSTEM ACL; POSIX owner-only permissions elsewhere.
[[nodiscard]] domain::CommandResult restrictToCurrentUser(const QString& path, bool directory);
} // namespace quickclash::infra
