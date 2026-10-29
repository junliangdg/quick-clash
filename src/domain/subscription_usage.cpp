#include "domain/subscription_usage.h"
#include <QSet>
#include <QTimeZone>
#include <QVariant>

namespace quickclash::domain {
QJsonObject parseSubscriptionUsage(const QByteArray& header) {
    QJsonObject usage;
    if (header.size() > 1024)
        return usage;
    QSet<QByteArray> seen;
    for (const auto& field : header.split(';')) {
        const auto pair = field.trimmed().split('=');
        const auto key = pair.value(0).trimmed();
        if (key != "upload" && key != "download" && key != "total" && key != "expire")
            continue;
        const auto name = QString::fromLatin1(key);
        if (seen.contains(key)) {
            usage.remove(name);
            continue;
        }
        seen.insert(key);
        const auto digits = pair.value(1).trimmed();
        bool valid = pair.size() == 2 && !digits.isEmpty();
        for (const auto digit : digits)
            valid &= digit >= '0' && digit <= '9';
        bool converted = false;
        const auto value = digits.toULongLong(&converted);
        if (!valid || !converted || value > 9007199254740991ULL ||
            (key == "expire" && value > 0 &&
             (value > 253402300799ULL ||
              !QDateTime::fromSecsSinceEpoch(static_cast<qint64>(value), QTimeZone::UTC)
                   .isValid())))
            continue;
        usage.insert(name, QString::number(value));
    }
    return usage;
}
QJsonObject subscriptionUsageStatus(const QJsonObject& usage, const QDateTime& now) {
    // Revalidate saved metadata too, including values from older versions/backups.
    QByteArray header;
    for (const auto* key : {"upload", "download", "total", "expire"}) {
        const auto value = usage.value(QLatin1String(key));
        const auto text = value.toVariant().toString().toUtf8();
        if (!value.isUndefined() && !text.contains(';'))
            header += QByteArray(key) + '=' + text + ';';
    }
    const auto safe = parseSubscriptionUsage(header);
    const bool complete =
        safe.contains(QStringLiteral("upload")) && safe.contains(QStringLiteral("download"));
    const auto used = safe.value(QStringLiteral("upload")).toString().toULongLong() +
                      safe.value(QStringLiteral("download")).toString().toULongLong();
    const auto total = safe.value(QStringLiteral("total")).toString().toULongLong();
    const auto expire = safe.value(QStringLiteral("expire")).toString().toLongLong();
    QJsonObject status{{QStringLiteral("complete"), complete},
                       {QStringLiteral("quota"), total > 0 ? QString::number(total) : QString{}},
                       {QStringLiteral("used"), complete ? QString::number(used) : QString{}},
                       {QStringLiteral("quotaState"), QStringLiteral("Unknown")},
                       {QStringLiteral("expiryState"), QStringLiteral("Unknown")}};
    if (complete && total > 0)
        status.insert(QStringLiteral("quotaState"), used >= total ? QStringLiteral("Exhausted")
                                                    : used * 10 >= total * 9
                                                        ? QStringLiteral("Low")
                                                        : QStringLiteral("Available"));
    if (expire > 0 && now.isValid()) {
        status.insert(QStringLiteral("expiresAt"),
                      QDateTime::fromSecsSinceEpoch(expire, QTimeZone::UTC).toString(Qt::ISODate));
        status.insert(QStringLiteral("expiryState"),
                      expire <= now.toSecsSinceEpoch()               ? QStringLiteral("Expired")
                      : expire - now.toSecsSinceEpoch() <= 7 * 86400 ? QStringLiteral("Soon")
                                                                     : QStringLiteral("Available"));
    }
    return status;
}
} // namespace quickclash::domain
