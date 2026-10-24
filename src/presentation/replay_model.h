#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace quickclash::presentation {
class ReplayModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by UiState")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    enum class Kind { Proxies, Connections };
    enum Role { Name = Qt::UserRole + 1, Detail, Status, Metric };
    explicit ReplayModel(Kind kind, QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const {
        return rowCount();
    }
    void setRows(int rows);
    void setFilter(const QString& filter);
    void tick();
  signals:
    void countChanged();

  private:
    void rebuild();
    [[nodiscard]] QString name(int row) const;
    Kind kind_;
    int rows_ = 0;
    int sequence_ = 0;
    QString filter_;
    QList<int> mapping_;
};
} // namespace quickclash::presentation
