#pragma once
#include "infra/storage/json_store.h"
#include <QHash>
#include <QIODevice>
#include <QTemporaryFile>
#include <map>

namespace quickclash::infra {
// One worker-owned serial download transaction. Keys are metadata, never filesystem paths.
class ProviderStage final {
  public:
    explicit ProviderStage(JsonStore& store) : store_(store) {}
    [[nodiscard]] domain::CommandResult begin(const QString& key);
    [[nodiscard]] domain::CommandResult consume(QIODevice& input);
    [[nodiscard]] domain::CommandResult finish();
    void cancel();
    [[nodiscard]] QHash<QString, QIODevice*> devices() const;
    [[nodiscard]] qint64 totalBytes() const {
        return total_;
    }
    [[nodiscard]] qsizetype fileCount() const {
        return static_cast<qsizetype>(files_.size());
    }

  private:
    JsonStore& store_;
    std::map<QString, std::unique_ptr<QTemporaryFile>> files_;
    QTemporaryFile* current_ = nullptr;
    qint64 itemBytes_ = 0;
    qint64 total_ = 0;
    bool cancelled_ = false;
};
} // namespace quickclash::infra
