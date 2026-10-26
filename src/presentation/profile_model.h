#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <array>

namespace quickclash::presentation {
class ProfileModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by UiState")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    enum Role {
        Identifier = Qt::UserRole + 1,
        Entry,
        Name,
        Usage,
        Schedule,
        Result,
        Source,
        History
    };
    explicit ProfileModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const {
        return static_cast<int>(entries_.size());
    }
    Q_INVOKABLE QVariantMap profile(const QString& id) const;
    [[nodiscard]] QJsonObject entryAt(int row) const;
    // Invalid or duplicate identities reject the entire publication without partial mutation.
    bool apply(const QJsonArray& profiles);
  signals:
    void countChanged();
    void profileChanged(const QString& id);
    void publicationApplied();

  private:
    void rebuildPositions();
    QList<QJsonObject> entries_;
    QHash<QString, int> positions_;
};

// Each virtual row contains at most three cards, never the entire subscription collection.
class ProfileRowModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by UiState")
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
  public:
    enum Role { First = Qt::UserRole + 1, Second, Third };
    explicit ProfileRowModel(ProfileModel* source, QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int columns() const {
        return columns_;
    }
    void setColumns(int columns);
  signals:
    void columnsChanged();

  private:
    void synchronize();
    ProfileModel* source_;
    int columns_ = 1;
    QList<std::array<QJsonObject, 3>> rows_;
};
} // namespace quickclash::presentation
