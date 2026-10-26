#include "infra/io/bounded_read.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
class ShortDevice final : public QIODevice {
  public:
    explicit ShortDevice(qint64 size) : remaining(size) {
        open(ReadOnly | Unbuffered);
    }
    qint64 remaining;
    qint64 maxRequest = 0;
    bool fail = false;
    bool zero = false;
    qint64 bytesAvailable() const override {
        return remaining;
    }
    bool isSequential() const override {
        return true;
    }

  protected:
    qint64 readData(char* data, qint64 maxSize) override {
        maxRequest = std::max(maxRequest, maxSize);
        if (fail)
            return -1;
        if (zero)
            return 0;
        const auto count = std::min({remaining, maxSize, qint64{7}});
        std::memset(data, 'x', static_cast<size_t>(count));
        remaining -= count;
        return count;
    }
    qint64 writeData(const char*, qint64) override {
        return -1;
    }
};
class ShortFile final : public QFile {
  public:
    enum class Change { None, Grow, Shrink };
    explicit ShortFile(const QString& path, Change change = Change::None)
        : QFile(path), change_(change) {}

  protected:
    qint64 readData(char* data, qint64 maxSize) override {
        if (change_ != Change::None) {
            QFile changed(fileName());
            if (!changed.open(QIODevice::ReadWrite) ||
                !changed.resize(change_ == Change::Grow ? size() + 1 : 1))
                return -1;
            change_ = Change::None;
        }
        return QFile::readData(data, std::min(maxSize, qint64{7}));
    }

  private:
    Change change_;
};
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    using namespace quickclash::infra;
    try {
        QByteArray bytes;
        ShortDevice small(23);
        check(appendAvailable(small, bytes, 48 * 1024 * 1024) && bytes == QByteArray(23, 'x'),
              "short reads assembled");
        check(small.maxRequest <= kReadChunkSize && bytes.capacity() < 128 * 1024,
              "policy limit is not an allocation request");
        ShortDevice exact(77);
        bytes.clear();
        check(appendAvailable(exact, bytes, 77) && bytes.size() == 77, "exact limit accepted");
        ShortDevice excess(1000);
        bytes.clear();
        check(!appendAvailable(excess, bytes, 77) && bytes.size() == 78 && excess.remaining == 922,
              "only limit plus one consumed");
        check(!appendAvailable(excess, bytes, 77) && bytes.size() == 78,
              "exceeded buffer is terminal without further reads");
        for (bool zero : {false, true}) {
            ShortDevice broken(1);
            broken.fail = !zero;
            broken.zero = zero;
            bytes.clear();
            check(!appendAvailable(broken, bytes, 10), "error or stalled short read rejected");
        }
        ShortDevice source(10);
        qint64 total = 0;
        check(consumeAvailable(source, total, 10, [](QByteArrayView) { return false; }) ==
                  ReadStatus::ReadFailed,
              "sink failure aborts consumption");
        ShortDevice empty(0);
        bytes.clear();
        check(appendAvailable(empty, bytes, 0), "empty exact limit accepted");
        check(!appendAvailable(empty, bytes, -1), "negative limit rejected");
        QTemporaryFile file;
        check(file.open() && file.write("abc", 3) == 3 && file.seek(0), "file fixture");
        auto read = readFileBounded(file, 48 * 1024 * 1024);
        check(read.ok && read.bytes == "abc" && read.bytes.capacity() < 128 * 1024,
              "small file bounded without policy allocation");
        check(file.seek(0) && !readFileBounded(file, 2).ok, "oversized file rejected");
        check(file.resize(0) && file.seek(0) && file.write(QByteArray(200, 's')) == 200 &&
                  file.flush(),
              "short file fixture");
        {
            ShortFile shortFile(file.fileName());
            check(shortFile.open(QIODevice::ReadOnly | QIODevice::Unbuffered), "short file open");
            const auto loaded = readFileBounded(shortFile, 200);
            check(loaded.ok && loaded.bytes == QByteArray(200, 's'), "file short reads assembled");
        }
        for (auto change : {ShortFile::Change::Grow, ShortFile::Change::Shrink}) {
            ShortFile changing(file.fileName(), change);
            check(changing.open(QIODevice::ReadOnly | QIODevice::Unbuffered), "changing file open");
            check(!readFileBounded(changing, 1024).ok, "file length change rejected");
        }
        std::cout
            << "PASS: bounded requests, short reads, exact/plus-one limits, errors and files\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
