#pragma once
#include "infra/storage/settings_repository.h"
#include <QObject>
#include <QThread>
#include <memory>

namespace quickclash::infra {
// The worker owns the store; all file/DPAPI work stays off the UI event loop.
class SettingsService final : public QObject {
    Q_OBJECT
  public:
    explicit SettingsService(QString root, QObject* parent = nullptr);
    ~SettingsService() override;
    void save(UiPreferences preferences);
    void command(const QString& name, const QStringList& arguments = {});
  signals:
    void loaded(const QString& theme, bool unload, bool acknowledged);
    void saved(const QString& theme, bool unload, bool acknowledged);
    void failed(const QString& code);
    void runtimeChanged(const QJsonObject& state);

  private:
    struct State;
    QThread thread_;
    QObject* worker_ = nullptr;
    std::shared_ptr<State> state_;
};
} // namespace quickclash::infra
