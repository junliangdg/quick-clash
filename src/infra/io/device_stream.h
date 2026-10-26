#pragma once
#include "infra/io/bounded_read.h"
#include <array>
#include <istream>
#include <ostream>
#include <streambuf>

namespace quickclash::infra {
// Synchronous, borrowed, non-sequential input. No second full std::string copy.
class DeviceStreamBuffer final : public std::streambuf {
  public:
    DeviceStreamBuffer(QIODevice& device, qint64 limit)
        : device_(device), expected_(device.size()), limit_(limit) {
        valid_ = device.isOpen() && !device.isSequential() && device.pos() == 0 && expected_ >= 0 &&
                 expected_ <= limit;
    }
    [[nodiscard]] bool complete() const {
        const auto* file = qobject_cast<const QFileDevice*>(&device_);
        return valid_ && total_ == expected_ && device_.size() == expected_ &&
               (!file || file->error() == QFileDevice::NoError);
    }

  protected:
    int_type underflow() override {
        if (!valid_)
            return traits_type::eof();
        const auto count =
            device_.read(chunk_.data(), std::min(kReadChunkSize, limit_ - total_ + 1));
        if (count < 0 || (count == 0 && total_ != expected_) || total_ + count > limit_) {
            valid_ = false;
            return traits_type::eof();
        }
        if (count == 0)
            return traits_type::eof();
        total_ += count;
        setg(chunk_.data(), chunk_.data(), chunk_.data() + count);
        return traits_type::to_int_type(*gptr());
    }

  private:
    QIODevice& device_;
    qint64 expected_;
    qint64 limit_;
    qint64 total_ = 0;
    bool valid_ = false;
    std::array<char, static_cast<std::size_t>(kReadChunkSize)> chunk_;
};
// YAML's ostream emitter writes through this budget before enlarging any output buffer.
class DeviceOutputBuffer final : public std::streambuf {
  public:
    DeviceOutputBuffer(QIODevice& output, qint64 limit) : output_(output), limit_(limit) {}
    [[nodiscard]] qint64 written() const {
        return written_;
    }

  protected:
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        if (size < 0 || size > limit_ - written_)
            return 0;
        std::streamsize done = 0;
        while (done < size) {
            const auto count = std::min<qint64>(size - done, kReadChunkSize);
            if (output_.write(data + done, count) != count)
                return done;
            written_ += count;
            done += count;
        }
        return done;
    }
    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof()))
            return traits_type::not_eof(value);
        const auto byte = traits_type::to_char_type(value);
        return xsputn(&byte, 1) == 1 ? value : traits_type::eof();
    }

  private:
    QIODevice& output_;
    qint64 limit_;
    qint64 written_ = 0;
};
} // namespace quickclash::infra
