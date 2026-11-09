#include "config_editor_probe.h"
#include "application/desktop_runtime.h"
#include "listener_fixture.h"
#include "network_fixture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

namespace quickclash::test {
namespace {
bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
int port() {
    return quickclash::test::availableListenerPort();
}
} // namespace
int configEditorProbe() {
    QTemporaryDir directory;
    infra::JsonStore store(directory.path());
    if (!std::holds_alternative<std::monostate>(store.open()))
        return 1;
    application::DesktopRuntime runtime(store);
    runtime.initialize();
    const auto check = [](bool result, const char* message) {
        if (!result)
            std::fprintf(stderr, "Editor assertion: %s\n", message);
        return result;
    };
    const auto done = [&] {
        return waitFor([&] { return !runtime.snapshot().value("busy").toBool(); });
    };
    const auto profile = [&] {
        return runtime.snapshot().value("profiles").toArray().first().toObject();
    };
    QJsonObject editor;
    QObject::connect(&runtime, &application::DesktopRuntime::dataPublished,
                     [&editor](const application::RuntimeUpdate& update) {
                         if (update.values.contains("editor"))
                             editor = update.values.value("editor").toObject();
                     });
    const auto editorReady = [&] {
        return waitFor([&] { return !editor.value("token").toString().isEmpty(); });
    };
    auto mixed = port(), controller = port();
    while (mixed == controller)
        controller = port();
    runtime.setPorts(mixed, controller);
    TlsFixture origin;
    if (!origin.initialize())
        return 2;
    origin.subscription =
        "# public comment stays byte exact\nproxies:\n"
        "- {name: fixture, type: http, server: 127.0.0.1, port: 18080, username: public_user, "
        "password: public_secret}\n"
        "proxy-groups:\n- {name: manual, type: select, proxies: [fixture, DIRECT]}\n"
        "rules:\n- MATCH,manual\n";
    const auto original = origin.subscription;
    runtime.addSubscription(
        QStringLiteral("https://127.0.0.1:%1/subscription").arg(origin.serverPort()),
        "Edit fixture");
    if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 1,
               "subscription imported"))
        return 3;
    const auto id = profile().value("id").toString();
    runtime.configureSubscription(id, 0, false);
    runtime.setDemands({"editor"});
    runtime.featureCommand("editor_open", {id, "source"});
    if (!check(editorReady() && editor.value("text").toString().toUtf8() == original,
               "source comment preserved"))
        return 4;
    const auto token = editor.value("token").toString();
    const auto draft = QString::fromUtf8(original) + "\n# private draft comment\n";
    runtime.featureCommand("editor_preview", {token, draft});
    if (!check(done() && waitFor([&] { return editor.contains("previewDigest"); }) &&
                   !editor.value("preview").toString().contains("public_secret") &&
                   !editor.value("preview").toString().contains("public_user"),
               "official preview redacts credentials"))
        return 5;
    const auto before = profile().value("sourceHash");
    if (!check(std::get<QByteArray>(store.readBytes(profile().value("sourceRef").toString())) ==
                   original,
               "preview leaves raw source unchanged"))
        return 6;
    origin.subscription = original + "\nlog-level: warning\n";
    runtime.updateProfile(id);
    if (!check(done() && profile().value("sourceHash") != before,
               "new subscription candidate saved"))
        return 7;
    const auto newer = profile().value("sourceHash");
    runtime.featureCommand("editor_apply", {token, draft, "", "confirmed"});
    if (!check(runtime.snapshot().value("error") == "EditorConflict" &&
                   profile().value("sourceHash") == newer,
               "stale draft cannot replace newer subscription"))
        return 8;
    runtime.featureCommand("editor_close", {});
    editor = {};
    runtime.featureCommand("editor_open", {id, "source"});
    if (!check(editorReady() && editor.value("text") == draft && editor.value("conflict").toBool(),
               "draft survives update and reopen"))
        return 9;
    const auto nextToken = editor.value("token").toString();
    runtime.featureCommand("editor_preview", {nextToken, draft});
    if (!check(done() && waitFor([&] { return editor.contains("previewDigest"); }),
               "conflicted draft can be compared"))
        return 10;
    runtime.featureCommand("editor_copy", {nextToken, draft, "Local copy", "confirmed"});
    if (!check(done() && runtime.snapshot().value("profiles").toArray().size() == 2 &&
                   profile().value("sourceHash") == newer,
               "copy leaves subscription current and creates offline profile"))
        return 11;
    runtime.featureCommand("editor_reload", {nextToken, "confirmed"});
    if (!check(waitFor([&] { return editor.value("token") != nextToken; }) &&
                   !editor.value("conflict").toBool(),
               "reload requires explicit discard"))
        return 12;
    editor = {};
    runtime.featureCommand("editor_open", {id, "overlay"});
    if (!check(editorReady(), "overlay editor opened"))
        return 13;
    const auto overlayToken = editor.value("token").toString();
    const auto overlay =
        QStringLiteral("{\"set\":{\"log-level\":\"debug\"},\"rules\":{\"mode\":\"prepend\","
                       "\"items\":[\"DOMAIN,extra.invalid,DIRECT\"]}}");
    runtime.start(false);
    if (!check(done() && runtime.snapshot().value("ready").toBool(), "editor live session ready"))
        return 25;
    const auto originalPid = runtime.snapshot().value("corePid");
    runtime.featureCommand("editor_apply", {overlayToken, overlay, "", "confirmed"});
    if (!check(runtime.snapshot().value("error") == "EditorPreviewRequired",
               "apply requires matching reviewed preview"))
        return 14;
    runtime.featureCommand("editor_preview", {overlayToken, overlay});
    if (!check(done() && waitFor([&] { return editor.contains("previewDigest"); }),
               "overlay validation completes"))
        return 15;
    runtime.featureCommand("editor_apply", {overlayToken, overlay, "", "confirmed"});
    if (!check(done() && profile().value("sourceHash") == newer &&
                   !profile().value("pendingApply").toBool() &&
                   runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("corePid") == originalPid,
               "overlay applied without changing raw source"))
        return 16;
    const auto catalog = *std::get<std::optional<QJsonObject>>(store.read("data/catalog.json"));
    if (!check(!catalog.value("profiles")
                       .toArray()
                       .first()
                       .toObject()
                       .value("overlay")
                       .toObject()
                       .isEmpty() &&
                   !profile().contains("overlay"),
               "overlay stays private outside explicit editor"))
        return 17;
    runtime.previewConfiguration();
    if (!check(runtime.snapshot().value("effectivePreview").toString().contains("extra.invalid"),
               "selected effective config includes overrides"))
        return 18;
    const auto manifest =
        *std::get<std::optional<QJsonObject>>(store.read("data/runtime/current.json"));
    const auto sourceHash = profile().value("sourceHash");
    runtime.featureCommand("editor_preview",
                           {overlayToken, "{\"set\":{\"tun\":{\"enable\":true}}}"});
    if (!check(!runtime.snapshot().value("busy").toBool() &&
                   runtime.snapshot().value("error") == "InvalidOverlay" &&
                   *std::get<std::optional<QJsonObject>>(store.read("data/runtime/current.json")) ==
                       manifest &&
                   profile().value("sourceHash") == sourceHash,
               "bad overlay keeps manifest and raw source"))
        return 19;
    const auto history = profile().value("history").toArray();
    if (!check(history.size() >= 2, "source and overlay history retained"))
        return 20;
    runtime.featureCommand("rollback_profile",
                           {id, "confirmed", "1", profile().value("revision").toString()});
    if (!check(done() && profile().value("sourceHash") == before,
               "chosen older history validates and restores"))
        return 21;
    editor = {};
    runtime.featureCommand("editor_open", {id, "overlay", "confirmed"});
    if (!check(editorReady(), "fresh editor after history restore"))
        return 26;
    const auto failureToken = editor.value("token").toString();
    const auto failureDraft = QStringLiteral(
        "{\"rules\":{\"mode\":\"prepend\",\"items\":[\"DOMAIN,failure.invalid,DIRECT\"]}}");
    runtime.featureCommand("editor_preview", {failureToken, failureDraft});
    if (!check(done() && waitFor([&] { return editor.contains("previewDigest"); }),
               "candidate failure preview completes"))
        return 27;
    const auto confirmed = runtime.snapshot().value("selectionGeneration");
    bool blocked = false;
    const auto failurePid = runtime.snapshot().value("corePid");
    const auto beforeReloadFiles = std::get<QStringList>(store.files("data/sessions"));
    const auto failureConnection = QObject::connect(
        &runtime, &application::DesktopRuntime::changed, &runtime, [&](const QJsonObject& state) {
            if (state.value("status") != "Reloading" || blocked)
                return;
            const auto files = store.files("data/sessions/" + state.value("epoch").toString());
            if (const auto* listed = std::get_if<QStringList>(&files))
                for (const auto& file : *listed)
                    if (!beforeReloadFiles.contains(file) && file.contains("/reload_"))
                        blocked = std::holds_alternative<std::monostate>(
                            store.writeBytes(file, "proxies: [invalid]"));
        });
    runtime.featureCommand("editor_apply", {failureToken, failureDraft, "", "confirmed"});
    if (!check(done() && blocked && runtime.snapshot().value("ready").toBool() &&
                   runtime.snapshot().value("selectionGeneration") == confirmed &&
                   runtime.snapshot().value("corePid") == failurePid &&
                   profile().value("pendingApply").toBool(),
               "reload failure preserves confirmed selection, process and candidate")) {
        std::fprintf(stderr,
                     "failure state blocked=%d ready=%d sameSelection=%d pending=%d error=%s\n",
                     blocked, runtime.snapshot().value("ready").toBool(),
                     runtime.snapshot().value("selectionGeneration") == confirmed,
                     profile().value("pendingApply").toBool(),
                     qPrintable(runtime.snapshot().value("error").toString()));
        return 28;
    }
    QObject::disconnect(failureConnection);
    const auto old = profile().value("history").toArray().first().toObject();
    const auto damaged = store.writeBytes(old.value("sourceRef").toString(), "bad: [");
    if (!check(std::holds_alternative<std::monostate>(damaged), "damage fixture prepared"))
        return 22;
    const auto preserved = profile().value("sourceHash");
    runtime.featureCommand("rollback_profile", {id, "confirmed"});
    if (!check(runtime.snapshot().value("error") == "SourceIntegrityFailed" &&
                   profile().value("sourceHash") == preserved,
               "corrupt retained source cannot replace catalog"))
        return 23;
    runtime.setDemands({});
    runtime.setDemands({"editor"});
    editor = {};
    runtime.featureCommand("editor_apply", {overlayToken, overlay, "", "confirmed"});
    if (!check(!runtime.snapshot().value("busy").toBool() &&
                   profile().value("sourceHash") == preserved,
               "release invalidates editor token"))
        return 24;
    runtime.stop();
    application::DesktopRuntime reopened(store);
    reopened.initialize();
    QJsonObject reopenedEditor;
    QObject::connect(&reopened, &application::DesktopRuntime::dataPublished,
                     [&reopenedEditor](const application::RuntimeUpdate& update) {
                         if (update.values.contains("editor"))
                             reopenedEditor = update.values.value("editor").toObject();
                     });
    reopened.setDemands({"editor"});
    reopened.featureCommand("editor_open", {id, "overlay"});
    if (!check(waitFor([&] { return reopenedEditor.value("text") == failureDraft; }),
               "failed candidate draft survives runtime reconstruction"))
        return 29;
    std::puts("Editor checks: raw comments, official redaction, subscription conflict, persistent "
              "draft, copy, overlay, selected history, corrupt history and release passed");
    return 0;
}
} // namespace quickclash::test
