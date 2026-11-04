#pragma once
#include <QStringList>
#include <QVariantMap>
#include <array>

namespace quickclash::presentation {
// Small, process-local editing state. Never owns a QML object or a full profile.
class SettingsSession {
  public:
    [[nodiscard]] QVariantMap snapshot() const;
    bool observe(const QVariantMap& runtime);
    void edit(const QString& field, const QString& value);
    void reset(const QString& group, bool keepInput = false);
    [[nodiscard]] QStringList prepareNetwork() const;
    [[nodiscard]] QStringList submitNetwork(const QStringList& confirmation);
    [[nodiscard]] QStringList submitTarget();
    [[nodiscard]] bool hasDraft() const;
    void setCategory(int category);
    void remember(int category, double position, const QString& focus);
    [[nodiscard]] double position(int category) const;
    [[nodiscard]] QString focus(int category) const;

  private:
    [[nodiscard]] bool networkDirty() const;
    [[nodiscard]] bool targetDirty() const;
    [[nodiscard]] bool networkValid() const;
    [[nodiscard]] bool targetValid() const;
    [[nodiscard]] QString identity() const;
    QVariantMap runtime_;
    QVariantMap network_;
    QVariantMap baseline_;
    QVariantMap saved_;
    QVariantMap submitted_;
    QString baseIdentity_;
    QString networkRequest_;
    QString networkResult_;
    QString target_;
    QString savedTarget_;
    QString baseTarget_;
    QString submittedTarget_;
    QString targetRequest_;
    QString targetResult_;
    bool initialized_ = false;
    int category_ = 0;
    std::array<double, 5> positions_{};
    std::array<QString, 5> focuses_{};
};
} // namespace quickclash::presentation
