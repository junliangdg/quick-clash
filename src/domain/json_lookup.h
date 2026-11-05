#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QStringView>

namespace quickclash::domain {
// Runtime node/group names are arbitrary Unicode strings. Compare complete keys
// with explicit lengths; keep JSON storage shared and lookup logarithmic.
[[nodiscard]] inline QJsonValue jsonValue(const QJsonObject& object, QStringView name) {
    qsizetype first = 0;
    qsizetype last = object.size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        const auto entry = object.constBegin() + middle;
        const auto key = entry.key();
        const auto order = QStringView{key}.compare(name);
        if (order < 0)
            first = middle + 1;
        else if (order > 0)
            last = middle;
        else
            return entry.value();
    }
    return QJsonValue::Undefined;
}
} // namespace quickclash::domain
