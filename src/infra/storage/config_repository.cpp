#include "infra/storage/config_repository.h"
#include <QDir>
#include <QJsonArray>
#include <limits>

namespace quickclash::infra {
namespace {
const QString kManifest = QStringLiteral("data/runtime/current.json");
QString journalRef(const QJsonObject& journal) {
    return QStringLiteral("data/transactions/%1.json")
        .arg(journal.value(QStringLiteral("operationId")).toString());
}
bool decimal(const QJsonValue& value, bool nonzero = false) {
    if (!value.isString()) {
        return false;
    }
    const auto text = value.toString();
    bool ok = false;
    const auto number = text.toULongLong(&ok);
    return ok && (!nonzero || number != 0) && text == QString::number(number);
}
bool validManifest(const QJsonObject& manifest, JsonStore& store) {
    if (!decimal(manifest.value(QStringLiteral("manifestGeneration")))) {
        return false;
    }
    for (const auto& key : {"activeRevision", "lastGoodRevision"}) {
        const auto value = manifest.value(QLatin1String(key));
        if (!value.isNull() && !decimal(value, true)) {
            return false;
        }
    }
    const auto selection = manifest.value(QStringLiteral("selection"));
    if (!selection.isNull()) {
        if (!selection.isObject()) {
            return false;
        }
        const auto object = selection.toObject();
        if (!decimal(object.value(QStringLiteral("selectedRevision")), true) ||
            !decimal(object.value(QStringLiteral("generation")), true)) {
            return false;
        }
        for (const auto& key : {"preferenceSnapshotRef", "resourceManifestRef"}) {
            const auto reference = object.value(QLatin1String(key)).toString();
            if (reference.isEmpty() ||
                std::holds_alternative<domain::Error>(store.path(reference))) {
                return false;
            }
        }
    }
    return true;
}
QJsonObject emptyManifest() {
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("manifestGeneration"), QStringLiteral("0")},
            {QStringLiteral("selection"), QJsonValue::Null},
            {QStringLiteral("activeRevision"), QJsonValue::Null},
            {QStringLiteral("lastGoodRevision"), QJsonValue::Null}};
}
} // namespace
ConfigRepository::ConfigRepository(JsonStore& store) : store_(store), manifest_(emptyManifest()) {}
domain::CommandResult ConfigRepository::load() {
    const auto result = store_.read(kManifest);
    if (const auto* error = std::get_if<domain::Error>(&result)) {
        error_ = *error;
        return *error;
    }
    const auto& document = std::get<std::optional<QJsonObject>>(result);
    manifest_ = document.value_or(emptyManifest());
    if (!validManifest(manifest_, store_)) {
        store_.freeze();
        error_ = storageError("ManifestRecoveryRequired");
        return *error_;
    }
    const auto directory = store_.path(QStringLiteral("data/transactions"));
    if (const auto* error = std::get_if<domain::Error>(&directory)) {
        return *error;
    }
    const auto files = QDir(std::get<QString>(directory))
                           .entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const auto& file : files) {
        const auto journal = store_.read(QStringLiteral("data/transactions/") + file);
        if (const auto* error = std::get_if<domain::Error>(&journal)) {
            return *error;
        }
        const auto value = std::get<std::optional<QJsonObject>>(journal).value_or(QJsonObject{});
        const auto stage = value.value(QStringLiteral("stage")).toString();
        const auto id = value.value(QStringLiteral("operationId")).toString();
        const auto target = value.value(QStringLiteral("target")).toObject();
        const auto previous = value.value(QStringLiteral("previous")).toObject();
        const auto kind = value.value(QStringLiteral("kind")).toString();
        const QStringList stages{
            QStringLiteral("Prepared"),      QStringLiteral("CoreApplying"),
            QStringLiteral("CoreConfirmed"), QStringLiteral("PlatformApplying"),
            QStringLiteral("Compensating"),  QStringLiteral("RecoveryRequired"),
            QStringLiteral("Committed"),     QStringLiteral("RolledBack")};
        if (QUuid(id).isNull() || file != id + QStringLiteral(".json") || !stages.contains(stage) ||
            (kind != QStringLiteral("Apply") && kind != QStringLiteral("Select")) ||
            !validManifest(previous, store_) ||
            !value.value(QStringLiteral("hadRunningSession")).isBool() ||
            !validManifest(target, store_) ||
            !decimal(value.value(QStringLiteral("baseGeneration"))) ||
            target.value(QStringLiteral("manifestGeneration")).toString().toULongLong() !=
                value.value(QStringLiteral("baseGeneration")).toString().toULongLong() + 1 ||
            target.value(QStringLiteral("committedOperationId")) != id) {
            store_.freeze();
            return storageError("JournalRecoveryRequired");
        }
        if (stage == QStringLiteral("Committed") || stage == QStringLiteral("RolledBack")) {
            const auto targetGeneration =
                target.value(QStringLiteral("manifestGeneration")).toString().toULongLong();
            if ((stage == QStringLiteral("Committed") &&
                 (targetGeneration > generation() ||
                  (targetGeneration == generation() && target != manifest_))) ||
                (stage == QStringLiteral("RolledBack") &&
                 manifest_.value(QStringLiteral("committedOperationId")) == id)) {
                store_.freeze();
                return storageError("JournalManifestMismatch");
            }
            continue;
        }
        if (!pending_.isEmpty()) {
            store_.freeze();
            return storageError("JournalRecoveryRequired");
        }
        pending_ = value;
        // Manifest replacement is the commit point, even if the final log write was lost.
        if (manifest_ == target && manifest_.value(QStringLiteral("committedOperationId")) == id) {
            const auto finished = finishJournal(QStringLiteral("Committed"));
            if (std::holds_alternative<domain::Error>(finished)) {
                return finished;
            }
        } else if (value.value(QStringLiteral("baseGeneration")).toString() ==
                   manifest_.value(QStringLiteral("manifestGeneration")).toString()) {
            if (value.value(QStringLiteral("kind")) == QStringLiteral("Select")) {
                const auto rolledBack = rollback();
                if (std::holds_alternative<domain::Error>(rolledBack)) {
                    return rolledBack;
                }
            }
        } else {
            store_.freeze();
            return storageError("JournalManifestMismatch");
        }
    }
    return std::monostate{};
}
domain::Generation ConfigRepository::generation() const {
    return manifest_.value(QStringLiteral("manifestGeneration")).toString().toULongLong();
}
domain::Result<std::optional<domain::ConfigSelection>> ConfigRepository::selection() const {
    if (error_) {
        return *error_;
    }
    const auto value = manifest_.value(QStringLiteral("selection"));
    if (value.isNull()) {
        return std::optional<domain::ConfigSelection>{};
    }
    const auto object = value.toObject();
    return std::optional<domain::ConfigSelection>{domain::ConfigSelection{
        object.value(QStringLiteral("selectedRevision")).toString().toULongLong(),
        object.value(QStringLiteral("preferenceSnapshotRef")).toString(),
        object.value(QStringLiteral("resourceManifestRef")).toString(),
        object.value(QStringLiteral("generation")).toString().toULongLong()}};
}
domain::Result<domain::ConfigHistory> ConfigRepository::history() const {
    if (error_) {
        return *error_;
    }
    domain::ConfigHistory result;
    const auto active = manifest_.value(QStringLiteral("activeRevision"));
    const auto last = manifest_.value(QStringLiteral("lastGoodRevision"));
    if (!active.isNull()) {
        result.activeRevision = active.toString().toULongLong();
    }
    if (!last.isNull()) {
        result.lastGoodRevision = last.toString().toULongLong();
    }
    return result;
}
domain::Result<QUuid> ConfigRepository::prepare(QJsonObject target, const QString& kind,
                                                bool hadRunningSession) {
    if (!pending_.isEmpty() || store_.readOnly() || error_) {
        return storageError("RepositoryRecoveryRequired");
    }
    if (generation() == std::numeric_limits<domain::Generation>::max()) {
        return storageError("GenerationExhausted");
    }
    const auto id = QUuid::createUuid();
    target.insert(QStringLiteral("schemaVersion"), 1);
    target.insert(QStringLiteral("manifestGeneration"), QString::number(generation() + 1));
    target.insert(QStringLiteral("committedOperationId"), id.toString(QUuid::WithoutBraces));
    if (!validManifest(target, store_)) {
        return storageError("InvalidManifest");
    }
    QJsonObject journal{{QStringLiteral("operationId"), id.toString(QUuid::WithoutBraces)},
                        {QStringLiteral("kind"), kind},
                        {QStringLiteral("hadRunningSession"), hadRunningSession},
                        {QStringLiteral("baseGeneration"), QString::number(generation())},
                        {QStringLiteral("previous"), manifest_},
                        {QStringLiteral("target"), target},
                        {QStringLiteral("stage"), QStringLiteral("Prepared")}};
    const auto written = store_.write(journalRef(journal), journal);
    if (const auto* error = std::get_if<domain::Error>(&written)) {
        return *error;
    }
    pending_ = journal;
    return id;
}
domain::CommandResult ConfigRepository::phase(const QString& stage) {
    const QStringList stages{QStringLiteral("CoreApplying"), QStringLiteral("CoreConfirmed"),
                             QStringLiteral("PlatformApplying"), QStringLiteral("Compensating"),
                             QStringLiteral("RecoveryRequired")};
    if (pending_.isEmpty() || !stages.contains(stage)) {
        return storageError("InvalidTransactionStage");
    }
    auto next = pending_;
    next.insert(QStringLiteral("stage"), stage);
    const auto result = store_.write(journalRef(next), next);
    if (!std::holds_alternative<domain::Error>(result)) {
        pending_ = next;
    }
    return result;
}
domain::CommandResult ConfigRepository::finishJournal(const QString& stage) {
    auto next = pending_;
    next.insert(QStringLiteral("stage"), stage);
    const auto result = store_.write(journalRef(next), next);
    if (std::holds_alternative<domain::Error>(result)) {
        store_.freeze();
        return storageError("JournalFinalizeFailed");
    }
    pending_ = {};
    return std::monostate{};
}
domain::CommandResult ConfigRepository::commit() {
    if (pending_.isEmpty()) {
        return storageError("NoTransaction");
    }
    const auto target = pending_.value(QStringLiteral("target")).toObject();
    const auto result = store_.write(kManifest, target);
    if (std::holds_alternative<domain::Error>(result)) {
        return result;
    }
    manifest_ = target;
    return finishJournal(QStringLiteral("Committed"));
}
domain::CommandResult ConfigRepository::rollback() {
    if (pending_.isEmpty()) {
        return std::monostate{};
    }
    if (manifest_.value(QStringLiteral("committedOperationId")) ==
        pending_.value(QStringLiteral("operationId"))) {
        return storageError("AlreadyCommitted");
    }
    return finishJournal(QStringLiteral("RolledBack"));
}
domain::CommandResult ConfigRepository::selectForNextStart(const domain::ConfigSelection& selection,
                                                           domain::Generation expectedGeneration) {
    if (expectedGeneration != generation() || selection.generation != generation() + 1) {
        return storageError("SelectionConflict");
    }
    auto target = manifest_;
    target.insert(
        QStringLiteral("selection"),
        QJsonObject{
            {QStringLiteral("selectedRevision"), QString::number(selection.selectedRevision)},
            {QStringLiteral("preferenceSnapshotRef"), selection.preferenceSnapshotRef},
            {QStringLiteral("resourceManifestRef"), selection.resourceManifestRef},
            {QStringLiteral("generation"), QString::number(selection.generation)}});
    const auto operation = prepare(target, QStringLiteral("Select"), false);
    if (const auto* error = std::get_if<domain::Error>(&operation)) {
        return *error;
    }
    return commit();
}
} // namespace quickclash::infra
