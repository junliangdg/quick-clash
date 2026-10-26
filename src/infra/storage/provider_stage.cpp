#include "infra/storage/provider_stage.h"
#include "infra/io/bounded_read.h"

namespace quickclash::infra {
domain::CommandResult ProviderStage::begin(const QString& key) {
    if (cancelled_ || current_ || key.isEmpty() || key.size() > 1024 || files_.contains(key) ||
        files_.size() >= 32)
        return storageError("ProviderLimitExceeded");
    auto result = store_.beginConfigTemporary();
    if (const auto* failure = std::get_if<domain::Error>(&result))
        return *failure;
    auto file = std::get<std::unique_ptr<QTemporaryFile>>(std::move(result));
    current_ = file.get();
    itemBytes_ = 0;
    files_.emplace(key, std::move(file));
    return std::monostate{};
}
domain::CommandResult ProviderStage::consume(QIODevice& input) {
    if (cancelled_ || !current_ || store_.readOnly())
        return storageError("OperationUnavailable");
    const auto limit = std::min<qint64>(4 * 1024 * 1024, itemBytes_ + 16 * 1024 * 1024 - total_);
    const auto before = itemBytes_;
    const auto status = consumeAvailable(input, itemBytes_, limit, [&](QByteArrayView chunk) {
        if (itemBytes_ > limit)
            return true; // Consume the overflow sentinel without storing it.
        return current_->write(chunk.data(), chunk.size()) == chunk.size();
    });
    total_ += itemBytes_ - before;
    if (status != ReadStatus::Complete) {
        cancel();
        return storageError(status == ReadStatus::LimitExceeded ? "ProviderLimitExceeded"
                                                                : "DataWriteFailed");
    }
    return std::monostate{};
}
domain::CommandResult ProviderStage::finish() {
    if (cancelled_ || !current_ || store_.readOnly())
        return storageError("OperationUnavailable");
    if (itemBytes_ == 0 || !current_->flush() || current_->size() != itemBytes_ ||
        !current_->seek(0)) {
        cancel();
        return storageError("DataReadFailed");
    }
    current_ = nullptr;
    return std::monostate{};
}
void ProviderStage::cancel() {
    cancelled_ = true;
    current_ = nullptr;
    files_.clear(); // QTemporaryFile removes each private file, including the partial item.
}
QHash<QString, QIODevice*> ProviderStage::devices() const {
    QHash<QString, QIODevice*> result;
    if (!cancelled_ && !current_)
        for (const auto& [key, file] : files_)
            result.insert(key, file.get());
    return result;
}
} // namespace quickclash::infra
