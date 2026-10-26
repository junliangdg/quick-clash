#include "presentation/profile_model.h"
#include <QSet>
#include <QThread>
#include <utility>

namespace quickclash::presentation {
int ProfileModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}
QJsonObject ProfileModel::entryAt(int row) const {
    return row >= 0 && row < count() ? entries_[row] : QJsonObject{};
}
QVariantMap ProfileModel::profile(const QString& id) const {
    return entryAt(positions_.value(id, -1)).toVariantMap();
}
QVariant ProfileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    const auto& entry = entries_[index.row()];
    switch (role) {
    case Identifier:
        return entry.value("id").toString();
    case Entry:
        return entry.toVariantMap();
    case Name:
        return entry.value("name").toString();
    case Usage:
        return entry.value("usage").toVariant();
    case Schedule:
        return entry.value("updateIntervalMinutes").toVariant();
    case Result:
        return entry.value("lastUpdateResult").toVariant();
    case Source:
        return entry.value("secretRef").toVariant();
    case History:
        return entry.value("history").toVariant();
    default:
        return {};
    }
}
QHash<int, QByteArray> ProfileModel::roleNames() const {
    return {{Identifier, "profileId"}, {Entry, "entry"},       {Name, "name"},
            {Usage, "usage"},          {Schedule, "schedule"}, {Result, "result"},
            {Source, "source"},        {History, "history"}};
}
void ProfileModel::rebuildPositions() {
    positions_.clear();
    for (int row = 0; row < count(); ++row)
        positions_.insert(entries_[row].value("id").toString(), row);
}
bool ProfileModel::apply(const QJsonArray& profiles) {
    Q_ASSERT(QThread::currentThread() == thread());
    QSet<QString> wanted;
    for (const auto& value : profiles) {
        const auto id = value.toObject().value("id").toString();
        if (id.isEmpty() || wanted.contains(id))
            return false;
        wanted.insert(id);
    }
    const int previousCount = count();
    bool orderChanged = false;
    QStringList changedIds;
    for (int row = count() - 1; row >= 0; --row) {
        const auto id = entries_[row].value("id").toString();
        if (!wanted.contains(id)) {
            beginRemoveRows({}, row, row);
            entries_.removeAt(row);
            rebuildPositions();
            endRemoveRows();
            changedIds.append(id);
        }
    }
    for (int row = 0; row < profiles.size(); ++row) {
        const auto next = profiles[row].toObject();
        const auto id = next.value("id").toString();
        const int existing = positions_.value(id, -1);
        if (existing < 0) {
            if (row == count()) {
                beginInsertRows({}, row, static_cast<int>(profiles.size()) - 1);
                for (int tail = row; tail < profiles.size(); ++tail) {
                    const auto value = profiles[tail].toObject();
                    entries_.append(value);
                    changedIds.append(value.value("id").toString());
                }
                rebuildPositions();
                endInsertRows();
                break;
            }
            beginInsertRows({}, row, row);
            entries_.insert(row, next);
            rebuildPositions();
            endInsertRows();
            changedIds.append(id);
            continue;
        }
        if (existing != row) {
            orderChanged = true;
            beginMoveRows({}, existing, existing, {}, existing < row ? row + 1 : row);
            entries_.move(existing, row);
            rebuildPositions();
            endMoveRows();
        }
        const auto previous = entries_[row];
        if (previous == next)
            continue;
        QList<int> roles{Entry};
        for (const auto& field : {std::pair{"name", Name},
                                  {"usage", Usage},
                                  {"updateIntervalMinutes", Schedule},
                                  {"autoApply", Schedule},
                                  {"nextUpdateAt", Schedule},
                                  {"lastUpdateResult", Result},
                                  {"pendingApply", Result},
                                  {"secretRef", Source},
                                  {"history", History}})
            if (previous.value(QLatin1String(field.first)) !=
                    next.value(QLatin1String(field.first)) &&
                !roles.contains(field.second))
                roles.append(field.second);
        entries_[row] = next;
        emit dataChanged(index(row), index(row), roles);
        changedIds.append(id);
    }
    if (previousCount != count())
        emit countChanged();
    if (orderChanged || !changedIds.isEmpty())
        emit publicationApplied();
    for (const auto& id : changedIds)
        emit profileChanged(id);
    return true;
}
ProfileRowModel::ProfileRowModel(ProfileModel* source, QObject* parent)
    : QAbstractListModel(parent), source_(source) {
    connect(source_, &ProfileModel::publicationApplied, this, &ProfileRowModel::synchronize);
    synchronize();
}
int ProfileRowModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}
QVariant ProfileRowModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size() || role < First ||
        role > Third)
        return {};
    return rows_[index.row()][static_cast<size_t>(role - First)].toVariantMap();
}
QHash<int, QByteArray> ProfileRowModel::roleNames() const {
    return {{First, "first"}, {Second, "second"}, {Third, "third"}};
}
void ProfileRowModel::setColumns(int columns) {
    if (columns < 1 || columns > 3 || columns == columns_)
        return;
    columns_ = columns;
    synchronize();
    emit columnsChanged();
}
void ProfileRowModel::synchronize() {
    const int nextCount = (source_->count() + columns_ - 1) / columns_;
    if (nextCount < rows_.size()) {
        beginRemoveRows({}, nextCount, static_cast<int>(rows_.size()) - 1);
        rows_.resize(nextCount);
        endRemoveRows();
    } else if (nextCount > rows_.size()) {
        const int oldCount = static_cast<int>(rows_.size());
        beginInsertRows({}, oldCount, nextCount - 1);
        rows_.resize(nextCount);
        for (int row = oldCount; row < nextCount; ++row)
            for (int slot = 0; slot < columns_; ++slot)
                rows_[row][static_cast<size_t>(slot)] = source_->entryAt(row * columns_ + slot);
        endInsertRows();
    }
    for (int row = 0; row < nextCount; ++row) {
        QList<int> roles;
        for (int slot = 0; slot < 3; ++slot) {
            auto next = slot < columns_ ? source_->entryAt(row * columns_ + slot) : QJsonObject{};
            auto& previous = rows_[row][static_cast<size_t>(slot)];
            if (previous != next) {
                previous = std::move(next);
                roles.append(First + slot);
            }
        }
        if (!roles.isEmpty())
            emit dataChanged(index(row), index(row), roles);
    }
}
} // namespace quickclash::presentation
