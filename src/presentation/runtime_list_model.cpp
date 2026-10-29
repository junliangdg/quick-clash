#include "presentation/runtime_list_model.h"
#include <QJsonDocument>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <utility>

namespace quickclash::presentation {
namespace {
const QHash<int, QByteArray>& fields() {
    static const QHash<int, QByteArray> names{{RuntimeRows::Identifier, "id"},
                                              {RuntimeRows::Metadata, "metadata"},
                                              {RuntimeRows::Chains, "chains"},
                                              {RuntimeRows::Rule, "rule"},
                                              {RuntimeRows::RulePayload, "rulePayload"},
                                              {RuntimeRows::Upload, "upload"},
                                              {RuntimeRows::Download, "download"},
                                              {RuntimeRows::UploadRate, "uploadRate"},
                                              {RuntimeRows::DownloadRate, "downloadRate"},
                                              {RuntimeRows::Start, "start"},
                                              {RuntimeRows::Epoch, "epoch"},
                                              {RuntimeRows::Time, "time"},
                                              {RuntimeRows::Source, "source"},
                                              {RuntimeRows::Level, "level"},
                                              {RuntimeRows::Message, "message"},
                                              {RuntimeRows::Type, "type"},
                                              {RuntimeRows::Payload, "payload"},
                                              {RuntimeRows::Proxy, "proxy"},
                                              {RuntimeRows::Name, "name"},
                                              {RuntimeRows::KindRole, "kind"},
                                              {RuntimeRows::Count, "itemCount"},
                                              {RuntimeRows::UpdatedAt, "updatedAt"},
                                              {RuntimeRows::LocalSeq, "localSeq"}};
    return names;
}
QString fieldName(int role, const QByteArray& name) {
    return role == RuntimeRows::Count ? QStringLiteral("count") : QString::fromLatin1(name);
}
} // namespace
RuntimeRows::RuntimeRows(Kind kind, QObject* parent) : QAbstractListModel(parent), kind_(kind) {}
int RuntimeRows::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}
QHash<int, QByteArray> RuntimeRows::roleNames() const {
    auto names = fields();
    names.insert(Key, "stableKey");
    names.insert(Entry, "entry");
    names.insert(SearchText, "searchText");
    names.insert(SourceOrder, "sourceOrder");
    return names;
}
QVariant RuntimeRows::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 ||
        index.row() >= rows_.size())
        return {};
    const auto& row = rows_[index.row()];
    switch (role) {
    case Key:
        return row.key;
    case Entry:
        return row.value.toVariantMap();
    case SearchText:
        return row.search;
    case SourceOrder:
        return row.order;
    default:
        const auto it = fields().constFind(role);
        return it == fields().constEnd() ? QVariant{}
                                         : row.value.value(fieldName(role, *it)).toVariant();
    }
}
QString RuntimeRows::key(const QJsonObject& value) const {
    switch (kind_) {
    case Kind::Connections:
        return value.value(QStringLiteral("epoch")).toString() + QChar(0) +
               value.value(QStringLiteral("id")).toString();
    case Kind::Rules:
        // Mihomo has no rule ID. Keep semantic identity separate from effective order.
        return QString::fromUtf8(QJsonDocument(QJsonArray{value.value(QStringLiteral("type")),
                                                          value.value(QStringLiteral("payload")),
                                                          value.value(QStringLiteral("proxy"))})
                                     .toJson(QJsonDocument::Compact));
    case Kind::Providers:
        return value.value(QStringLiteral("kind")).toString() + QChar(0) +
               value.value(QStringLiteral("name")).toString();
    case Kind::Logs:
        return value.value(QStringLiteral("localSeq")).toString();
    }
    return {};
}
QString RuntimeRows::searchable(const QJsonObject& value) const {
    QStringList text;
    for (const auto* field : {"name", "kind", "type", "payload", "proxy", "rule", "rulePayload",
                              "start", "time", "source", "level", "message", "updatedAt",
                              "providerName", "mode", "behavior", "lastResult", "lastError"})
        text.append(value.value(QLatin1String(field)).toString());
    const auto metadata = value.value(QStringLiteral("metadata")).toObject();
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        if (it.value().isString())
            text.append(it.value().toString());
        else if (it.value().isDouble())
            text.append(QString::number(it.value().toDouble()));
    }
    for (const auto& chain : value.value(QStringLiteral("chains")).toArray())
        text.append(chain.toString());
    return text.join(QLatin1Char(' '));
}
void RuntimeRows::clear() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (rows_.isEmpty())
        return;
    beginRemoveRows({}, 0, rowCount() - 1);
    rows_ = {};
    endRemoveRows();
    emit batchFinished(true, true, false);
}
void RuntimeRows::apply(const QJsonArray& entries) {
    Q_ASSERT(QThread::currentThread() == thread());
    QHash<QString, Row> next;
    QHash<QString, int> occurrences;
    QStringList order;
    next.reserve(entries.size());
    order.reserve(entries.size());
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const auto value = entries[i].toObject();
        auto id = key(value);
        if (kind_ == Kind::Rules) {
            const auto occurrence = occurrences[id]++;
            id += QChar(0) + QString::number(occurrence);
        }
        if (id.isEmpty() || next.contains(id))
            continue;
        next.insert(id, {id, value, {}, static_cast<int>(i)});
        order.append(id);
    }
    bool structural = false;
    // Remove consecutive missing ranges backwards; no per-row hash-index repair or row moves.
    for (int end = rowCount() - 1; end >= 0;) {
        if (next.contains(rows_[end].key)) {
            --end;
            continue;
        }
        int first = end;
        while (first > 0 && !next.contains(rows_[first - 1].key))
            --first;
        beginRemoveRows({}, first, end);
        rows_.remove(first, end - first + 1);
        endRemoveRows();
        structural = true;
        end = first - 1;
    }
    QSet<QString> present;
    present.reserve(rows_.size());
    bool filtering = false, ordering = false, speed = false;
    int notifyFirst = -1, notifyLast = -1;
    QList<int> notifyRoles;
    const auto flush = [&] {
        if (notifyFirst >= 0)
            emit dataChanged(index(notifyFirst), index(notifyLast), notifyRoles);
        notifyFirst = -1;
    };
    for (int i = 0; i < rowCount(); ++i) {
        auto& row = rows_[i];
        present.insert(row.key);
        auto incoming = next.value(row.key);
        QList<int> roles;
        if (incoming.order != row.order) {
            row.order = incoming.order;
            roles.append(SourceOrder);
            ordering = true;
        }
        if (incoming.value != row.value) {
            roles.append(Entry);
            for (auto it = fields().constBegin(); it != fields().constEnd(); ++it)
                if (row.value.value(fieldName(it.key(), it.value())) !=
                    incoming.value.value(fieldName(it.key(), it.value())))
                    roles.append(it.key());
            // Stable textual search deliberately excludes changing byte/rate counters.
            const bool textChanged =
                roles.contains(Metadata) || roles.contains(Chains) || roles.contains(Rule) ||
                roles.contains(RulePayload) || roles.contains(Start) || roles.contains(Time) ||
                roles.contains(Source) || roles.contains(Level) || roles.contains(Message) ||
                roles.contains(Type) || roles.contains(Payload) || roles.contains(Proxy) ||
                roles.contains(Name) || roles.contains(KindRole) || roles.contains(UpdatedAt) ||
                kind_ == Kind::Rules || kind_ == Kind::Providers;
            incoming.search = textChanged ? searchable(incoming.value) : row.search;
            if (row.search != incoming.search) {
                row.search = incoming.search;
                roles.append(SearchText);
                filtering = true;
            }
            ordering |= roles.contains(Start);
            speed |= roles.contains(DownloadRate) || roles.contains(UploadRate);
            filtering |= roles.contains(Upload) || roles.contains(Download) ||
                         roles.contains(UploadRate) || roles.contains(DownloadRate) ||
                         roles.contains(Count) || roles.contains(Source) || roles.contains(Level);
            row.value = std::move(incoming.value);
        }
        if (roles.isEmpty()) {
            flush();
            continue;
        }
        std::sort(roles.begin(), roles.end());
        if (notifyFirst >= 0 && roles != notifyRoles)
            flush();
        if (notifyFirst < 0) {
            notifyFirst = i;
            notifyRoles = roles;
        }
        notifyLast = i;
    }
    flush();
    QList<Row> additions;
    for (const auto& id : order) {
        if (present.contains(id))
            continue;
        auto row = next.take(id);
        row.search = searchable(row.value);
        additions.append(std::move(row));
    }
    if (!additions.isEmpty()) {
        const int first = rowCount();
        beginInsertRows({}, first, first + static_cast<int>(additions.size()) - 1);
        rows_.append(additions);
        endInsertRows();
        structural = true;
    }
    emit batchFinished(structural || filtering, structural || ordering, speed);
}
RuntimeListModel::RuntimeListModel(RuntimeRows* rows, bool connections, QObject* parent)
    : QSortFilterProxyModel(parent), connections_(connections) {
    setDynamicSortFilter(false);
    setSourceModel(rows);
    speedSort_.setSingleShot(true);
    speedSort_.setInterval(1000);
    connect(&speedSort_, &QTimer::timeout, this, [this] { sort(0); });
    connect(rows, &RuntimeRows::batchFinished, this, &RuntimeListModel::finishBatch);
    connect(this, &QAbstractItemModel::rowsInserted, this, &RuntimeListModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &RuntimeListModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &RuntimeListModel::countChanged);
    sort(0);
}
void RuntimeListModel::setQuery(const QString& query) {
    const auto bounded = query.left(128);
    if (query_ == bounded)
        return;
    query_ = bounded;
    refilter();
    emit queryChanged();
}
void RuntimeListModel::release() {
    speedSort_.stop();
    static_cast<RuntimeRows*>(sourceModel())->clear();
    invalidate();
}
void RuntimeListModel::setSortMode(int mode) {
    if (mode < 0 || mode > 2 || sortMode_ == mode)
        return;
    sortMode_ = mode;
    speedSort_.stop();
    sort(0);
    emit sortModeChanged();
}
void RuntimeListModel::setSourceFilter(const QString& source) {
    if (sourceFilter_ == source)
        return;
    sourceFilter_ = source;
    refilter();
    emit filtersChanged();
}
void RuntimeListModel::setLevelFilter(const QString& level) {
    if (levelFilter_ == level)
        return;
    levelFilter_ = level;
    refilter();
    emit filtersChanged();
}
bool RuntimeListModel::filterAcceptsRow(int row, const QModelIndex& parent) const {
    const auto item = sourceModel()->index(row, 0, parent);
    if ((!sourceFilter_.isEmpty() && item.data(RuntimeRows::Source).toString() != sourceFilter_) ||
        (!levelFilter_.isEmpty() && item.data(RuntimeRows::Level).toString() != levelFilter_))
        return false;
    if (query_.isEmpty() ||
        item.data(RuntimeRows::SearchText).toString().contains(query_, Qt::CaseInsensitive))
        return true;
    // Preserve numeric searches without rebuilding static metadata text on every rate update.
    for (const auto role : {RuntimeRows::Upload, RuntimeRows::Download, RuntimeRows::UploadRate,
                            RuntimeRows::DownloadRate, RuntimeRows::Count}) {
        const auto value = item.data(role);
        if (value.isValid() &&
            (value.toString().contains(query_, Qt::CaseInsensitive) ||
             QString::number(value.toDouble(), 'f', 0).contains(query_, Qt::CaseInsensitive)))
            return true;
    }
    return false;
}
bool RuntimeListModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (connections_) {
        if (sortMode_ != 0) {
            const auto role = sortMode_ == 1 ? RuntimeRows::DownloadRate : RuntimeRows::UploadRate;
            const auto a = left.data(role), b = right.data(role);
            if (a.isValid() != b.isValid())
                return a.isValid();
            if (a.toDouble() != b.toDouble())
                return a.toDouble() > b.toDouble();
        } else {
            const auto a = left.data(RuntimeRows::Start).toString();
            const auto b = right.data(RuntimeRows::Start).toString();
            if (a != b)
                return a > b;
        }
        return left.data(RuntimeRows::Key).toString() < right.data(RuntimeRows::Key).toString();
    }
    return left.data(RuntimeRows::SourceOrder).toInt() <
           right.data(RuntimeRows::SourceOrder).toInt();
}
void RuntimeListModel::finishBatch(bool filtering, bool ordering, bool speed) {
    emit entriesChanged();
    if (filtering && (!query_.isEmpty() || !sourceFilter_.isEmpty() || !levelFilter_.isEmpty()))
        refilter();
    if (ordering)
        sort(0);
    else if (connections_ && sortMode_ != 0 && speed && !speedSort_.isActive())
        speedSort_.start();
}
QStringList RuntimeListModel::captureIds(int limit) const {
    if (!connections_ || limit < 1 || limit > 500 || rowCount() > limit)
        return {};
    QStringList ids;
    ids.reserve(rowCount());
    for (int i = 0; i < rowCount(); ++i) {
        ids.append(index(i, 0).data(RuntimeRows::Identifier).toString());
    }
    return ids;
}
QVariantMap RuntimeListModel::connection(const QString& epoch, const QString& id) const {
    if (!connections_ || epoch.isEmpty() || id.isEmpty())
        return {};
    for (int i = 0; i < sourceModel()->rowCount(); ++i) {
        const auto item = sourceModel()->index(i, 0);
        if (item.data(RuntimeRows::Epoch).toString() == epoch &&
            item.data(RuntimeRows::Identifier).toString() == id)
            return item.data(RuntimeRows::Entry).toMap();
    }
    return {};
}
} // namespace quickclash::presentation
