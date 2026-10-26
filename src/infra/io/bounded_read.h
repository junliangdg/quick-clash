#pragma once
#include <QByteArray>
#include <QByteArrayView>
#include <QFileDevice>
#include <QIODevice>
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace quickclash::infra {
inline constexpr qint64 kReadChunkSize = 64 * 1024;
enum class ReadStatus { Complete, LimitExceeded, ReadFailed };

// The sink borrows the chunk only for this call. At most limit + 1 bytes are consumed.
// Callers own cancellation/generation checks and must not commit on a non-Complete result.
template <class Sink>
[[nodiscard]] ReadStatus consumeAvailable(QIODevice& device, qint64& total, qint64 limit,
                                          Sink&& sink) {
    if (limit < 0 || limit == std::numeric_limits<qint64>::max() || total < 0)
        return ReadStatus::ReadFailed;
    if (total > limit)
        return ReadStatus::LimitExceeded;
    std::array<char, static_cast<std::size_t>(kReadChunkSize)> chunk;
    for (;;) {
        const auto available = device.bytesAvailable();
        if (available < 0)
            return ReadStatus::ReadFailed;
        if (available == 0)
            return ReadStatus::Complete;
        const auto request = std::min({available, kReadChunkSize, limit - total + 1});
        const auto count = device.read(chunk.data(), request);
        if (count <= 0 || count > request)
            return ReadStatus::ReadFailed;
        total += count;
        if (!sink(QByteArrayView(chunk.data(), static_cast<qsizetype>(count))))
            return ReadStatus::ReadFailed;
        if (total > limit)
            return ReadStatus::LimitExceeded;
    }
}
[[nodiscard]] inline bool appendAvailable(QIODevice& device, QByteArray& bytes, qint64 limit) {
    qint64 total = bytes.size();
    return consumeAvailable(device, total, limit, [&bytes](QByteArrayView chunk) {
               bytes.append(chunk.data(), chunk.size());
               return true;
           }) == ReadStatus::Complete;
}
struct FileReadResult {
    QByteArray bytes;
    bool ok = false;
};
[[nodiscard]] inline FileReadResult readFileBounded(QFileDevice& file, qint64 limit) {
    FileReadResult result;
    const auto expected = file.size();
    if (!file.isOpen() || file.isSequential() || file.pos() != 0 || expected < 0 ||
        expected > limit)
        return result;
    result.ok = appendAvailable(file, result.bytes, limit) && result.bytes.size() == expected &&
                file.size() == expected && file.error() == QFileDevice::NoError;
    return result;
}
} // namespace quickclash::infra
