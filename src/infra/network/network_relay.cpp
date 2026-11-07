#include "infra/network/network_relay.h"
#include "platform/process_memory.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QUuid>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace quickclash::infra {
struct NetworkRelay::State {
    platform::ProcessIdentity identity;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
    ~State() {
        if (job)
            CloseHandle(job);
    }
#endif
};
NetworkRelay::NetworkRelay(QObject* parent, const QString& executable)
    : QObject(parent), state_(std::make_unique<State>()), executable_(executable) {
    idle_.setSingleShot(true);
    idle_.setInterval(1000);
    deadline_.setSingleShot(true);
    deadline_.setInterval(5000);
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
    // A relay emits only a bounded readiness message. Never retain Qt/socket diagnostics,
    // which could contain an origin; stderr goes directly to the null device.
    process_.setStandardErrorFile(QProcess::nullDevice());
    connect(&idle_, &QTimer::timeout, this, [this] {
        if (!users_ && process_.state() != QProcess::NotRunning) {
            stopping_ = true;
            port_ = 0;
            process_.kill();
        }
    });
    connect(&deadline_, &QTimer::timeout, this, &NetworkRelay::fail);
    connect(&process_, &QProcess::started, this, [this] {
        state_->identity = platform::processIdentity(static_cast<quint32>(process_.processId()));
#ifdef Q_OS_WIN
        state_->job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const auto child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                       static_cast<DWORD>(process_.processId()));
        const bool owned = state_->job && child &&
                           SetInformationJobObject(state_->job, JobObjectExtendedLimitInformation,
                                                   &limits, sizeof(limits)) &&
                           AssignProcessToJobObject(state_->job, child);
        if (child)
            CloseHandle(child);
        if (!owned) {
            fail();
            return;
        }
#endif
        const auto input =
            QJsonDocument(QJsonObject{{QStringLiteral("password"), QString::fromLatin1(password_)}})
                .toJson(QJsonDocument::Compact) +
            '\n';
        if (process_.write(input) != input.size()) {
            fail();
            return;
        }
        process_.closeWriteChannel();
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        if (process_.bytesAvailable() > 1024 - response_.size()) {
            fail();
            return;
        }
        response_ += process_.readAllStandardOutput();
        if (!response_.contains('\n'))
            return;
        const auto value = QJsonDocument::fromJson(response_.trimmed()).object();
        const auto port = value.value(QStringLiteral("port")).toInt();
        if (port_ || !value.value(QStringLiteral("ready")).toBool() || port < 1 || port > 65535) {
            fail();
            return;
        }
        port_ = static_cast<quint16>(port);
        response_.clear();
        deadline_.stop();
        emit available();
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail();
    });
    connect(&process_, &QProcess::finished, this, [this] {
        deadline_.stop();
        port_ = 0;
        state_ = std::make_unique<State>();
        const bool expected = std::exchange(stopping_, false);
        if (!expected) {
            emit failed();
            return;
        }
        if (users_)
            start();
    });
}
NetworkRelay::~NetworkRelay() {
    disconnect(&process_, nullptr, this, nullptr);
    disconnect(this, nullptr, nullptr, nullptr);
    process_.kill();
    if (process_.state() != QProcess::NotRunning)
        (void)process_.waitForFinished(2000);
}
void NetworkRelay::acquire() {
    ++users_;
    idle_.stop();
    start();
}
void NetworkRelay::release() {
    if (users_ > 0 && --users_ == 0)
        idle_.start();
}
void NetworkRelay::start() {
    if (process_.state() != QProcess::NotRunning)
        return;
    stopping_ = false;
    response_.clear();
    password_ = QUuid::createUuid().toByteArray(QUuid::Id128) +
                QUuid::createUuid().toByteArray(QUuid::Id128);
    deadline_.start();
#ifdef Q_OS_WIN
    const auto name = QStringLiteral("quickclash_network.exe");
#else
    const auto name = QStringLiteral("quickclash_network");
#endif
    process_.start(executable_.isEmpty()
                       ? QDir(QCoreApplication::applicationDirPath()).filePath(name)
                       : executable_,
                   {QStringLiteral("--parent=%1").arg(QCoreApplication::applicationPid())});
}
void NetworkRelay::fail() {
    deadline_.stop();
    port_ = 0;
    stopping_ = true;
    process_.kill();
    if (process_.state() == QProcess::NotRunning)
        stopping_ = false;
    emit failed();
}
bool NetworkRelay::ready() const {
    return port_ && !stopping_ && process_.state() == QProcess::Running;
}
bool NetworkRelay::running() const {
    return process_.state() != QProcess::NotRunning;
}
QNetworkProxy NetworkRelay::proxy() const {
    return QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), port_,
                         QStringLiteral("quickclash"), QString::fromLatin1(password_));
}
QJsonObject NetworkRelay::memory() const {
    return running() ? platform::processMemory(state_->identity) : QJsonObject{};
}
} // namespace quickclash::infra
