#pragma once
#include "application/runtime_update.h"
#include "infra/config/config_preflight.h"
#include "infra/core/engine_session.h"
#include "infra/storage/config_repository.h"
#include "infra/storage/provider_stage.h"
#include "infra/storage/settings_repository.h"
#include "platform/windows/credential_store.h"
#include "platform/windows/system_proxy.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <memory>

namespace quickclash::application {
// One instance on the storage thread; survives all QML engine rebuilds.
class DesktopRuntime final : public QObject, public IProxyApplication {
    Q_OBJECT
  public:
    DesktopRuntime(infra::JsonStore& store, QObject* parent = nullptr,
                   platform::ProxySettings* proxySettings = nullptr,
                   std::function<QDateTime()> clock = {});
    ~DesktopRuntime() override;
    void initialize();
    // Enabled by the desktop host after storage and UI settings have loaded.
    void enableBackgroundManagement();
    void importFile(const QString& url, const QString& name);
    void addSubscription(const QString& url, const QString& name);
    void updateProfile(const QString& id);
    void configureSubscription(const QString& id, int intervalMinutes, bool autoApply);
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
    void setTun(bool desired);
    void testWebsite(const QString& target, const QString& path = QStringLiteral("CurrentProxy"));
    void refresh();
    void previewConfiguration();
    void reapplySystemProxy();
    void setDemands(const QStringList& streams);
    void applyDemands(const RuntimeDemands& demands);
    void featureCommand(const QString& command, const QStringList& arguments);
    [[nodiscard]] domain::Generation selectionGeneration() const;
    // Explicit diagnostic API. Production UI publications never call this method.
    [[nodiscard]] QJsonObject snapshot() const;
  signals:
    // Reliable compact control state, not a complete diagnostic snapshot.
    void changed(const QJsonObject& state);
    void dataPublished(const quickclash::application::RuntimeUpdate& update);
    void operationCompleted(const QUuid& id, domain::OperationState state,
                            const QString& errorCode);

  private:
    void publish();
    void flushData();
    [[nodiscard]] QJsonObject controlState() const;
    void error(const QString& code);
    void configError(const domain::Error& failure);
    void download(const QString& url, const QString& name, const QString& id = {},
                  const QJsonObject& headers = {});
    struct ConfigCandidates {
        explicit ConfigCandidates(infra::JsonStore& owner) : store(owner) {}
        ~ConfigCandidates();
        infra::JsonStore& store;
        QStringList references;
    };
    void importBytes(QByteArray bytes, const QString& name, const QString& secretRef,
                     const QString& id = {}, const QJsonObject& storedSource = {},
                     const QJsonObject& metadata = {},
                     std::shared_ptr<ConfigCandidates> candidates = {});
    void resolveImport(QByteArray bytes, const QString& name, const QString& secretRef,
                       const QString& id = {}, const QJsonObject& metadata = {});
    struct ProviderImport {
        explicit ProviderImport(infra::JsonStore& store) : stage(store) {}
        infra::ProviderStage stage;
        QString sourceRef;
        QString sourceHash;
        std::shared_ptr<ConfigCandidates> candidates;
        QJsonArray requests;
        qsizetype index = 0;
        QString name;
        QString secretRef;
        QString id;
        QJsonObject metadata;
        quint64 generation = 0;
    };
    void fetchProvider();
    void validate(const QByteArray& semantic, std::function<void(bool)> completion);
    // Trust is worker-private, tied to one operation, exact persisted candidate and preferences.
    struct ConfigTransaction {
        quint64 generation = 0;
        QString profileId;
        QByteArray semantic;
        QJsonObject preferences;
        QJsonObject imported;
        QString artifactRef;
        std::shared_ptr<ConfigCandidates> candidates;
        bool validated = false;
    };
    void finishImport(const std::shared_ptr<ConfigTransaction>& context);
    void discardConfigTransaction();
    void applyProfile(const QString& id, bool startAfter);
    void launch();
    void confirmed();
    void failedApply(const QString& code);
    void cleanupSession();
    void collectGarbage();
    void restorePreferences();
    void loadOfflineGroups();
    void pollFeatures();
    void releaseFeature(const QString& stream);
    void requestPageData(const QString& stream);
    [[nodiscard]] RuntimeRequest beginFeatureRequest(const QString& stream);
    [[nodiscard]] bool acceptFeatureRequest(const RuntimeRequest& request);
    void appendLog(const QString& source, const QString& level, const QString& message);
    void backupProfiles();
    void closeNextConnection(const QJsonArray& targets, qsizetype index, const QString& epoch);
    void startDelayTests(const QStringList& names, const QString& target);
    void pumpDelayTests();
    void cancelDelayTests();
    void restoreBackup(const QString& url);
    void restoreNext(const QJsonArray& entries, qsizetype index, QJsonArray staged,
                     quint64 generation);
    void updateGeo();
    void fetchGeo(int index, QJsonObject snapshot, quint64 generation);
    void scheduleSubscriptions();
    void completeSubscriptionUpdate(const QString& result);
    void scheduleBackgroundStart(bool afterFailure = false);
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
    QTimer subscriptionTimer_;
    QTimer featureTimer_;
    QTimer publicationTimer_;
    quint64 publicationGeneration_ = 0;
    QString publicationEpoch_;
    QJsonObject publishedControl_;
    QJsonObject publishedData_;
    QJsonArray summaryGroups_;
    QString summaryMode_;
    QString summaryPolicy_;
    bool summaryReady_ = false;
    QJsonObject outbound_;
    QJsonArray summaryProfiles_;
    QString summarySelectedId_;
    QJsonValue selectedName_;
    QSet<QString> featureDemands_;
    RuntimeDemands demandState_;
    QHash<QString, quint64> featureRequests_;
    QSet<QString> pendingFeatureRequests_;
    QSet<QString> loadedFeatures_;
    QSet<QString> invalidatedChannels_;
    QJsonObject featureState_;
    QJsonArray connections_;
    QJsonArray rules_;
    QJsonArray providers_;
    QJsonArray providerDraft_;
    QJsonArray logs_;
    QJsonArray appLogs_;
    QJsonObject delays_;
    bool delayRunning_ = false;
    QStringList delayQueue_;
    QSet<QString> activeDelays_;
    QString delayTarget_;
    quint64 delayGeneration_ = 0;
    bool closingConnections_ = false;
    int droppedLogs_ = 0;
    quint64 logSequence_ = 0;
    int resourceTick_ = 0;
    int proxyTick_ = 0;
    qint64 connectionSampleMs_ = 0;
    QElapsedTimer coreUptime_;
    std::function<QDateTime()> clock_;
    QString updatingSubscription_;
    bool shuttingDown_ = false;
    bool backgroundManaged_ = false;
    bool backgroundSuppressed_ = false;
    int recoveryFailures_ = 0;
    QList<qint64> crashTimes_;
    QPointer<QNetworkReply> downloadReply_;
    std::shared_ptr<ProviderImport> providerImport_;
    std::shared_ptr<ConfigTransaction> configTransaction_;
    QPointer<QNetworkReply> testReply_;
    std::unique_ptr<QTemporaryDir> staging_;
    std::function<void(bool)> validationCompletion_;
    QJsonArray profiles_;
    QJsonArray groups_;
    QJsonArray configuredGroups_;
    QString defaultPolicy_;
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
    QStringList testQueue_;
    QString testPath_;
    quint64 testGeneration_ = 0;
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
    quint64 reconcileRequests_ = 0;
    quint64 reconcileCount_ = 0;
};
} // namespace quickclash::application
