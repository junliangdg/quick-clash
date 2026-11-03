#pragma once
#include "infra/storage/settings_service.h"
#include "presentation/feature_registry.h"
#include "presentation/profile_model.h"
#include "presentation/replay_model.h"
#include "presentation/runtime_list_model.h"
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace quickclash::presentation {
// Qt type registration may instantiate QQmlElement<UiState> even for uncreatable types.
class UiState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ApplicationHost")
    Q_PROPERTY(QVariantList navigation READ navigation NOTIFY navigationChanged)
    Q_PROPERTY(QVariantList compactNavigation READ compactNavigation NOTIFY navigationChanged)
    Q_PROPERTY(QString runtimeStatus READ runtimeStatus NOTIFY runtimeChanged)
    Q_PROPERTY(QString runtimeError READ runtimeError NOTIFY runtimeChanged)
    Q_PROPERTY(QString currentPage READ currentPage NOTIFY routeChanged)
    Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY routeChanged)
    Q_PROPERTY(QUrl pageSource READ pageSource NOTIFY routeChanged)
    Q_PROPERTY(QString routeError READ routeError NOTIFY routeErrorChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeChanged)
    Q_PROPERTY(
        QString languageMode READ languageMode WRITE setLanguageMode NOTIFY languageModeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)
    Q_PROPERTY(bool uiVisible READ uiVisible NOTIFY uiVisibleChanged)
    Q_PROPERTY(bool replay READ replay WRITE setReplay NOTIFY replayChanged)
    Q_PROPERTY(int replayConnections READ replayConnections NOTIFY replayChanged)
    Q_PROPERTY(int activeDemandCount READ activeDemandCount NOTIFY demandsChanged)
    Q_PROPERTY(int activeStreamCount READ activeStreamCount NOTIFY demandsChanged)
    Q_PROPERTY(bool replayRunning READ replayRunning NOTIFY demandsChanged)
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageChanged)
    Q_PROPERTY(bool settingsReady READ settingsReady NOTIFY storageChanged)
    Q_PROPERTY(QVariantMap dashboard READ dashboard NOTIFY storageChanged)
    Q_PROPERTY(QVariantMap runtime READ runtime NOTIFY runtimeChanged)
    Q_PROPERTY(QVariantMap telemetry READ telemetry NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantMap editor READ editor NOTIFY editorChanged)
    Q_PROPERTY(QVariantMap customGroups READ customGroups NOTIFY customGroupsChanged)
    Q_PROPERTY(QVariantMap userRules READ userRules NOTIFY userRulesChanged)
    Q_PROPERTY(QVariantMap userRulesDraft READ userRulesDraft WRITE setUserRulesDraft NOTIFY
                   userRulesDraftChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(quickclash::presentation::ProfileModel* profileModel READ profileModel CONSTANT)
    Q_PROPERTY(quickclash::presentation::ProfileRowModel* profileRows READ profileRows CONSTANT)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY nodesChanged)
    Q_PROPERTY(QVariantMap delays READ delays NOTIFY delaysChanged)
    Q_PROPERTY(QVariantMap delayDetails READ delayDetails NOTIFY delaysChanged)
    Q_PROPERTY(QVariantMap proxyStates READ proxyStates NOTIFY proxyStatesChanged)
    Q_PROPERTY(
        quickclash::presentation::RuntimeListModel* liveConnections READ liveConnections CONSTANT)
    Q_PROPERTY(quickclash::presentation::RuntimeListModel* liveRules READ liveRules CONSTANT)
    Q_PROPERTY(
        quickclash::presentation::RuntimeListModel* liveProviders READ liveProviders CONSTANT)
    Q_PROPERTY(quickclash::presentation::RuntimeListModel* liveLogs READ liveLogs CONSTANT)
    Q_PROPERTY(QString upload READ upload NOTIFY sampleChanged)
    Q_PROPERTY(QString download READ download NOTIFY sampleChanged)
    Q_PROPERTY(QVariantList uploadHistory READ uploadHistory NOTIFY sampleChanged)
    Q_PROPERTY(QVariantList downloadHistory READ downloadHistory NOTIFY sampleChanged)
    Q_PROPERTY(quickclash::presentation::ReplayModel* proxyModel READ proxyModel CONSTANT)
    Q_PROPERTY(quickclash::presentation::ReplayModel* connectionModel READ connectionModel CONSTANT)
    Q_PROPERTY(bool trayAvailable READ trayAvailable WRITE setTrayAvailable NOTIFY hostChanged)
    Q_PROPERTY(QString hostState READ hostState WRITE setHostState NOTIFY hostChanged)
    Q_PROPERTY(QString hostStatus READ hostStatus NOTIFY hostChanged)
    Q_PROPERTY(
        bool unloadWhenHidden READ unloadWhenHidden WRITE setUnloadWhenHidden NOTIFY hostChanged)
  public:
    explicit UiState(QString settingsFile = {}, QObject* parent = nullptr);
    [[nodiscard]] QVariantMap runtime() const {
        return runtime_;
    }
    [[nodiscard]] QVariantList profiles() const {
        return appliedData_.value(QStringLiteral("profiles")).toArray().toVariantList();
    }
    [[nodiscard]] ProfileModel* profileModel() {
        return &profileModel_;
    }
    [[nodiscard]] ProfileRowModel* profileRows() {
        return &profileRows_;
    }
    [[nodiscard]] QVariantMap telemetry() const {
        return telemetry_;
    }
    [[nodiscard]] QVariantMap customGroups() const {
        return appliedData_.value(QStringLiteral("custom_groups")).toObject().toVariantMap();
    }
    [[nodiscard]] QVariantMap userRules() const {
        return appliedData_.value(QStringLiteral("user_rules")).toObject().toVariantMap();
    }
    [[nodiscard]] QVariantMap userRulesDraft() const {
        return userRulesDraft_;
    }
    void setUserRulesDraft(const QVariantMap& draft) {
        if (userRulesDraft_ == draft)
            return;
        userRulesDraft_ = draft;
        emit userRulesDraftChanged();
    }
    [[nodiscard]] QVariantMap editor() const {
        return appliedData_.value(QStringLiteral("editor")).toObject().toVariantMap();
    }
    [[nodiscard]] QVariantList groups() const {
        return groups_;
    }
    [[nodiscard]] QVariantList nodes() const {
        return nodes_;
    }
    [[nodiscard]] QVariantMap delays() const {
        return delays_;
    }
    [[nodiscard]] QVariantMap delayDetails() const {
        return delayDetails_;
    }
    [[nodiscard]] QVariantMap proxyStates() const {
        return proxyStates_;
    }
    Q_INVOKABLE QVariantMap proxyState(const QString& name, const QString& group = {}) const;
    [[nodiscard]] RuntimeListModel* liveConnections() {
        return &liveConnections_;
    }
    [[nodiscard]] RuntimeListModel* liveRules() {
        return &liveRules_;
    }
    [[nodiscard]] RuntimeListModel* liveProviders() {
        return &liveProviders_;
    }
    [[nodiscard]] RuntimeListModel* liveLogs() {
        return &liveLogs_;
    }
    void applyControl(const QJsonObject& control);
    void applyData(const application::RuntimeUpdate& update);
    [[nodiscard]] const application::RuntimeDemands& dataDemands() const {
        return dataDemands_;
    }
    // Explicit diagnostic/test compatibility only; never bound to a production QML property.
    [[nodiscard]] QJsonObject diagnosticSnapshot() const;
    Q_INVOKABLE void setLogsPaused(bool paused);
    Q_INVOKABLE void runtimeCommand(const QString& command, const QStringList& arguments = {});
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE void setDashboardOption(const QString& key, bool value);
    [[nodiscard]] QVariantMap dashboard() const {
        return dashboard_.toVariantMap();
    }
    [[nodiscard]] QString storageError() const {
        return storageError_;
    }
    [[nodiscard]] bool settingsReady() const {
        return settingsReady_;
    }
    [[nodiscard]] QVariantList navigation() const;
    [[nodiscard]] QVariantList compactNavigation() const;
    [[nodiscard]] QString runtimeStatus() const;
    [[nodiscard]] QString runtimeError() const;
    [[nodiscard]] QString currentPage() const {
        return currentPage_;
    }
    [[nodiscard]] QString pageTitle() const;
    [[nodiscard]] QUrl pageSource() const;
    [[nodiscard]] QString routeError() const {
        return routeError_;
    }
    [[nodiscard]] QString themeMode() const {
        return themeMode_;
    }
    [[nodiscard]] bool dark() const;
    void setThemeMode(const QString& mode);
    [[nodiscard]] QString languageMode() const {
        return languageMode_;
    }
    void setLanguageMode(const QString& mode);
    void retranslateUi();
    void setSystemDark(bool dark);
    [[nodiscard]] bool uiVisible() const {
        return uiVisible_;
    }
    // Minimized windows retain only the host traffic lease until hidden or unloaded.
    void setUiVisible(bool visible, bool minimized = false);
    [[nodiscard]] bool replay() const {
        return replay_;
    }
    void setReplay(bool replay);
    [[nodiscard]] int replayConnections() const {
        return replayConnections_;
    }
    [[nodiscard]] bool setReplayConnections(int rows);
    [[nodiscard]] int activeDemandCount() const {
        return static_cast<int>(demands_.size());
    }
    [[nodiscard]] int activeStreamCount() const;
    [[nodiscard]] bool replayRunning() const {
        return replayTimer_.isActive();
    }
    [[nodiscard]] QString upload() const;
    [[nodiscard]] QString download() const;
    [[nodiscard]] QVariantList uploadHistory() const;
    [[nodiscard]] QVariantList downloadHistory() const;
    [[nodiscard]] ReplayModel* proxyModel() {
        return &proxyModel_;
    }
    [[nodiscard]] ReplayModel* connectionModel() {
        return &connectionModel_;
    }
    [[nodiscard]] bool trayAvailable() const {
        return trayAvailable_;
    }
    void setTrayAvailable(bool available);
    [[nodiscard]] QString hostState() const {
        return hostState_;
    }
    [[nodiscard]] QString hostStatus() const;
    void setHostState(const QString& state);
    [[nodiscard]] bool unloadWhenHidden() const {
        return unloadWhenHidden_;
    }
    void setUnloadWhenHidden(bool enabled);
    [[nodiscard]] bool closeAcknowledged() const {
        return closeAcknowledged_;
    }
    void acknowledgeClose();
    Q_INVOKABLE bool navigate(const QString& pageId, const QVariantMap& context = {});
    Q_INVOKABLE QUrl embeddedSource(const QString& contentId,
                                    const QVariantMap& context = {}) const;
    Q_INVOKABLE void setDemand(const QString& instanceKey, const QString& stream, bool active);
    Q_INVOKABLE QString createDemandKey(const QString& scope) const;
    Q_INVOKABLE QVariantMap readState(const QString& instanceKey) const;
    Q_INVOKABLE void saveState(const QString& instanceKey, const QVariantMap& state);
    Q_INVOKABLE void setFilter(const QString& pageId, const QString& text);
    Q_INVOKABLE void requestClose(bool confirmed = false) {
        emit closeRequested(confirmed);
    }
    Q_INVOKABLE void requestQuit() {
        emit quitRequested();
    }
    Q_INVOKABLE void requestMinimize() {
        emit minimizeRequested();
    }
    Q_INVOKABLE void requestUnload() {
        emit unloadRequested();
    }
  signals:
    void subscriptionUrlReady(const QString& id, const QString& token, const QString& url);
    void routeChanged();
    void storageChanged();
    void runtimeChanged();
    void profilesChanged();
    void telemetryChanged();
    void editorChanged();
    void customGroupsChanged();
    void userRulesChanged();
    void userRulesDraftChanged();
    void groupsChanged();
    void nodesChanged();
    void delaysChanged();
    void proxyStatesChanged();
    void operationCompleted(const QUuid& id, domain::OperationState state,
                            const QString& errorCode);
    void filterChanged(const QString& pageId, const QString& text);
    void routeErrorChanged();
    void themeChanged();
    void languageModeChanged(const QString& mode);
    void navigationChanged();
    void uiVisibleChanged();
    void uiAboutToHide();
    void replayChanged();
    void demandsChanged();
    void channelInvalidated(const QString& channel);
    void sampleChanged();
    void hostChanged();
    void closeRequested(bool confirmed);
    void quitRequested();
    void minimizeRequested();
    void unloadRequested();
    void closeConfirmationRequired();

  private:
    [[nodiscard]] bool acceptGeneration(quint64 generation, const QString& epoch);
    void updateTimer();
    void clearChannel(const QString& channel);
    void clearTrafficHistory();
    void refreshTelemetry();
    [[nodiscard]] QVariantList history(bool upload) const;
    void persist(const QString& key, const QVariant& value);
    void applyPreferences(const QString& theme, bool unload, bool acknowledged,
                          const QString& language, const QJsonObject& dashboard);
    FeatureRegistry registry_;
    QString currentPage_ = QStringLiteral("home");
    QString routeError_;
    QString themeMode_ = QStringLiteral("system");
    QString languageMode_ = QStringLiteral("system");
    bool systemDark_ = false;
    bool uiVisible_ = false;
    bool uiMinimized_ = false;
    bool replay_ = false;
    int replayConnections_ = 1000;
    bool trayAvailable_ = false;
    bool unloadWhenHidden_ = false;
    bool closeAcknowledged_ = false;
    QJsonObject dashboard_ = infra::UiPreferences{}.dashboard;
    QString hostState_ = QStringLiteral("Unloaded");
    QHash<QString, QString> demands_;
    application::RuntimeDemands dataDemands_;
    QHash<QString, QVariantMap> states_;
    QTimer replayTimer_;
    int sample_ = 0;
    ReplayModel proxyModel_{ReplayModel::Kind::Proxies};
    ReplayModel connectionModel_{ReplayModel::Kind::Connections};
    bool settingsReady_ = false;
    QString storageError_;
    QVariantMap runtime_;
    QVariantMap userRulesDraft_;
    ProfileModel profileModel_{this};
    ProfileRowModel profileRows_{&profileModel_, this};
    QVariantList groups_;
    QVariantList nodes_;
    QVariantMap delays_;
    QVariantMap delayDetails_;
    QVariantMap proxyStates_;
    QHash<QString, QVariantMap> proxyGroups_;
    void rebuildProxyStates();
    QVariantMap telemetry_;
    QJsonObject appliedData_;
    quint64 publicationGeneration_ = 0;
    bool logsPaused_ = false;
    QJsonArray pendingLogs_;
    RuntimeRows connectionRows_{RuntimeRows::Kind::Connections, this};
    RuntimeRows ruleRows_{RuntimeRows::Kind::Rules, this};
    RuntimeRows providerRows_{RuntimeRows::Kind::Providers, this};
    RuntimeRows logRows_{RuntimeRows::Kind::Logs, this};
    RuntimeListModel liveConnections_{&connectionRows_, true, this};
    RuntimeListModel liveRules_{&ruleRows_, false, this};
    RuntimeListModel liveProviders_{&providerRows_, false, this};
    RuntimeListModel liveLogs_{&logRows_, false, this};
    QVariantList liveUpload_;
    QVariantList liveDownload_;
    QString trafficSample_;
    QString liveEpoch_;
    std::unique_ptr<infra::SettingsService> settings_;
};
} // namespace quickclash::presentation
