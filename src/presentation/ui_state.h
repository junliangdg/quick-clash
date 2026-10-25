#pragma once
#include "infra/storage/settings_service.h"
#include "presentation/feature_registry.h"
#include "presentation/replay_model.h"
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
    Q_PROPERTY(QVariantList navigation READ navigation CONSTANT)
    Q_PROPERTY(QVariantList compactNavigation READ compactNavigation CONSTANT)
    Q_PROPERTY(QString runtimeStatus READ runtimeStatus NOTIFY runtimeChanged)
    Q_PROPERTY(QString runtimeError READ runtimeError NOTIFY runtimeChanged)
    Q_PROPERTY(QString currentPage READ currentPage NOTIFY routeChanged)
    Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY routeChanged)
    Q_PROPERTY(QUrl pageSource READ pageSource NOTIFY routeChanged)
    Q_PROPERTY(QString routeError READ routeError NOTIFY routeErrorChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)
    Q_PROPERTY(bool uiVisible READ uiVisible NOTIFY uiVisibleChanged)
    Q_PROPERTY(bool replay READ replay WRITE setReplay NOTIFY replayChanged)
    Q_PROPERTY(int replayConnections READ replayConnections NOTIFY replayChanged)
    Q_PROPERTY(int activeDemandCount READ activeDemandCount NOTIFY demandsChanged)
    Q_PROPERTY(int activeStreamCount READ activeStreamCount NOTIFY demandsChanged)
    Q_PROPERTY(bool replayRunning READ replayRunning NOTIFY demandsChanged)
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageChanged)
    Q_PROPERTY(bool settingsReady READ settingsReady NOTIFY storageChanged)
    Q_PROPERTY(QVariantMap runtime READ runtime NOTIFY runtimeChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
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
        return profiles_;
    }
    Q_INVOKABLE void runtimeCommand(const QString& command, const QStringList& arguments = {});
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
    void setSystemDark(bool dark);
    [[nodiscard]] bool uiVisible() const {
        return uiVisible_;
    }
    void setUiVisible(bool visible);
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
    void routeChanged();
    void storageChanged();
    void runtimeChanged();
    void profilesChanged();
    void filterChanged(const QString& pageId, const QString& text);
    void routeErrorChanged();
    void themeChanged();
    void uiVisibleChanged();
    void replayChanged();
    void demandsChanged();
    void sampleChanged();
    void hostChanged();
    void closeRequested(bool confirmed);
    void quitRequested();
    void minimizeRequested();
    void unloadRequested();
    void closeConfirmationRequired();

  private:
    void updateTimer();
    [[nodiscard]] QVariantList history(bool upload) const;
    void persist(const QString& key, const QVariant& value);
    void applyPreferences(const QString& theme, bool unload, bool acknowledged);
    FeatureRegistry registry_;
    QString currentPage_ = QStringLiteral("home");
    QString routeError_;
    QString themeMode_ = QStringLiteral("system");
    bool systemDark_ = false;
    bool uiVisible_ = false;
    bool replay_ = false;
    int replayConnections_ = 1000;
    bool trayAvailable_ = false;
    bool unloadWhenHidden_ = false;
    bool closeAcknowledged_ = false;
    QString hostState_ = QStringLiteral("Unloaded");
    QHash<QString, QString> demands_;
    QHash<QString, QVariantMap> states_;
    QTimer replayTimer_;
    int sample_ = 0;
    ReplayModel proxyModel_{ReplayModel::Kind::Proxies};
    ReplayModel connectionModel_{ReplayModel::Kind::Connections};
    bool settingsReady_ = false;
    QString storageError_;
    QVariantMap runtime_;
    QVariantList profiles_;
    QVariantList liveUpload_;
    QVariantList liveDownload_;
    QString trafficSample_;
    QString liveEpoch_;
    std::unique_ptr<infra::SettingsService> settings_;
};
} // namespace quickclash::presentation
