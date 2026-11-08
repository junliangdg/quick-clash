#include "infra/config/geo_resources.h"
#include "infra/config/config_metadata.h"
#include "infra/io/bounded_read.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <array>
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
namespace {
struct Asset {
    const char* source;
    const char* target;
    const char* sha256;
};
constexpr std::array kAssets{Asset{"geoip.dat", "GeoIP.dat", QUICKCLASH_GEOIP_SHA256},
                             Asset{"geosite.dat", "GeoSite.dat", QUICKCLASH_GEOSITE_SHA256}};
bool needed(const QByteArray& semantic) {
    try {
        const auto metadata = configMetadata(semantic);
        if (!std::holds_alternative<ConfigMetadata>(metadata))
            return true;
        const auto& fields = std::get<ConfigMetadata>(metadata);
        if (fields.geoRule || semantic.contains("GEOIP,") || semantic.contains("GEOSITE,"))
            return true;
        const auto root = YAML::Load(fields.fieldsYaml.toStdString());
        if (root["rule-providers"] && root["rule-providers"].size() > 0)
            return true; // Future core refreshes may introduce GEOIP/GEOSITE rules.
        if (root["dns"] && root["dns"]["enable"] && root["dns"]["enable"].as<bool>())
            return true; // DNS fallback defaults can use GeoIP even without a GEOIP rule.
    } catch (const YAML::Exception&) {
        return true; // Fail closed if a corrupt artifact reaches this boundary.
    }
    return false;
}
QString sourceDirectory() {
    const QDir app(QCoreApplication::applicationDirPath());
    const auto packaged = app.filePath(QStringLiteral("../geo"));
    // A packaged core must never fall back to development files.
    if (QFileInfo::exists(app.filePath(QStringLiteral("../core/mihomo.exe"))))
        return packaged;
#ifdef QUICKCLASH_DEVELOPMENT_GEO
    return QStringLiteral(QUICKCLASH_DEVELOPMENT_GEO);
#else
    return packaged;
#endif
}
domain::Error failure() {
    domain::Error error;
    error.code = QStringLiteral("GeoResourceIntegrityFailed");
    error.userMessageKey = error.code;
    error.stage = QStringLiteral("resources");
    return error;
}
} // namespace
QJsonObject geoResourceManifest(const QByteArray& semantic, const QJsonObject& snapshot) {
    QJsonArray resources;
    if (needed(semantic))
        for (const auto& asset : kAssets) {
            const auto entry = snapshot.value(QLatin1String(asset.source)).toObject();
            auto record =
                QJsonObject{{QStringLiteral("name"), QString::fromLatin1(asset.target)},
                            {QStringLiteral("sha256"), QString::fromLatin1(asset.sha256)}};
            if (!snapshot.isEmpty()) {
                record.insert(QStringLiteral("sha256"), entry.value(QStringLiteral("sha256")));
                record.insert(QStringLiteral("sourceRef"),
                              entry.value(QStringLiteral("sourceRef")));
            }
            resources.append(record);
        }
    return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("resources"), resources}};
}
domain::CommandResult prepareGeoResources(const QByteArray& semantic, const QString& directory,
                                          const QJsonObject& snapshot, const QString& storageRoot,
                                          const std::function<bool(QFile&)>& openSource) {
    if (!needed(semantic))
        return std::monostate{};
    const QDir source(sourceDirectory());
    for (const auto& asset : kAssets) {
        auto path = source.filePath(QString::fromLatin1(asset.source));
        auto expected = QByteArray(asset.sha256);
        if (!snapshot.isEmpty()) {
            const auto entry = snapshot.value(QLatin1String(asset.source)).toObject();
            expected = entry.value(QStringLiteral("sha256")).toString().toLatin1();
            const auto reference = entry.value(QStringLiteral("sourceRef")).toString();
            if (storageRoot.isEmpty() || expected.size() != 64 ||
                QByteArray::fromHex(expected).toHex() != expected ||
                reference != QStringLiteral("data/geo/%1/%2")
                                 .arg(QString::fromLatin1(expected), QLatin1String(asset.source)))
                return failure();
            path = QDir(storageRoot).filePath(reference);
            for (auto info = QFileInfo(path);
                 info.absoluteFilePath() != QDir(storageRoot).absolutePath();
                 info = QFileInfo(info.absolutePath())) {
                if (!info.exists() || info.isSymbolicLink() || info.isJunction() || info.isRoot())
                    return failure();
            }
        }
        QFile input(path);
        if (!(openSource ? openSource(input) : input.open(QIODevice::ReadOnly)) ||
            input.size() <= 0 || input.size() > 32 * 1024 * 1024)
            return failure();
        const auto expectedSize = input.size();
        QSaveFile output(QDir(directory).filePath(QString::fromLatin1(asset.target)));
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly))
            return failure();
        // Hash and copy the same bounded chunks; never retain entire Geo assets in the client.
        QCryptographicHash checksum(QCryptographicHash::Sha256);
        qint64 copied = 0;
        const auto status =
            consumeAvailable(input, copied, 32 * 1024 * 1024, [&](QByteArrayView chunk) {
                if (copied > 32 * 1024 * 1024 ||
                    output.write(chunk.data(), chunk.size()) != chunk.size())
                    return false;
                checksum.addData(chunk);
                return true;
            });
        if (status != ReadStatus::Complete || input.error() != QFileDevice::NoError ||
            copied != expectedSize || input.size() != expectedSize ||
            checksum.result().toHex() != expected || !output.commit())
            return failure();
    }
    return std::monostate{};
}
} // namespace quickclash::infra
