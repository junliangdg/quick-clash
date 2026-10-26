#include "infra/storage/geo_resource_writer.h"
#include "infra/storage/json_store.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> bool good(const T& value) {
    return !std::holds_alternative<quickclash::domain::Error>(value);
}
std::unique_ptr<quickclash::infra::GeoResourceWriter> begin(quickclash::infra::JsonStore& store) {
    auto result = store.beginGeoWrite();
    check(good(result), "begin controlled Geo staging");
    return std::move(std::get<std::unique_ptr<quickclash::infra::GeoResourceWriter>>(result));
}
bool feed(quickclash::infra::GeoResourceWriter& writer, QByteArray bytes) {
    QBuffer source(&bytes);
    check(source.open(QIODevice::ReadOnly), "source open");
    return good(writer.consume(source));
}
void clean(const QString& root) {
    check(QDir(root + "/cache/geo").entryList(QDir::Files | QDir::Hidden).isEmpty(),
          "staging cleaned");
}
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    using namespace quickclash;
    try {
        QTemporaryDir directory;
        check(directory.isValid(), "temporary root");
        infra::JsonStore store(directory.path());
        check(!good(store.beginGeoWrite()), "unopened store rejected");
        check(good(store.open()), "store opened");
        QString original;
        {
            auto writer = begin(store);
            check(feed(*writer, "abc"), "partial body");
            check(feed(*writer, "def"), "second short body");
            auto saved = writer->finish("geoip.dat");
            check(good(saved), "atomic commit");
            const auto resource = std::get<infra::StoredGeoResource>(saved);
            original = resource.reference;
            check(resource.sha256 ==
                      QString::fromLatin1(
                          QCryptographicHash::hash("abcdef", QCryptographicHash::Sha256).toHex()),
                  "stream digest");
            check(std::get<QByteArray>(store.readBytes(original)) == "abcdef", "committed body");
            check(!good(writer->finish("geoip.dat")), "double commit rejected");
            clean(directory.path());
        }
        {
            auto writer = begin(store);
            check(feed(*writer, "abcdef"), "duplicate body");
            check(!good(writer->finish("geoip.dat")), "immutable resource not overwritten");
            check(std::get<QByteArray>(store.readBytes(original)) == "abcdef", "old resource kept");
            clean(directory.path());
        }
        {
            auto writer = begin(store);
            check(feed(*writer, "cancel"), "cancel partial input");
            check(good(writer->cancel()) && good(writer->cancel()), "idempotent cancellation");
            check(!feed(*writer, "late") && !good(writer->finish("geoip.dat")),
                  "late reads and commits rejected");
            clean(directory.path());
        }
        {
            auto writer = begin(store);
            check(feed(*writer, "abandoned"), "abandoned partial body");
        }
        clean(directory.path());
        {
            auto writer = begin(store);
            check(!good(writer->finish("geoip.dat")), "empty response rejected");
            clean(directory.path());
        }
        {
            auto writer = begin(store);
            check(feed(*writer, "unsafe"), "path fixture");
            check(!good(writer->finish("../escape")), "unsafe filename rejected");
            clean(directory.path());
        }
        {
            auto writer = begin(store);
            check(feed(*writer, "tamper"), "integrity fixture");
            const QDir cache(directory.path() + "/cache/geo");
            const auto files = cache.entryList(QDir::Files | QDir::Hidden);
            check(files.size() == 1, "one controlled temporary file");
            QFile altered(cache.filePath(files.first()));
            // Grow beyond buffered writes, so the fixture does not depend on QFile flushing.
            check(altered.open(QIODevice::ReadWrite) && altered.resize(128 * 1024),
                  "tamper fixture resize");
            altered.close();
            check(!good(writer->finish("geosite.dat")), "changed staging not committed");
            clean(directory.path());
        }
        const QByteArray chunk(64 * 1024, 'g');
        for (bool overflow : {false, true}) {
            auto writer = begin(store);
            for (int i = 0; i < 512; ++i)
                check(feed(*writer, chunk), "stream up to exact 32 MiB");
            if (overflow) {
                check(!feed(*writer, "x"), "32 MiB plus one rejected");
                check(!good(writer->finish("geosite.dat")), "overflow cannot commit");
            } else {
                auto saved = writer->finish("geosite.dat");
                check(good(saved), "32 MiB Geo succeeds beyond ordinary 16 MiB limit");
                const auto resolved =
                    store.path(std::get<infra::StoredGeoResource>(saved).reference);
                check(QFileInfo(std::get<QString>(resolved)).size() == 32 * 1024 * 1024,
                      "full Geo file committed");
            }
            clean(directory.path());
        }
        check(!good(store.writeBytes("config/large.bin", QByteArray(16 * 1024 * 1024 + 1, 'c'))),
              "ordinary configuration limit remains 16 MiB");
        {
            auto writer = begin(store);
            check(feed(*writer, "freeze"), "freeze fixture");
            store.freeze();
            check(!good(writer->finish("geoip.dat")), "read-only transition prevents commit");
            clean(directory.path());
        }
        std::cout << "PASS: Geo streaming, bounds, digest, immutable commit, cancel/late/cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
