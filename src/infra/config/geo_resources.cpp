#include "infra/config/geo_resources.h"
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
        const auto root = YAML::Load(semantic.toStdString());
        if (root["dns"] && root["dns"]["enable"] && root["dns"]["enable"].as<bool>())
            return true; // DNS fallback defaults can use GeoIP even without a GEOIP rule.
        for (const auto& rule : root["rules"]) {
            const auto value = rule.as<std::string>();
            if (value.starts_with("GEOIP,") || value.starts_with("GEOSITE,"))
                return true;
        }
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
QJsonObject geoResourceManifest(const QByteArray& semantic) {
    QJsonArray resources;
    if (needed(semantic))
        for (const auto& asset : kAssets)
            resources.append(
                QJsonObject{{QStringLiteral("name"), QString::fromLatin1(asset.target)},
                            {QStringLiteral("sha256"), QString::fromLatin1(asset.sha256)}});
    return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("resources"), resources}};
}
domain::CommandResult prepareGeoResources(const QByteArray& semantic, const QString& directory) {
    if (!needed(semantic))
        return std::monostate{};
    const QDir source(sourceDirectory());
    for (const auto& asset : kAssets) {
        QFile input(source.filePath(QString::fromLatin1(asset.source)));
        if (!input.open(QIODevice::ReadOnly) || input.size() > 32 * 1024 * 1024)
            return failure();
        const auto bytes = input.readAll();
        if (input.error() != QFileDevice::NoError ||
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != asset.sha256)
            return failure();
        QSaveFile output(QDir(directory).filePath(QString::fromLatin1(asset.target)));
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
            !output.commit())
            return failure();
    }
    return std::monostate{};
}
} // namespace quickclash::infra
