#pragma once
#include "infra/config/config_preflight.h"
#include "infra/core/engine_session.h"
#include "infra/storage/config_repository.h"
#include "infra/storage/settings_repository.h"
#include "platform/windows/credential_store.h"
#include "platform/windows/system_proxy.h"
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>

namespace quickclash::application {
// One instance on the storage thread; survives all QML engine rebuilds.
class DesktopRuntime final : public QObject, public IProxyApplication {
    Q_OBJECT
  public:
    DesktopRuntime(infra::JsonStore& store, QObject* parent = nullptr,
                   platform::ProxySettings* proxySettings = nullptr);
    ~DesktopRuntime() override;
    void initialize();
    void importFile(const QString& url, const QString& name);
    void addSubscription(const QString& url, const QString& name);
    void updateProfile(const QString& id);
    void changeProfileSource(const QString& id, const QString& url, const QString& headers);
    void selectProfile(const QString& id);
    void renameProfile(const QString& id, const QString& name);
    void deleteProfile(const QString& id);
    void start(bool systemProxy);
    [[nodiscard]] QUuid startCore(domain::Generation selectionGeneration) override;
    [[nodiscard]] QUuid startProxy(domain::Generation selectionGeneration) override;
    [[nodiscard]] QUuid stopProxy(domain::StopReason reason) override;
    [[nodiscard]] QUuid setSystemProxy(bool desired) override;
    void reconcile();
    void stop(domain::StopReason reason = domain::StopReason::UserStop);
    void setMode(const QString& mode);
    void selectProxy(const QString& group, const QString& member);
    void setPorts(int mixed, int controller);
    void testWebsite(const QString& target);
    void refresh();
    void previewConfiguration();
    void reapplySystemProxy();
    [[nodiscard]] QJsonObject snapshot() const;
  signals:
    void changed(const QJsonObject& state);
    void operationCompleted(const QUuid& id, domain::OperationState state,
                            const QString& errorCode);

  private:
    void publish();
    void error(const QString& code);
    void configError(const domain::Error& failure);
    void download(const QString& url, const QString& name, const QString& id = {},
                  const QJsonObject& headers = {});
    void importBytes(const QByteArray& bytes, const QString& name, const QString& secretRef,
                     const QString& id = {});
    void validate(const QByteArray& semantic, std::function<void(bool)> completion);
    void applyProfile(const QString& id, bool startAfter);
    void launch();
    void confirmed();
    void failedApply(const QString& code);
    void cleanupSession();
    void collectGarbage();
    void restorePreferences();
    [[nodiscard]] domain::CommandResult saveProxyIntent(bool desired);
    [[nodiscard]] QUuid beginOperation(const QString& kind);
    void finishOperation(domain::OperationState state, const QString& error = {});
    [[nodiscard]] domain::RuntimePreferences preferences() const;
    [[nodiscard]] QString coreExecutable() const;
    [[nodiscard]] QJsonObject profile(const QString& id) const;
    [[nodiscard]] bool saveCatalog(const QJsonArray& profiles);
    infra::JsonStore& store_;
    infra::ConfigRepository repository_;
    platform::CredentialStore credentials_;
    platform::WindowsProxySettings defaultProxySettings_;
    platform::ProxyLease proxyLease_;
    infra::EngineSession engine_;
    platform::OwnedProcess validator_;
    QNetworkAccessManager directNetwork_;
    QNetworkAccessManager testNetwork_;
    QTimer validationDeadline_;
    QTimer recoveryTimer_;
    QTimer reconcileTimer_;
    QList<qint64> crashTimes_;
    QPointer<QNetworkReply> downloadReply_;
    QPointer<QNetworkReply> testReply_;
    std::unique_ptr<QTemporaryDir> staging_;
    std::function<void(bool)> validationCompletion_;
    QJsonArray profiles_;
    QJsonArray groups_;
    QJsonArray errors_;
    QJsonArray configIssues_;
    QJsonObject runtimePreferences_;
    QJsonObject candidate_;
    QJsonObject previous_;
    QJsonObject previousPreferences_;
    QJsonObject identities_;
    QJsonObject selections_;
    QJsonObject operationSnapshot_;
    std::unique_ptr<domain::Operation> operation_;
    QString pendingGroup_;
    QString pendingMember_;
    bool restoringNodes_ = false;
    QSet<QString> restoredGroups_;
    QString selectedId_;
    QString applyingId_;
    QString status_ = QStringLiteral("Stopped");
    QString lastError_;
    QString testResult_;
    QString testTarget_;
    QString effectivePreview_;
    QString stopFailure_;
    QString epoch_;
    QByteArray secret_;
    bool busy_ = false;
    bool applying_ = false;
    bool recovering_ = false;
    bool startAfter_ = false;
    bool proxyDesired_ = false;
    bool previouslyProxied_ = false;
    bool restartAttempt_ = false;
    bool persistedProxyIntent_ = false;
    std::optional<bool> previousProxyIntent_;
    quint64 operationGeneration_ = 0;
};
} // namespace quickclash::application
