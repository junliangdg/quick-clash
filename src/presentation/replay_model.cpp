#include "presentation/replay_model.h"
#include <algorithm>

namespace quickclash::presentation {
ReplayModel::ReplayModel(Kind kind, QObject* parent) : QAbstractListModel(parent), kind_(kind) {}
int ReplayModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : (filter_.isEmpty() ? rows_ : static_cast<int>(mapping_.size()));
}
QString ReplayModel::name(int row) const {
    return kind_ == Kind::Proxies ? tr("Sample node %1").arg(row + 1, 4, 10, QLatin1Char('0'))
                                  : tr("Connection %1 · app_%2").arg(row + 1).arg(row % 12);
}
QVariant ReplayModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const int row = filter_.isEmpty() ? index.row() : mapping_.at(index.row());
    switch (role) {
    case Name:
        return name(row);
    case Detail:
        return kind_ == Kind::Proxies
                   ? tr("Example group / Test node · No network connection")
                   : tr("service_%1.example:443 → Sample node %2").arg(row % 99).arg(row % 500 + 1);
    case Status:
        return kind_ == Kind::Proxies ? tr("Replay") : tr("TCP · Replay");
    case Metric:
        return kind_ == Kind::Proxies
                   ? QStringLiteral("%1 ms").arg(30 + row % 180)
                   : QStringLiteral("%1 KiB").arg((row * 17 + sequence_ * 11) % 4096);
    default:
        return {};
    }
}
QHash<int, QByteArray> ReplayModel::roleNames() const {
    return {{Name, "name"}, {Detail, "detail"}, {Status, "status"}, {Metric, "metric"}};
}
void ReplayModel::setRows(int rows) {
    rows = std::clamp(rows, 0, 100000);
    if (rows_ == rows) {
        return;
    }
    beginResetModel();
    rows_ = rows;
    rebuild();
    endResetModel();
    emit countChanged();
}
void ReplayModel::setFilter(const QString& filter) {
    const auto bounded = filter.left(128);
    if (filter_ == bounded) {
        return;
    }
    beginResetModel();
    filter_ = bounded;
    rebuild();
    endResetModel();
    emit countChanged();
}
void ReplayModel::rebuild() {
    mapping_.clear();
    if (filter_.isEmpty()) {
        return;
    }
    for (int row = 0; row < rows_; ++row) {
        if (name(row).contains(filter_, Qt::CaseInsensitive)) {
            mapping_.append(row);
        }
    }
}
void ReplayModel::tick() {
    sequence_ = (sequence_ + 1) % 100000;
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {Metric});
    }
}
} // namespace quickclash::presentation
