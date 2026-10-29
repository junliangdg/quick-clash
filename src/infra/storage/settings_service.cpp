#include "infra/storage/settings_service.h"
#include "application/desktop_runtime.h"
#include "infra/storage/config_repository.h"
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

namespace quickclash::infra {
struct SettingsService::State {
    std::unique_ptr<JsonStore> store;
    bool available = false;
    std::unique_ptr<application::DesktopRuntime> runtime;
};
SettingsService::SettingsService(QString root, QObject* parent)
    : QObject(parent), worker_(new QObject), state_(std::make_shared<State>()) {
    worker_->moveToThread(&thread_);
    qRegisterMetaType<application::RuntimeUpdate>();
    qRegisterMetaType<domain::OperationState>();
    connect(&mailbox_, &LatestMailbox::delivered, this, &SettingsService::dataChanged);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    const auto state = state_;
    connect(&thread_, &QThread::started, worker_, [this, state, root] {
        state->store = std::make_unique<JsonStore>(root);
        auto result = state->store->open();
        if (const auto* error = std::get_if<domain::Error>(&result)) {
            emit failed(error->code);
            return;
        }
        state->runtime = std::make_unique<application::DesktopRuntime>(*state->store);
        connect(state->runtime.get(), &application::DesktopRuntime::changed, this,
                &SettingsService::runtimeChanged);
        // The direct producer only touches the mailbox's locked value state, never GUI models.
        connect(state->runtime.get(), &application::DesktopRuntime::dataPublished, worker_,
                mailbox_.publisher(), Qt::DirectConnection);
        connect(state->runtime.get(), &application::DesktopRuntime::operationCompleted, this,
                &SettingsService::operationCompleted);
        SettingsRepository settings(*state->store);
        auto preferences = settings.load();
        if (const auto* error = std::get_if<domain::Error>(&preferences)) {
            emit failed(error->code);
            // Recovery UI remains available, but no defaults are persisted and no core starts.
            state->available = true;
            state->runtime->initialize();
            emit runtimeChanged(state->runtime->snapshot());
            emit loaded(QStringLiteral("system"), false, false);
            return;
        }
        auto value = std::get<UiPreferences>(preferences);
        state->runtime->initialize();
        // One-time P1 UI preference migration; source remains as a backup.
        const auto current = state->store->path(QStringLiteral("config/settings.json"));
        const auto legacy = root + QStringLiteral("/ui.ini");
        if (!state->store->readOnly() && std::holds_alternative<QString>(current) &&
            !QFileInfo::exists(std::get<QString>(current)) && QFileInfo::exists(legacy)) {
            QSettings old(legacy, QSettings::IniFormat);
            value.theme = old.value(QStringLiteral("theme"), QStringLiteral("system")).toString();
            value.unloadWhenHidden =
                old.value(QStringLiteral("unload_when_hidden"), false).toBool();
            value.closeAcknowledged =
                old.value(QStringLiteral("close_acknowledged"), false).toBool();
            const auto migrated = settings.save(value);
            if (const auto* error = std::get_if<domain::Error>(&migrated)) {
                emit failed(error->code);
                return;
            }
        }
        state->available = true;
        if (!state->store->readOnly())
            state->runtime->enableBackgroundManagement();
        emit loaded(value.theme, value.unloadWhenHidden, value.closeAcknowledged);
    });
    connect(
        &thread_, &QThread::finished, worker_,
        [state] {
            state->runtime.reset();
            state->store.reset();
        },
        Qt::DirectConnection);
    QTimer::singleShot(0, this, [this] { thread_.start(); });
}
SettingsService::~SettingsService() {
    mailbox_.close();
    // A barrier drains already queued saves before the event loop is stopped.
    if (!thread_.isRunning()) {
        thread_.start();
    }
    QMetaObject::invokeMethod(worker_, [] {}, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}
void SettingsService::save(UiPreferences preferences) {
    const auto state = state_;
    QMetaObject::invokeMethod(worker_, [this, state, preferences] {
        if (!state->available) {
            emit failed(QStringLiteral("SettingsUnavailable"));
            return;
        }
        SettingsRepository repository(*state->store);
        const auto result = repository.save(preferences);
        if (const auto* error = std::get_if<domain::Error>(&result)) {
            emit failed(error->code);
        } else {
            emit saved(preferences.theme, preferences.unloadWhenHidden,
                       preferences.closeAcknowledged);
        }
    });
}
void SettingsService::setDemands(const application::RuntimeDemands& demands) {
    // Revoke queued values synchronously, before the worker can observe the new leases.
    mailbox_.setDemands(demands);
    const auto state = state_;
    QMetaObject::invokeMethod(worker_, [state, demands] {
        if (state->runtime)
            state->runtime->applyDemands(demands);
    });
}
void SettingsService::command(const QString& name, const QStringList& arguments) {
    const auto state = state_;
    QMetaObject::invokeMethod(worker_, [this, state, name, arguments] {
        if (!state->runtime) {
            emit failed(QStringLiteral("RuntimeUnavailable"));
            return;
        }
        auto& runtime = *state->runtime;
        const auto argument = [&arguments](qsizetype index) { return arguments.value(index); };
        if (name == QStringLiteral("import")) {
            runtime.importFile(argument(0), argument(1));
        } else if (name == QStringLiteral("subscribe")) {
            runtime.addSubscription(argument(0), argument(1),
                                    argument(2).isEmpty() ? QStringLiteral("Direct") : argument(2));
        } else if (name == QStringLiteral("subscription_route")) {
            runtime.setSubscriptionRoute(argument(0), argument(1));
        } else if (name == QStringLiteral("update")) {
            runtime.updateProfile(argument(0));
        } else if (name == QStringLiteral("subscription_policy")) {
            runtime.configureSubscription(argument(0), argument(1).toInt(),
                                          argument(2) == QStringLiteral("true"));
        } else if (name == QStringLiteral("source")) {
            runtime.changeProfileSource(argument(0), argument(1), argument(2));
        } else if (name == QStringLiteral("select")) {
            runtime.selectProfile(argument(0));
        } else if (name == QStringLiteral("rename")) {
            runtime.renameProfile(argument(0), argument(1));
        } else if (name == QStringLiteral("delete")) {
            runtime.deleteProfile(argument(0));
        } else if (name == QStringLiteral("start")) {
            (void)runtime.startCore(runtime.selectionGeneration());
        } else if (name == QStringLiteral("start_proxy")) {
            (void)runtime.startProxy(runtime.selectionGeneration());
        } else if (name == QStringLiteral("stop")) {
            (void)runtime.stopProxy(domain::StopReason::UserStop);
        } else if (name == QStringLiteral("mode")) {
            runtime.setMode(argument(0));
        } else if (name == QStringLiteral("proxy")) {
            runtime.selectProxy(argument(0), argument(1));
        } else if (name == QStringLiteral("ports")) {
            runtime.setPorts(argument(0).toInt(), argument(1).toInt());
        } else if (name == QStringLiteral("test")) {
            runtime.testWebsite(argument(0), argument(1).isEmpty() ? QStringLiteral("CurrentProxy")
                                                                   : argument(1));
        } else if (name == QStringLiteral("refresh")) {
            runtime.refresh();
        } else if (name == QStringLiteral("system_proxy")) {
            (void)runtime.setSystemProxy(argument(0) == QStringLiteral("true"));
        } else if (name == QStringLiteral("reconcile")) {
            runtime.reconcile();
        } else if (name == QStringLiteral("preview")) {
            runtime.previewConfiguration();
        } else if (name == QStringLiteral("reapply_system_proxy")) {
            runtime.reapplySystemProxy();
        } else {
            runtime.featureCommand(name, arguments);
        }
    });
}
} // namespace quickclash::infra
