#include "infra/storage/geo_resource_writer.h"
#include "infra/io/bounded_read.h"
#include "infra/storage/json_store.h"
#include <QFileInfo>
#include <QSaveFile>

namespace quickclash::infra {
namespace {
constexpr qint64 kGeoLimit = 32 * 1024 * 1024;
} // namespace
GeoResourceWriter::GeoResourceWriter(JsonStore& store, const QString& temporaryTemplate)
    : store_(store), temporary_(temporaryTemplate) {}
domain::CommandResult GeoResourceWriter::open() {
    if (!temporary_.open())
        return fail("DataWriteFailed");
    const auto secured = restrictToCurrentUser(temporary_.fileName(), false);
    if (const auto* error = std::get_if<domain::Error>(&secured))
        return fail(error->code.toLatin1().constData());
    active_ = true;
    return std::monostate{};
}
domain::CommandResult GeoResourceWriter::cancel() {
    active_ = false;
    if (!failure_)
        failure_ = storageError("OperationUnavailable");
    temporary_.close();
    if (!temporary_.fileName().isEmpty() && QFileInfo::exists(temporary_.fileName()) &&
        !temporary_.remove())
        return storageError("DataRemoveFailed");
    return std::monostate{};
}
domain::Error GeoResourceWriter::fail(const char* code) {
    failure_ = storageError(code);
    const auto cleaned = cancel();
    if (const auto* error = std::get_if<domain::Error>(&cleaned))
        failure_ = *error;
    return *failure_;
}
domain::CommandResult GeoResourceWriter::consume(QIODevice& source) {
    if (!active_)
        return failure_.value_or(storageError("OperationUnavailable"));
    const auto status = consumeAvailable(source, total_, kGeoLimit, [this](QByteArrayView chunk) {
        if (total_ > kGeoLimit)
            return true; // The overflow sentinel is consumed, never stored or committed.
        if (temporary_.write(chunk.data(), chunk.size()) != chunk.size())
            return false;
        checksum_.addData(chunk);
        return true;
    });
    if (status != ReadStatus::Complete)
        return fail(status == ReadStatus::LimitExceeded ? "DataLimitExceeded" : "DataWriteFailed");
    return std::monostate{};
}
domain::Result<StoredGeoResource> GeoResourceWriter::finish(const QString& fileName) {
    if (!active_)
        return failure_.value_or(storageError("OperationUnavailable"));
    if (fileName != QStringLiteral("geoip.dat") && fileName != QStringLiteral("geosite.dat"))
        return fail("UnsafeDataPath");
    if (total_ == 0 || !temporary_.flush() || temporary_.size() != total_ || !temporary_.seek(0))
        return fail("DataReadFailed");
    const auto digest = checksum_.result().toHex();
    const auto reference =
        QStringLiteral("data/geo/%1/%2").arg(QString::fromLatin1(digest), fileName);
    const auto resolved = store_.prepareWrite(reference, true);
    if (const auto* error = std::get_if<domain::Error>(&resolved))
        return fail(error->code.toLatin1().constData());
    QSaveFile output(std::get<QString>(resolved));
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        return fail("DataWriteFailed");
    QCryptographicHash verified(QCryptographicHash::Sha256);
    qint64 copied = 0;
    const auto status = consumeAvailable(temporary_, copied, kGeoLimit, [&](QByteArrayView chunk) {
        if (copied > kGeoLimit || output.write(chunk.data(), chunk.size()) != chunk.size())
            return false;
        verified.addData(chunk);
        return true;
    });
    if (status != ReadStatus::Complete || copied != total_ || temporary_.size() != total_ ||
        temporary_.error() != QFileDevice::NoError || verified.result().toHex() != digest)
        return fail("GeoResourceIntegrityFailed");
    const auto committed = store_.commitWrite(output);
    if (const auto* error = std::get_if<domain::Error>(&committed))
        return fail(error->code.toLatin1().constData());
    const auto cleaned = cancel();
    if (const auto* error = std::get_if<domain::Error>(&cleaned))
        return *error;
    return StoredGeoResource{reference, QString::fromLatin1(digest)};
}
} // namespace quickclash::infra
