#include "application/desktop_runtime.h"
#include "infra/config/config_overlay.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>

namespace quickclash::application {
namespace {
QString digest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString draftReference(const QString& id, const QString& kind) {
    return QStringLiteral("data/drafts/%1_%2.json").arg(id, kind);
}
} // namespace
QString DesktopRuntime::profileRevision(const QJsonObject& item) const {
    return digest(
        QJsonDocument(QJsonObject{{QStringLiteral("source"), item.value("sourceHash")},
                                  {QStringLiteral("snapshot"), item.value("snapshotHash")},
                                  {QStringLiteral("overlay"), item.value("overlay")}})
            .toJson(QJsonDocument::Compact));
}
void DesktopRuntime::openEditor(const QString& id, const QString& kind, bool discard) {
    if (!featureDemands_.contains(QStringLiteral("editor")) || busy_ ||
        (kind != QStringLiteral("source") && kind != QStringLiteral("overlay")))
        return;
    const auto item = profile(id);
    if (item.isEmpty()) {
        error(QStringLiteral("ProfileNotFound"));
        return;
    }
    const auto reference = draftReference(id, kind);
    QJsonObject draft;
    if (!discard) {
        const auto path = store_.path(reference);
        if (const auto* value = std::get_if<QString>(&path); value && QFileInfo::exists(*value)) {
            const auto bytes = store_.readBytes(reference);
            if (const auto* loaded = std::get_if<QByteArray>(&bytes))
                draft = QJsonDocument::fromJson(*loaded).object();
            if (draft.value("schemaVersion").toInt() != 1 || draft.value("id") != id ||
                draft.value("kind") != kind || !draft.value("text").isString() ||
                draft.value("text").toString().toUtf8().size() > 1024 * 1024) {
                error(QStringLiteral("InvalidEditorDraft"));
                return;
            }
        }
    }
    if (draft.isEmpty()) {
        QString text;
        if (kind == QStringLiteral("source")) {
            const auto bytes = store_.readBytes(item.value("sourceRef").toString());
            if (!std::holds_alternative<QByteArray>(bytes) ||
                digest(std::get<QByteArray>(bytes)) != item.value("sourceHash")) {
                error(QStringLiteral("SourceIntegrityFailed"));
                return;
            }
            if (std::get<QByteArray>(bytes).size() > 1024 * 1024) {
                error(QStringLiteral("EditorLimitExceeded"));
                return;
            }
            text = QString::fromUtf8(std::get<QByteArray>(bytes));
            if (text.toUtf8() != std::get<QByteArray>(bytes)) {
                error(QStringLiteral("EditorUtf8Required"));
                return;
            }
        } else {
            text = QString::fromUtf8(
                QJsonDocument(item.value("overlay").toObject()).toJson(QJsonDocument::Indented));
        }
        draft = {{QStringLiteral("schemaVersion"), 1},
                 {QStringLiteral("id"), id},
                 {QStringLiteral("kind"), kind},
                 {QStringLiteral("text"), text},
                 {QStringLiteral("baseRevision"), profileRevision(item)},
                 {QStringLiteral("sourceHash"), item.value("sourceHash")},
                 {QStringLiteral("baseSourceRef"), item.value("sourceRef")},
                 {QStringLiteral("baseSnapshotRef"), item.value("snapshotRef")},
                 {QStringLiteral("baseOverlay"), item.value("overlay").toObject()}};
    }
    editor_ = draft;
    editor_.insert(QStringLiteral("token"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    editor_.insert(QStringLiteral("name"), item.value("name"));
    editor_.insert(QStringLiteral("runningRevision"),
                   repository_.manifest().value("activeRevision"));
    editor_.insert(QStringLiteral("conflict"),
                   editor_.value("baseRevision") != profileRevision(item));
    editor_.insert(QStringLiteral("draftSaved"),
                   !discard && QFileInfo::exists(std::get<QString>(store_.path(reference))));
    if (discard) {
        const auto removed = store_.remove(reference);
        if (const auto* failure = std::get_if<domain::Error>(&removed)) {
            error(failure->code);
            return;
        }
    }
    publish();
}
bool DesktopRuntime::saveEditorDraft(const QString& token, const QString& text) {
    if (editor_.isEmpty() || token != editor_.value("token") || store_.readOnly())
        return false;
    if (text.toUtf8().size() > 1024 * 1024) {
        error(QStringLiteral("EditorLimitExceeded"));
        return false;
    }
    editor_.insert(QStringLiteral("text"), text);
    editor_.remove(QStringLiteral("preview"));
    editor_.remove(QStringLiteral("previousPreview"));
    editor_.remove(QStringLiteral("previewDigest"));
    editor_.remove(QStringLiteral("previewPreferences"));
    const auto item = profile(editor_.value("id").toString());
    editor_.insert(QStringLiteral("conflict"),
                   item.isEmpty() || editor_.value("baseRevision") != profileRevision(item) ||
                       editor_.value("sourceHash") != item.value("sourceHash"));
    auto draft = editor_;
    for (const auto* key : {"token", "conflict", "draftSaved", "name", "runningRevision",
                            "previewDigest", "previewPreferences", "candidateSaved"})
        draft.remove(QLatin1String(key));
    const auto saved = store_.writeBytes(
        draftReference(editor_.value("id").toString(), editor_.value("kind").toString()),
        QJsonDocument(draft).toJson(QJsonDocument::Compact));
    if (const auto* failure = std::get_if<domain::Error>(&saved)) {
        error(failure->code);
        return false;
    }
    editor_.insert(QStringLiteral("draftSaved"), true);
    lastError_.clear();
    publish();
    return true;
}
void DesktopRuntime::prepareEditor(bool copy, const QString& name) {
    if (busy_ || store_.readOnly() || editor_.isEmpty())
        return;
    const auto id = editor_.value("id").toString();
    const auto item = profile(id);
    if (item.isEmpty()) {
        error(QStringLiteral("ProfileNotFound"));
        return;
    }
    const bool conflict = editor_.value("baseRevision") != profileRevision(item) ||
                          editor_.value("sourceHash") != item.value("sourceHash");
    editor_.insert(QStringLiteral("conflict"), conflict);
    const bool preview = editor_.value("action") == QStringLiteral("preview");
    if (conflict && !copy && !preview) {
        error(QStringLiteral("EditorConflict"));
        return;
    }
    QByteArray bytes;
    auto overlay = editor_.value("baseOverlay").toObject();
    if (editor_.value("kind") == QStringLiteral("source")) {
        bytes = editor_.value("text").toString().toUtf8();
    } else {
        const auto parsed = QJsonDocument::fromJson(editor_.value("text").toString().toUtf8());
        if (!parsed.isObject()) {
            error(QStringLiteral("InvalidOverlay"));
            return;
        }
        overlay = parsed.object();
        const auto source = store_.readBytes(editor_.value("baseSourceRef").toString());
        if (!std::holds_alternative<QByteArray>(source) ||
            digest(std::get<QByteArray>(source)) != editor_.value("sourceHash")) {
            error(QStringLiteral("SourceIntegrityFailed"));
            return;
        }
        bytes = std::get<QByteArray>(source);
    }
    QJsonObject metadata{{QStringLiteral("overlay"), overlay},
                         {QStringLiteral("_editorToken"), editor_.value("token")},
                         {QStringLiteral("_previewOnly"), preview},
                         {QStringLiteral("_forceApply"), !copy}};
    resolveImport(std::move(bytes), copy ? name : item.value("name").toString(),
                  copy ? QString{} : item.value("secretRef").toString(), copy ? QString{} : id,
                  metadata);
}
void DesktopRuntime::finishEditorPreview(const std::shared_ptr<ConfigTransaction>& context) {
    if (editor_.value("token") == context->editorToken) {
        editor_.insert(QStringLiteral("preview"), infra::redactedConfig(context->semantic));
        editor_.insert(QStringLiteral("previewDigest"),
                       digest(editor_.value("text").toString().toUtf8()));
        editor_.insert(QStringLiteral("previewPreferences"), runtimePreferences_);
        QStringList fields;
        const auto overlay = context->imported.value("overlay").toObject();
        const auto values = overlay.value("set").toObject();
        for (auto it = values.begin(); it != values.end(); ++it)
            fields.append(it.key());
        for (const auto& key : overlay.value("remove").toArray())
            fields.append(key.toString());
        if (overlay.contains("rules"))
            fields.append(QStringLiteral("rules"));
        editor_.insert(QStringLiteral("overlayFields"), QJsonArray::fromStringList(fields));
        const auto previewChecked = infra::preflightConfig(context->semantic, preferences());
        if (const auto* effective = std::get_if<infra::PreflightConfig>(&previewChecked)) {
            QStringList overridden;
            for (const auto& field : effective->overriddenFields)
                overridden.append(field);
            editor_.insert(QStringLiteral("reservedFields"),
                           QJsonArray::fromStringList(overridden));
        }
        const auto item = profile(editor_.value("id").toString());
        const auto bytes = store_.readBytes(
            item.value(item.contains("snapshotRef") ? "snapshotRef" : "sourceRef").toString());
        if (const auto* source = std::get_if<QByteArray>(&bytes)) {
            const auto composed = infra::composeOverlay(*source, item.value("overlay").toObject());
            if (const auto* value = std::get_if<QByteArray>(&composed)) {
                const auto checked = infra::preflightConfig(*value, preferences());
                if (const auto* effective = std::get_if<infra::PreflightConfig>(&checked))
                    editor_.insert(QStringLiteral("previousPreview"),
                                   infra::redactedConfig(effective->semanticYaml));
            }
        }
    }
    configTransaction_.reset();
    context->semantic = {};
    busy_ = false;
    publish();
}
void DesktopRuntime::restoreProfileVersion(const QString& id, int index, const QString& base) {
    if (busy_ || store_.readOnly())
        return;
    const auto current = profile(id);
    auto history = current.value("history").toArray();
    if (index < 0 || index >= history.size() ||
        (!base.isEmpty() && profileRevision(current) != base)) {
        error(QStringLiteral("EditorConflict"));
        return;
    }
    const auto restored = history[index].toObject();
    const auto ref = restored.contains("snapshotRef") ? "snapshotRef" : "sourceRef";
    const auto hash = restored.contains("snapshotRef") ? "snapshotHash" : "sourceHash";
    const auto bytes = store_.readBytes(restored.value(ref).toString());
    if (!std::holds_alternative<QByteArray>(bytes) ||
        digest(std::get<QByteArray>(bytes)) != restored.value(hash) ||
        std::holds_alternative<domain::Error>(store_.verifyBytes(
            restored.value("sourceRef").toString(), restored.value("sourceHash").toString()))) {
        error(QStringLiteral("SourceIntegrityFailed"));
        return;
    }
    // Restore immutable content only; current source credentials and scheduling remain current.
    QJsonObject source{{QStringLiteral("sourceRef"), restored.value("sourceRef")},
                       {QStringLiteral("sourceHash"), restored.value("sourceHash")}};
    if (!restored.contains("snapshotRef"))
        source = {};
    else {
        source.insert(QStringLiteral("snapshotRef"), restored.value("snapshotRef"));
        source.insert(QStringLiteral("snapshotHash"), restored.value("snapshotHash"));
    }
    importBytes(
        std::get<QByteArray>(bytes), current.value("name").toString(),
        current.value("secretRef").toString(), id, source,
        {{QStringLiteral("overlay"), restored.value("overlay").toObject()},
         {QStringLiteral("providerResources"), restored.value("providerResources").toArray()},
         {QStringLiteral("managedProviders"), restored.value("managedProviders").toArray()},
         {QStringLiteral("providerMode"),
          restored.value("providerMode").toString(QStringLiteral("snapshot"))},
         {QStringLiteral("_forceApply"), true}});
}
void DesktopRuntime::editorCommand(const QString& command, const QStringList& args) {
    if (command == QStringLiteral("editor_close")) {
        if (args.value(0) == editor_.value("token") && args.size() > 1)
            saveEditorDraft(args.value(0), args.value(1));
        editor_ = {};
        publish();
        return;
    }
    if (!featureDemands_.contains(QStringLiteral("editor")))
        return;
    if (command == QStringLiteral("editor_open")) {
        openEditor(args.value(0), args.value(1), args.value(2) == QStringLiteral("confirmed"));
        return;
    }
    if (command == QStringLiteral("editor_reload")) {
        if (args.value(0) == editor_.value("token") && args.value(1) == QStringLiteral("confirmed"))
            openEditor(editor_.value("id").toString(), editor_.value("kind").toString(), true);
        return;
    }
    if (args.value(0) != editor_.value("token") || editor_.isEmpty())
        return;
    if (command == QStringLiteral("editor_save_draft")) {
        saveEditorDraft(args.value(0), args.value(1));
        return;
    }
    if (busy_ ||
        (command != QStringLiteral("editor_preview") && command != QStringLiteral("editor_apply") &&
         command != QStringLiteral("editor_copy")))
        return;
    if ((command == QStringLiteral("editor_apply") || command == QStringLiteral("editor_copy")) &&
        (args.value(3) != QStringLiteral("confirmed") ||
         editor_.value("previewDigest") != digest(args.value(1).toUtf8()) ||
         editor_.value("previewPreferences").toObject() != runtimePreferences_)) {
        error(QStringLiteral("EditorPreviewRequired"));
        return;
    }
    if (!saveEditorDraft(args.value(0), args.value(1)))
        return;
    editor_.insert(QStringLiteral("action"), command == QStringLiteral("editor_preview")
                                                 ? QStringLiteral("preview")
                                                 : QStringLiteral("apply"));
    prepareEditor(command == QStringLiteral("editor_copy"), args.value(2));
}
} // namespace quickclash::application
