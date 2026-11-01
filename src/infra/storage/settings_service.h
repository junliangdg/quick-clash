#pragma once
#include "domain/operation.h"
#include "infra/storage/latest_mailbox.h"
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
    void setDemands(const application::RuntimeDemands& demands);
    void command(const QString& name, const QStringList& arguments = {});
  signals:
    void subscriptionUrlReady(const QString& id, const QString& token, const QString& url);
    void loaded(const QString& theme, bool unload, bool acknowledged, const QString& language,
                const QJsonObject& dashboard);
    void saved(const QString& theme, bool unload, bool acknowledged, const QString& language,
               const QJsonObject& dashboard);
    void failed(const QString& code);
    void runtimeChanged(const QJsonObject& state);
    void dataChanged(const quickclash::application::RuntimeUpdate& update);
    void operationCompleted(const QUuid& id, domain::OperationState state,
                            const QString& errorCode);

  private:
    struct State;
    QThread thread_;
    QObject* worker_ = nullptr;
    std::shared_ptr<State> state_;
    LatestMailbox mailbox_;
};
} // namespace quickclash::infra
