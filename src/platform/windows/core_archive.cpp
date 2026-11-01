#include "platform/core_archive.h"
#include "infra/storage/json_store.h"
#include "platform/core_artifacts.h"
#include "platform/windows/owned_process.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace quickclash::platform {
struct CoreArchive::State {
    OwnedProcess process;
    QTimer deadline;
    std::function<void(domain::CommandResult)> completion;
};
CoreArchive::CoreArchive(QObject* parent) : QObject(parent), state_(std::make_unique<State>()) {
    state_->deadline.setSingleShot(true);
    connect(&state_->process, &OwnedProcess::finished, this, [this](int code) {
        state_->deadline.stop();
        auto completion = std::move(state_->completion);
        if (completion)
            completion(code == 0 ? domain::CommandResult{std::monostate{}}
                                 : domain::CommandResult{infra::storageError("CoreArchiveFailed")});
    });
    connect(&state_->deadline, &QTimer::timeout, this, [this] {
        state_->process.stop();
        auto completion = std::move(state_->completion);
        if (completion)
            completion(infra::storageError("CoreArchiveFailed"));
    });
}
CoreArchive::~CoreArchive() {
    cancel();
}
bool CoreArchive::running() const {
    return state_->process.running();
}
QJsonObject CoreArchive::memory() const {
    return state_->process.memory();
}
void CoreArchive::cancel() {
    state_->completion = {};
    state_->deadline.stop();
    state_->process.stop();
}
void CoreArchive::extract(const QString& archive, const QString& directory, const QString& id,
                          std::function<void(domain::CommandResult)> completion) {
    cancel();
    const auto artifact = coreArtifact(id);
    QFile file(archive);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const bool verified =
        !artifact.isEmpty() && file.open(QIODevice::ReadOnly) &&
        file.size() == artifact.value("archiveBytes").toInteger() && hash.addData(&file) &&
        hash.result().toHex() == artifact.value("archiveSha256").toString().toLatin1();
    file.close();
    if (!verified) {
        completion(infra::storageError("CoreIntegrityFailed"));
        return;
    }
    wchar_t system[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(system, MAX_PATH);
    const auto tar = QDir(QString::fromWCharArray(system)).filePath(QStringLiteral("tar.exe"));
    if (!length || length >= MAX_PATH || !QFileInfo::exists(tar)) {
        completion(infra::storageError("CoreArchiveUnavailable"));
        return;
    }
    // The catalog authorizes exact archive bytes and one reviewed, flat executable entry.
    // No shell, remote metadata, caller paths or wildcard names reach tar arguments.
    const auto result = state_->process.start(
        tar,
        {QStringLiteral("-x"), QStringLiteral("-f"), archive, QStringLiteral("-C"), directory,
         QStringLiteral("--no-same-owner"), artifact.value("entry").toString()},
        directory);
    if (const auto* error = std::get_if<domain::Error>(&result)) {
        completion(*error);
        return;
    }
    state_->completion = std::move(completion);
    state_->deadline.start(30000);
}
} // namespace quickclash::platform
