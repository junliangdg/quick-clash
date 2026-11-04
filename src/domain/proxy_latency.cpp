#include "domain/proxy_latency.h"
#include <QCryptographicHash>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace quickclash::domain {
namespace {
QDateTime timestamp(const QJsonObject& record) {
    return QDateTime::fromString(record.value("time").toString(), Qt::ISODateWithMs);
}
QJsonObject aged(QJsonObject record, const QDateTime& now) {
    const auto state = record.value("state").toString();
    if (state != "Queued" && state != "Testing" && timestamp(record).secsTo(now) >= 600)
        record.insert("state", "Stale");
    return record;
}
} // namespace
QString ProxyLatency::targetHash(const QString& target) {
    return QString::fromLatin1(
        QCryptographicHash::hash(target.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString ProxyLatency::text(const QJsonObject& observation) {
    if (observation.value("state") == "Success")
        return QString::number(observation.value("delayMs").toInt()) + " ms";
    return observation.value("state").toString();
}
bool ProxyLatency::insert(const QString& name, QJsonObject observation) {
    auto targets = records_.value(name).toObject();
    const auto key = observation.value("targetHash").toString();
    const auto old = targets.value(key).toObject();
    if (!old.isEmpty() && timestamp(old) >= timestamp(observation))
        return false;
    targets.insert(key, observation);
    records_.insert(name, targets);
    trim(name);
    return true;
}
void ProxyLatency::trim(const QString& name) {
    auto records = records_.value(name).toObject();
    auto manual = manual_.value(name).toObject();
    auto targets = records;
    for (auto it = manual.begin(); it != manual.end(); ++it)
        targets.insert(it.key(), it.value());
    while (targets.size() > 8) {
        auto oldest = targets.end();
        for (auto it = targets.begin(); it != targets.end(); ++it) {
            const auto value = manual.value(it.key()).toObject();
            if (value.value("state") == "Queued" || value.value("state") == "Testing")
                continue;
            if (oldest == targets.end() ||
                timestamp(it.value().toObject()) < timestamp(oldest.value().toObject()))
                oldest = it;
        }
        if (oldest == targets.end())
            break; // Runtime admits at most eight active targets per node.
        records.remove(oldest.key());
        manual.remove(oldest.key());
        targets.erase(oldest);
    }
    records_.insert(name, records);
    if (!manual.isEmpty())
        manual_.insert(name, manual);
    else
        manual_.remove(name);
}
bool ProxyLatency::ingest(const QJsonObject& proxies, const QDateTime& now) {
    bool changed = false;
    QSet<QString> names;
    for (auto it = proxies.begin(); it != proxies.end() && names.size() < 5500; ++it) {
        const auto proxy = it.value().toObject();
        if (proxy.contains("all") || it.key() == "DIRECT" || it.key() == "REJECT")
            continue;
        names.insert(it.key());
        const auto ingestHistory = [&](const QJsonArray& history, const QString& hash) {
            // The pinned core keeps ten observations. Parse only that bounded tail.
            for (qsizetype i = std::max<qsizetype>(0, history.size() - 10); i < history.size();
                 ++i) {
                const auto value = history[i].toObject();
                const auto time = timestamp(value);
                const auto delay = value.value("delay").toDouble(-1);
                if (!time.isValid() || time > now.addSecs(5) || delay < 0 || delay > 65535 ||
                    std::floor(delay) != delay)
                    continue;
                const auto cancelled = manual_.value(it.key()).toObject().value(hash).toObject();
                if (cancelled.value("state") == "Cancelled" &&
                    cancelled.value("targetHash") == hash &&
                    time <= timestamp(cancelled).addSecs(5))
                    continue; // The controller may finish a probe after local cancellation.
                changed |= insert(it.key(), {{"targetHash", hash},
                                             {"source", "Core"},
                                             {"time", time.toUTC().toString(Qt::ISODateWithMs)},
                                             {"state", delay > 0 ? "Success" : "Failed"},
                                             {"delayMs", static_cast<int>(delay)}});
            }
        };
        const auto extra = proxy.value("extra").toObject();
        // Prefer target-specific histories; generic history has no reliable target.
        if (extra.isEmpty())
            ingestHistory(proxy.value("history").toArray(), {});
        else {
            for (auto target = extra.begin(); target != extra.end(); ++target)
                if (target.key().size() <= 8192)
                    ingestHistory(target.value().toObject().value("history").toArray(),
                                  targetHash(target.key()));
        }
    }
    QSet<QString> pendingNames;
    for (auto it = manual_.constBegin(); it != manual_.constEnd(); ++it) {
        const auto targets = it.value().toObject();
        for (auto target = targets.constBegin(); target != targets.constEnd(); ++target) {
            const auto state = target.value().toObject().value("state").toString();
            if (state == "Queued" || state == "Testing") {
                pendingNames.insert(it.key());
                break;
            }
        }
    }
    for (auto* values : {&records_, &manual_}) {
        for (auto it = values->begin(); it != values->end();) {
            // A concurrent group refresh can omit a node whose request is still
            // pending. Keep its state until the test finishes or is cancelled.
            if (!names.contains(it.key()) && !pendingNames.contains(it.key())) {
                it = values->erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}
void ProxyLatency::manual(const QString& name, const QString& target, const QString& state,
                          const QDateTime& time, int delay) {
    QJsonObject observation{{"targetHash", targetHash(target)},
                            {"source", "Manual"},
                            {"time", time.toUTC().toString(Qt::ISODateWithMs)},
                            {"state", state}};
    if (delay >= 0)
        observation.insert("delayMs", delay);
    auto manual = manual_.value(name).toObject();
    manual.insert(targetHash(target), observation);
    manual_.insert(name, manual);
    if (state == "Success" || state == "Failed" || state == "Timeout")
        insert(name, observation);
    trim(name);
}
void ProxyLatency::invalidateManual(const QString& target) {
    const auto hash = target.isEmpty() ? QString{} : targetHash(target);
    for (auto it = manual_.begin(); it != manual_.end(); ++it) {
        auto values = it.value().toObject();
        for (auto value = values.begin(); value != values.end(); ++value) {
            if (!target.isEmpty() && value.key() != hash)
                continue;
            auto record = value.value().toObject();
            record.insert("state", "Stale");
            value.value() = record;
        }
        it.value() = values;
        auto targets = records_.value(it.key()).toObject();
        for (auto entry = targets.begin(); entry != targets.end(); ++entry) {
            auto record = entry.value().toObject();
            if (record.value("source") == "Manual" && (target.isEmpty() || entry.key() == hash)) {
                record.insert("state", "Stale");
                entry.value() = record;
            }
        }
        records_.insert(it.key(), targets);
    }
}
void ProxyLatency::clear() {
    records_ = {};
    manual_ = {};
}
QJsonObject ProxyLatency::details(const QDateTime& now) const {
    QJsonObject result;
    QSet<QString> names;
    for (auto it = records_.begin(); it != records_.end(); ++it)
        names.insert(it.key());
    for (auto it = manual_.begin(); it != manual_.end(); ++it)
        names.insert(it.key());
    for (const auto& name : names) {
        auto targets = records_.value(name).toObject();
        const auto manual = manual_.value(name).toObject();
        for (auto it = manual.begin(); it != manual.end(); ++it) {
            const auto record = it.value().toObject();
            if (record.value("state") == "Queued" || record.value("state") == "Testing" ||
                !targets.contains(it.key()) ||
                timestamp(record) >= timestamp(targets.value(it.key()).toObject()))
                targets.insert(it.key(), record);
        }
        QJsonObject latest;
        bool pending = false;
        QJsonArray records;
        for (auto it = targets.begin(); it != targets.end(); ++it) {
            const auto record = it.value().toObject();
            records.append(aged(record, now));
            const bool active =
                record.value("state") == "Queued" || record.value("state") == "Testing";
            if (latest.isEmpty() || (active && !pending) ||
                (active == pending && timestamp(record) > timestamp(latest))) {
                latest = record;
                pending = active;
            }
        }
        result.insert(name, QJsonObject{{"latest", aged(latest, now)}, {"records", records}});
    }
    return result;
}
} // namespace quickclash::domain
