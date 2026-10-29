#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace quickclash::presentation {
// One GUI-owned row store per channel. JSON is expanded only for requested delegate roles.
class RuntimeRows final : public QAbstractListModel {
    Q_OBJECT
  public:
    enum class Kind { Connections, Rules, Providers, Logs };
    enum Role {
        Key = Qt::UserRole + 1,
        Entry,
        SearchText,
        SourceOrder,
        Identifier,
        Metadata,
        Chains,
        Rule,
        RulePayload,
        Upload,
        Download,
        UploadRate,
        DownloadRate,
        Start,
        Epoch,
        Time,
        Source,
        Level,
        Message,
        Type,
        Payload,
        Proxy,
        Name,
        KindRole,
        Count,
        UpdatedAt,
        LocalSeq
    };
    explicit RuntimeRows(Kind kind, QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    void apply(const QJsonArray& entries);
    void clear();
  signals:
    void batchFinished(bool filterChanged, bool orderChanged, bool speedChanged);

  private:
    struct Row {
        QString key;
        QJsonObject value;
        QString search;
        int order = 0;
    };
    [[nodiscard]] QString key(const QJsonObject& value) const;
    [[nodiscard]] QString searchable(const QJsonObject& value) const;
    Kind kind_;
    QList<Row> rows_;
};

class RuntimeListModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by UiState")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString levelFilter READ levelFilter WRITE setLevelFilter NOTIFY filtersChanged)
  public:
    explicit RuntimeListModel(RuntimeRows* rows, bool connections, QObject* parent = nullptr);
    [[nodiscard]] int count() const {
        return rowCount();
    }
    [[nodiscard]] QString query() const {
        return query_;
    }
    void setQuery(const QString& query);
    [[nodiscard]] int sortMode() const {
        return sortMode_;
    }
    void setSortMode(int mode);
    [[nodiscard]] QString sourceFilter() const {
        return sourceFilter_;
    }
    void setSourceFilter(const QString& source);
    [[nodiscard]] QString levelFilter() const {
        return levelFilter_;
    }
    void setLevelFilter(const QString& level);
    // Release cached rows and deferred sorting, retaining only bounded user view preferences.
    void release();
    // Capture only the bounded destructive-command target set, not a JS copy of all rows.
    Q_INVOKABLE QStringList captureIds(int limit = 500) const;
    // Resolve a selected connection independently of current filtering and row order.
    Q_INVOKABLE QVariantMap connection(const QString& epoch, const QString& id) const;
  signals:
    void countChanged();
    void queryChanged();
    void sortModeChanged();
    void filtersChanged();
    void entriesChanged();

  protected:
    [[nodiscard]] bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

  private:
    void finishBatch(bool filterChanged, bool orderChanged, bool speedChanged);
    // Qt 6.10 deprecates invalidateFilter() in favor of begin/endFilterChange().
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    void refilter() {
        beginFilterChange();
        endFilterChange();
    }
#else
    void refilter() {
        invalidateFilter();
    }
#endif
    QString query_;
    QString sourceFilter_;
    QString levelFilter_;
    int sortMode_ = 0;
    bool connections_ = false;
    QTimer speedSort_;
};
} // namespace quickclash::presentation
