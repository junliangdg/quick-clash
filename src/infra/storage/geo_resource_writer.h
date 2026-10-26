#pragma once
#include "domain/contracts.h"
#include <QCryptographicHash>
#include <QIODevice>
#include <QTemporaryFile>
#include <optional>

namespace quickclash::infra {
class JsonStore;
struct StoredGeoResource {
    QString reference;
    QString sha256;
};
// Worker-thread only. JsonStore must outlive this writer; callbacks share only the writer.
class GeoResourceWriter final {
  public:
    [[nodiscard]] domain::CommandResult consume(QIODevice& source);
    [[nodiscard]] domain::Result<StoredGeoResource> finish(const QString& fileName);
    [[nodiscard]] domain::CommandResult cancel();

  private:
    friend class JsonStore;
    GeoResourceWriter(JsonStore& store, const QString& temporaryTemplate);
    [[nodiscard]] domain::CommandResult open();
    [[nodiscard]] domain::Error fail(const char* code);
    JsonStore& store_;
    QTemporaryFile temporary_;
    QCryptographicHash checksum_{QCryptographicHash::Sha256};
    qint64 total_ = 0;
    bool active_ = false;
    std::optional<domain::Error> failure_;
};
} // namespace quickclash::infra
