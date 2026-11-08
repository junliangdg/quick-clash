#pragma once
#include "application/desktop_services.h"
#include "application/runtime_update.h"
#include "domain/log_buffer.h"
#include "domain/proxy_latency.h"
#include "infra/config/config_preflight.h"
#include "infra/core/engine_session.h"
#include "infra/network/download_network.h"
#include "infra/network/network_relay.h"
#include "infra/storage/config_repository.h"
#include "infra/storage/provider_stage.h"
#include "infra/storage/settings_repository.h"
#include "platform/core_archive.h"
#include "platform/proxy_settings.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMap>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>

namespace quickclash::application {
// One instance on the storage thread; survives all QML engine rebuilds.
class DesktopRuntime final : public QObject, public IProxyApplication {
    Q_OBJECT
  public:
    DesktopRuntime(infra::JsonStore& store, QObject* parent = nullptr,
                   platform::ProxySettings* proxySettings = nullptr,
                   std::function<QDateTime()> clock = {}, const QUrl& geoSourceBase = {});
    DesktopRuntime(infra::JsonStore& store, DesktopServices services, QObject* parent = nullptr,
                   std::function<QDateTime()> clock = {}, const QUrl& geoSourceBase = {});
    ~DesktopRuntime() override;
    void initialize();
    // Enabled by the desktop host after storage and UI settings have loaded.
    void enableBackgroundManagement();
    void importFile(const QString& url, const QString& name);
    void addSubscription(const QString& url, const QString& name,
                         const QString& route = QStringLiteral("Direct"));
    void setSubscriptionRoute(const QString& id, const QString& route);
    void cancelGeo();
    void updateProfile(const QString& id, bool applyNow = false);
    void configureSubscription(const QString& id, int intervalMinutes, bool autoApply);
    void changeProfileSource(const QString& id, const QString& url, const QString& headers,
                             bool applyNow = false, const QString& token = {});
    void requestSubscriptionUrl(const QString& id, const QString& token);
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
    // Composition-only override for isolated integration hosts; production keeps the defaults.
    void setDeferredApplyTiming(int pollMs, qint64 limitMs);
    void setTun(bool desired);
    void testWebsite(const QString& target, const QString& path = QStringLiteral("CurrentProxy"));
    void queryIp(const QString& family, const QUrl& source = {});
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
    // One-shot reply to an explicit management action; never part of published state.
    void subscriptionUrlReady(const QString& id, const QString& token, const QString& url);
    void subscriptionSourceFinished(const QString& id, const QString& token, bool saved,
                                    const QString& errorCode);
    // Reliable compact control state, not a complete diagnostic snapshot.
    void changed(const QJsonObject& state);
    void dataPublished(const quickclash::application::RuntimeUpdate& update);
    void operationCompleted(const QUuid& id, domain::OperationState state,
                            const QString& errorCode);

  private:
    void refreshStartup();
    void configureStartup(const QString& command, const QStringList& args);
    QJsonObject startupState_;
    void coreUpdateCommand(const QString& command, const QStringList& args);
    void downloadCore();
    void applyCore(const QString& id);
    void cancelCoreUpdate();
    platform::CoreArchive coreArchive_;
    std::unique_ptr<QTemporaryDir> coreStage_;
    QJsonObject coreUpdateState_;
    bool coreOperationActive_ = false;
    QString runningCoreVersion_;
    QString bundledCoreExecutable_;
    QUrl coreUpdateSource_;
    [[nodiscard]] QString bundledCoreExecutable() const;
    void configureGeo(const QString& command, const QStringList& args);
    void resolveGeoSnapshot(quint64 generation);
    void scheduleGeo();
    QTimer geoTimer_;
    infra::NetworkRelay networkRelay_;
    infra::DownloadNetwork resourceNetwork_{networkRelay_};
    QUrl geoResolvedBase_;
    QString geoRevision_;
    bool geoScheduled_ = false;
    QJsonObject geoPending_;
    QJsonObject geoSettings_;
    void publish();
    void flushData();
    [[nodiscard]] QJsonObject controlState() const;
    void error(const QString& code);
    // Runs the TUN preflight and, when the service is missing, stopped or not yet authorized
    // for this folder, requests one administrator confirmation before retrying.
    [[nodiscard]] bool startTunSetup(const QString& failure);
    void finishTunSetup(int exitCode);
    void retryTunAfterSetup();
    void configError(const domain::Error& failure);
    void download(const QString& url, const QString& name, const QString& id = {},
                  const QJsonObject& headers = {}, const QString& route = {},
                  const QJsonObject& cache = {});
    struct SubscriptionDownload {
        QElapsedTimer timer;
        quint64 generation = 0;
        QString url, name, id, route, proxyEpoch;
        QJsonObject headers, cache;
        QSet<QUrl> visited;
        QByteArray bytes;
        qint64 received = 0;
        int redirects = 0;
        bool proxy = false;
        bool fallback = false;
        bool cacheRetry = false;
        bool authorization = true;
        bool cachedSourceVerified = false;
    };
    void requestSubscription(const std::shared_ptr<SubscriptionDownload>& context, const QUrl& url);
    void failSubscription(const QString& code);
    void finishSubscription(const std::shared_ptr<SubscriptionDownload>& context,
                            QNetworkReply* reply, int status);
    void applyGeoSnapshot(const QJsonObject& snapshot);
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
        QJsonArray resources;
        QJsonArray cachedResources;
        QString refreshKey;
        qsizetype index = 0;
        QString name;
        QString secretRef;
        QString id;
        QJsonObject metadata;
        quint64 generation = 0;
    };
    void fetchProvider();
    void refreshSnapshotProvider(const QString& kind, const QString& name);
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
        bool previewOnly = false;
        bool forceApply = false;
        bool rulesComposed = false;
        QJsonObject userRules;
        QString editorToken;
        QString customGroupToken;
    };
    void finishImport(const std::shared_ptr<ConfigTransaction>& context);
    void applyUpdatedProfile(const QString& id, const std::shared_ptr<ConfigTransaction>& context);
    void deferUpdatedProfile(const QString& id);
    void checkDeferredApply();
    void cancelDeferredApply(bool notify = true);
    void recordUpdateOutcome(const QString& outcome, const QString& detail);
    [[nodiscard]] bool matchesRunningConfig(const QString& id, const QByteArray& semantic) const;
    [[nodiscard]] bool coreArtifactVerified(const QString& path, const QString& id) const;
    void discardConfigTransaction();
    void applyProfile(const QString& id, bool startAfter);
    void launch();
    void confirmed();
    void failedApply(const QString& code);
    void failModeChange(const QString& code);
    void recordCoreExit();
    void cleanupSession();
    void collectGarbage();
    void maintenanceCommand(const QString& command, const QStringList& arguments);
    void clearMaintenancePreview();
    void restorePreferences();
    domain::CommandResult stopValidation();
    void loadOfflineGroups();
    void pollFeatures();
    void releaseFeature(const QString& stream);
    void requestPageData(const QString& stream);
    void updateProvider(const QString& kind, const QString& name);
    void finishSnapshotProviderRefresh();
    void decorateProviderResults();
    [[nodiscard]] RuntimeRequest beginFeatureRequest(const QString& stream);
    [[nodiscard]] bool acceptFeatureRequest(const RuntimeRequest& request);
    void appendLog(const QString& source, const QString& level, const QString& message,
                   bool truncated = false);
    void prepareLogExport(bool hideMessages, const QString& token = {});
    void exportLogs(const QString& token);
    void clearLogExport();
    void sampleResources();
    void customGroupCommand(const QString& command, const QStringList& arguments);
    void refreshCustomGroupContext();
    void finishCustomGroupPreview(const std::shared_ptr<ConfigTransaction>& context);
    void finishCustomGroupOperation();
    void userRuleCommand(const QString& command, const QStringList& arguments);
    [[nodiscard]] QJsonObject userRuleBook() const;
    void refreshUserRuleContext();
    void finishUserRuleOperation();
    void editorCommand(const QString& command, const QStringList& arguments);
    void openEditor(const QString& id, const QString& kind, bool discard = false);
    bool saveEditorDraft(const QString& token, const QString& text);
    void prepareEditor(bool copy, const QString& name = {});
    void finishEditorPreview(const std::shared_ptr<ConfigTransaction>& context);
    void restoreProfileVersion(const QString& id, int index, const QString& base = {});
    [[nodiscard]] QString profileRevision(const QJsonObject& item) const;
    void backupProfiles(bool originals);
    struct BackupRestore {
        QJsonArray entries;
        QJsonObject preferences;
        std::shared_ptr<ConfigCandidates> candidates;
        QString token;
        QString catalogHash;
        QJsonArray globalRules;
        bool hasGlobalRules = false;
        bool restoreGlobalRules = false;
        quint64 generation = 0;
        bool active = false;
    };
    void previewBackup(const QString& url);
    void cancelBackupRestore();
    void closeNextConnection(const QJsonArray& targets, qsizetype index, const QString& epoch);
    void resetConnectionClose();
    void closeConnectionTarget(const QString& id, const QString& epoch,
                               std::function<void(const QString&)> completion);
    struct DelayProbe {
        QString name;
        QString target;
        [[nodiscard]] QString key() const {
            return name + QChar(0) + domain::ProxyLatency::targetHash(target);
        }
    };
    [[nodiscard]] std::optional<DelayProbe> delayProbe(const QString& name,
                                                       const QString& context) const;
    void startDelayTests(const QList<DelayProbe>& probes, const QString& group = {},
                         const QString& scope = {});
    void pumpDelayTests();
    void cancelDelayTests();
    void restartDelayTestsAfterReconcile();
    void resumeDelayTestsAfterReconcile();
    struct WebsiteProbe {
        QElapsedTimer timer;
        QJsonObject context;
        QSet<QUrl> visited;
        qint64 received = 0;
        quint64 generation = 0;
        quint64 operationGeneration = 0;
        int redirects = 0;
    };
    void requestWebsite(const std::shared_ptr<WebsiteProbe>& probe, const QUrl& url);
    void finishWebsite(const std::shared_ptr<WebsiteProbe>& probe, const QString& outcome,
                       int status);
    void invalidateWebsiteTest();
    void cancelIpQueries(bool stale);
    void restoreBackup(const QString& token, bool restoreGlobalRules = false);
    void restoreNext(const std::shared_ptr<BackupRestore>& context, qsizetype index);
    void updateGeo();
    void fetchGeo(int index, QJsonObject snapshot, quint64 generation);
    void scheduleSubscriptions();
    void completeSubscriptionUpdate(const QString& result);
    void completeSubscriptionApply();
    void scheduleBackgroundStart(bool afterFailure = false, bool shortFailure = false);
    [[nodiscard]] domain::CommandResult saveProxyIntent(bool desired);
    [[nodiscard]] QUuid beginOperation(const QString& kind);
    void finishOperation(domain::OperationState state, const QString& error = {});
    [[nodiscard]] domain::RuntimePreferences preferences() const;
    [[nodiscard]] QString coreExecutable() const;
    [[nodiscard]] QJsonObject profile(const QString& id) const;
    [[nodiscard]] QJsonArray profileViews() const;
    [[nodiscard]] bool saveCatalog(const QJsonArray& profiles);
    infra::JsonStore& store_;
    infra::ConfigRepository repository_;
    std::unique_ptr<ICredentialStore> credentials_;
    const DesktopPlatformInfo platformInfo_;
    std::unique_ptr<IDesktopResources> resources_;
    std::unique_ptr<ISystemProxyLease> proxyLease_;
    std::unique_ptr<IServiceSetup> serviceSetup_;
    infra::EngineSession engine_;
    std::unique_ptr<IDesktopProcess> validator_;
    infra::DownloadNetwork directNetwork_{networkRelay_};
    infra::DownloadNetwork testNetwork_{networkRelay_};
    infra::DownloadNetwork ipDirectNetwork_{networkRelay_};
    infra::DownloadNetwork ipProxyNetwork_{networkRelay_};
    QList<QPointer<QNetworkReply>> ipReplies_;
    quint64 ipGeneration_ = 0;
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
    QJsonArray profileViews_;
    qint64 usageMinute_ = -1;
    QString summarySelectedId_;
    QJsonValue selectedName_;
    QSet<QString> featureDemands_;
    RuntimeDemands demandState_;
    QHash<QString, quint64> featureRequests_;
    QSet<QString> pendingFeatureRequests_;
    QSet<QString> loadedFeatures_;
    QSet<QString> invalidatedChannels_;
    QJsonObject featureState_;
    QJsonObject maintenancePreview_;
    QString maintenanceToken_;
    QString maintenanceKind_;
    QString maintenanceFingerprint_;
    bool recoveryArchived_ = false;
    QJsonObject editor_;
    QJsonArray connections_;
    QJsonArray rules_;
    QJsonArray providers_;
    QJsonArray providerDraft_;
    QJsonObject rulePageState_;
    QJsonObject providerPageState_;
    QJsonObject providerResults_;
    // Only a small operation identity survives page release; results remain demand-scoped.
    QJsonObject snapshotProviderRefresh_;
    domain::LogBuffer logs_;
    domain::LogBuffer appLogs_{100, 512 * 1024};
    QJsonObject logExport_;
    QJsonObject logExportFrozen_;
    QString logExportToken_;
    QTimer logPublicationTimer_;
    void syncDelayViews();
    domain::ProxyLatency proxyLatency_;
    QJsonObject delayDetails_;
    QDateTime delayExpiry_;
    QDateTime delayViewsAt_;
    QJsonObject delays_;
    // The contract projection is shared by control snapshots until latency changes.
    QJsonObject delayTimes_;
    bool delayRunning_ = false;
    QList<DelayProbe> delayQueue_;
    QList<DelayProbe> delayProbes_;
    QHash<QString, DelayProbe> activeDelays_;
    QHash<QString, QString> groupDelayTargets_;
    QString delayTarget_ = QStringLiteral("https://www.gstatic.com/generate_204");
    quint64 delayGeneration_ = 0;
    bool delayReconcilePending_ = false;
    QTimer delayReconcileTimer_;
    bool closingConnections_ = false;
    quint64 logSequence_ = 0;
    QElapsedTimer resourceSampleClock_;
    int proxyTick_ = 0;
    qint64 connectionSampleMs_ = 0;
    QElapsedTimer coreUptime_;
    std::function<QDateTime()> clock_;
    QString updatingSubscription_;
    QString applyingSubscriptionRetry_;
    QString subscriptionSourceToken_;
    bool subscriptionApplyNow_ = false;
    bool subscriptionCandidateSaved_ = false;
    bool subscriptionContentChanged_ = false;
    bool subscriptionWasPending_ = false;
    QString subscriptionOutcome_;
    QTimer deferredApplyTimer_;
    QString deferredApplyId_;
    QElapsedTimer deferredApplyClock_;
    quint64 deferredApplyToken_ = 0;
    int deferredApplyPollMs_ = 60 * 1000;
    qint64 deferredApplyLimitMs_ = 30 * 60 * 1000;
    struct VerifiedCoreArtifact {
        QString path;
        QString id;
        qint64 size = 0;
        qint64 modifiedMs = 0;
    };
    mutable VerifiedCoreArtifact verifiedCore_;
    bool shuttingDown_ = false;
    bool backgroundManaged_ = false;
    bool backgroundSuppressed_ = false;
    int recoveryFailures_ = 0;
    infra::TunRecoveryBudget tunRecoveryBudget_;
    bool tunRecoveryBlocked_ = false;
    bool tunPending_ = false;
    bool tunDisabling_ = false;
    QList<qint64> crashTimes_;
    QPointer<QNetworkReply> downloadReply_;
    infra::DownloadNetwork subscriptionNetwork_{networkRelay_};
    QUrl geoSourceBase_;
    bool geoOperationActive_ = false;
    std::shared_ptr<ProviderImport> providerImport_;
    std::shared_ptr<ConfigTransaction> configTransaction_;
    std::shared_ptr<BackupRestore> backupRestore_;
    QPointer<QNetworkReply> testReply_;
    std::unique_ptr<QTemporaryDir> staging_;
    std::function<void(bool)> validationCompletion_;
    QJsonArray profiles_;
    QJsonObject customGroupState_;
    QJsonObject customGroupLiveProviders_;
    QJsonObject customGroupDraft_;
    QJsonObject customGroupActivation_;
    bool customGroupApplying_ = false;
    QJsonObject userRuleState_;
    QJsonObject userRuleTargets_;
    QString userRuleContextRevision_;
    QString userRuleResult_;
    quint64 userRuleAttempt_ = 0;
    bool userRuleApplying_ = false;
    bool userRuleBackupApplying_ = false;
    QJsonArray groups_;
    QJsonArray configuredGroups_;
    QString defaultPolicy_;
    QJsonArray errors_;
    QJsonArray configIssues_;
    QString coreExitDetail_;
    QJsonObject runtimePreferences_;
    QJsonObject savedPreferences_;
    QJsonObject sessionPreferences_;
    QJsonObject effectivePreferences_;
    QString sessionProfileId_;
    QString effectiveProfileId_;
    QJsonObject listenerIssue_;
    bool listenerRecoveryBlocked_ = false;
    // A single TUN enable action requests at most one administrator confirmation; a second
    // failure is reported instead of showing another UAC prompt.
    bool tunSetupAttempted_ = false;
    QJsonObject candidate_;
    QJsonObject previous_;
    QJsonObject previousPreferences_;
    QJsonObject identities_;
    QJsonObject selections_;
    QJsonObject operationSnapshot_;
    std::unique_ptr<domain::Operation> operation_;
    QString pendingGroup_;
    QString pendingMember_;
    QMap<QString, QString> queuedSelections_;
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
    bool modeOnlyApply_ = false;
    bool unchangedConfigApply_ = false;
    bool applyingConfirmedSource_ = false;
    bool persistedProxyIntent_ = false;
    std::optional<bool> previousProxyIntent_;
    quint64 operationGeneration_ = 0;
    quint64 reconcileRequests_ = 0;
    quint64 reconcileCount_ = 0;
};
} // namespace quickclash::application
