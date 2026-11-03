#include "application/desktop_runtime.h"
#include "infra/config/config_overlay.h"
#include "infra/config/custom_groups.h"
#include <QCryptographicHash>
#include <QJsonDocument>

namespace quickclash::application {
namespace {
QString digest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QJsonObject publicView(QJsonObject view) {
    auto groups = view.value("groups").toArray();
    for (qsizetype index = 0; index < groups.size(); ++index) {
        auto group = groups[index].toObject();
        group.remove("sourceNames");
        groups[index] = group;
    }
    view.insert("groups", groups);
    return view;
}
} // namespace
void DesktopRuntime::refreshCustomGroupContext() {
    if (!featureDemands_.contains(QStringLiteral("custom_groups")))
        return;
    const auto item = profile(selectedId_);
    const auto revision = profileRevision(item);
    if (customGroupState_.value("revision") == revision &&
        customGroupState_.value("profileId") == selectedId_)
        return;
    customGroupDraft_ = {};
    customGroupState_ = {{"profileId", selectedId_}, {"revision", revision}};
    if (item.isEmpty())
        return;
    const auto source = store_.readBytes(
        item.value(item.contains("snapshotRef") ? "snapshotRef" : "sourceRef").toString());
    if (!std::holds_alternative<QByteArray>(source) ||
        digest(std::get<QByteArray>(source)) !=
            item.value(item.contains("snapshotRef") ? "snapshotHash" : "sourceHash")) {
        customGroupState_.insert("error", "SourceIntegrityFailed");
        return;
    }
    const auto overlaid =
        infra::composeOverlay(std::get<QByteArray>(source), item.value("overlay").toObject());
    if (!std::holds_alternative<QByteArray>(overlaid)) {
        customGroupState_.insert("error", std::get<domain::Error>(overlaid).code);
        return;
    }
    const auto composed =
        infra::composeCustomGroups(std::get<QByteArray>(overlaid),
                                   item.value("customGroups").toArray(), customGroupLiveProviders_);
    if (const auto* failure = std::get_if<domain::Error>(&composed)) {
        customGroupState_.insert("error", failure->code);
        return;
    }
    const auto view = publicView(std::get<infra::CustomGroupConfig>(composed).view);
    for (auto it = view.begin(); it != view.end(); ++it)
        customGroupState_.insert(it.key(), it.value());
}
void DesktopRuntime::customGroupCommand(const QString& command, const QStringList& args) {
    if (!featureDemands_.contains(QStringLiteral("custom_groups")))
        return;
    if (command == QStringLiteral("custom_group_clear")) {
        customGroupDraft_ = {};
        customGroupState_.remove("preview");
        publish();
        return;
    }
    if (busy_ || store_.readOnly())
        return;
    refreshCustomGroupContext();
    const auto item = profile(selectedId_);
    if (item.isEmpty()) {
        error(QStringLiteral("ProfileNotFound"));
        return;
    }
    QJsonArray definitions;
    bool preview = command == QStringLiteral("custom_group_preview");
    if (preview) {
        customGroupDraft_ = {};
        customGroupState_.remove("preview");
        if (args.value(0) != profileRevision(item)) {
            error(QStringLiteral("CustomGroupConflict"));
            return;
        }
        const auto document = QJsonDocument::fromJson(args.value(1).toUtf8());
        if (!document.isObject() || args.value(1).toUtf8().size() > 256 * 1024 ||
            (args.value(2) != QStringLiteral("save") &&
             args.value(2) != QStringLiteral("delete"))) {
            error(QStringLiteral("InvalidCustomGroup"));
            return;
        }
        auto definition = document.object();
        auto id = definition.value("id").toString();
        if (id.isEmpty()) {
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            definition.insert("id", id);
        }
        bool found = false;
        const auto operation = args.value(2);
        for (const auto& entry : item.value("customGroups").toArray()) {
            if (entry.toObject().value("id") == id) {
                found = true;
                if (operation == QStringLiteral("save"))
                    definitions.append(definition);
                else {
                    const auto oldName = entry.toObject().value("name").toString();
                    for (const auto& group : groups_)
                        if (group.toObject().value("current") == oldName) {
                            error(QStringLiteral("CustomGroupInUse"));
                            return;
                        }
                }
            } else {
                definitions.append(entry);
            }
        }
        if (!found) {
            if (operation == QStringLiteral("delete") ||
                !document.object().value("id").toString().isEmpty()) {
                error(QStringLiteral("CustomGroupConflict"));
                return;
            }
            definitions.append(definition);
        }
        customGroupDraft_ = {{"token", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                             {"profileId", selectedId_},
                             {"revision", profileRevision(item)},
                             {"preferences", runtimePreferences_},
                             {"definitions", definitions},
                             {"target", id},
                             {"operation", operation},
                             {"activate", args.value(3) == QStringLiteral("true")},
                             {"parent", definition.value("parent")},
                             {"member", definition.value("name")}};
    } else if (command == QStringLiteral("custom_group_apply")) {
        if (args.value(1) != QStringLiteral("confirmed") ||
            customGroupDraft_.value("token").toString().isEmpty() ||
            args.value(0) != customGroupDraft_.value("token") ||
            customGroupDraft_.value("validated") != true ||
            customGroupDraft_.value("profileId") != selectedId_ ||
            customGroupDraft_.value("revision") != profileRevision(item) ||
            customGroupDraft_.value("preferences").toObject() != runtimePreferences_) {
            error(QStringLiteral("CustomGroupPreviewRequired"));
            return;
        }
        definitions = customGroupDraft_.value("definitions").toArray();
    } else {
        return;
    }
    const auto ref = item.contains("snapshotRef") ? "snapshotRef" : "sourceRef";
    const auto checksum = item.contains("snapshotRef") ? "snapshotHash" : "sourceHash";
    const auto bytes = store_.readBytes(item.value(QLatin1String(ref)).toString());
    if (!std::holds_alternative<QByteArray>(bytes) ||
        digest(std::get<QByteArray>(bytes)) != item.value(QLatin1String(checksum)) ||
        std::holds_alternative<domain::Error>(store_.verifyBytes(
            item.value("sourceRef").toString(), item.value("sourceHash").toString()))) {
        error(QStringLiteral("SourceIntegrityFailed"));
        return;
    }
    QJsonObject metadata{{"customGroups", definitions},
                         {"_previewOnly", preview},
                         {"_forceApply", !preview},
                         {"_customGroupToken", customGroupDraft_.value("token")}};
    if (!preview) {
        customGroupApplying_ = true;
        customGroupActivation_ = customGroupDraft_;
        customGroupState_.insert("result", "Applying");
    }
    lastError_.clear();
    importBytes(std::get<QByteArray>(bytes), item.value("name").toString(),
                item.value("secretRef").toString(), selectedId_, item, metadata);
}
void DesktopRuntime::finishCustomGroupPreview(const std::shared_ptr<ConfigTransaction>& context) {
    if (customGroupDraft_.value("token") == context->customGroupToken &&
        featureDemands_.contains(QStringLiteral("custom_groups"))) {
        const auto& item = context->imported;
        const auto bytes = store_.readBytes(
            item.value(item.contains("snapshotRef") ? "snapshotRef" : "sourceRef").toString());
        if (const auto* input = std::get_if<QByteArray>(&bytes)) {
            const auto overlay = infra::composeOverlay(*input, item.value("overlay").toObject());
            if (const auto* source = std::get_if<QByteArray>(&overlay)) {
                const auto composed = infra::composeCustomGroups(
                    *source, item.value("customGroups").toArray(), customGroupLiveProviders_);
                if (const auto* groups = std::get_if<infra::CustomGroupConfig>(&composed)) {
                    bool empty = false;
                    for (const auto& value : groups->view.value("groups").toArray()) {
                        const auto group = value.toObject();
                        if (group.value("id") == customGroupDraft_.value("target"))
                            empty = group.value("count").toInt() == 0;
                    }
                    auto preview = publicView(groups->view);
                    preview.insert("token", context->customGroupToken);
                    preview.insert("target", customGroupDraft_.value("target"));
                    preview.insert("empty", empty);
                    preview.insert("operation", customGroupDraft_.value("operation"));
                    preview.insert("validated", !empty);
                    customGroupState_.insert("preview", preview);
                    customGroupDraft_.insert("validated", !empty);
                }
            }
        }
    }
    configTransaction_.reset();
    context->semantic = {};
    busy_ = false;
    publish();
}
void DesktopRuntime::finishCustomGroupOperation() {
    if (!customGroupApplying_ || busy_)
        return;
    customGroupApplying_ = false;
    auto intent = customGroupActivation_;
    customGroupActivation_ = {};
    customGroupDraft_ = {};
    customGroupState_ = {};
    refreshCustomGroupContext();
    customGroupState_.insert("result", lastError_.isEmpty() ? "Saved" : "Failed");
    if (!lastError_.isEmpty() || intent.value("profileId") != selectedId_ ||
        intent.value("activate") != true || intent.value("operation") != QStringLiteral("save") ||
        intent.value("parent").toString().isEmpty())
        return;
    // Selection is a separate confirmed operation, after configuration commit and restoration.
    QTimer::singleShot(0, this, [this, intent] {
        if (!busy_ && lastError_.isEmpty() && intent.value("profileId") == selectedId_ &&
            profile(selectedId_).value("customGroups") == intent.value("definitions"))
            selectProxy(intent.value("parent").toString(), intent.value("member").toString());
    });
}
} // namespace quickclash::application
