#include "presentation/settings_session.h"
#include <QTest>
#include <limits>
using quickclash::presentation::SettingsSession;

namespace {
QVariantMap runtime() {
    return {{"epoch", "session-a"},
            {"selectedId", "profile-a"},
            {"selectionGeneration", "4"},
            {"busy", false},
            {"readOnly", false},
            {"settingsState", QVariantMap{{"saved", QVariantMap{{"mode", "rule"},
                                                                {"mixedPort", 7890},
                                                                {"controllerPort", 9090},
                                                                {"dnsMode", "profile"}}},
                                          {"pending", false}}},
            {"delayTarget", "https://example.org/check"}};
}
void receipt(QVariantMap& state, const QString& kind, const QString& id, const QString& phase) {
    state["features"] = QVariantMap{{kind, QVariantMap{{"id", id}, {"state", phase}}}};
}
} // namespace
class SettingsSessionTest : public QObject {
    Q_OBJECT
  private slots:
    void editsSurviveNavigationAndRefresh() {
        SettingsSession session;
        auto state = runtime();
        session.observe(state);
        session.edit("mixedPort", "8765");
        session.edit("target", "https://example.org/other");
        session.setCategory(4);
        session.remember(1, 700, "mixed_port");
        state["status"] = "Ready";
        session.observe(state);
        QCOMPARE(session.snapshot().value("network").toMap().value("mixedPort"), "8765");
        QCOMPARE(session.snapshot().value("target"), "https://example.org/other");
        QCOMPARE(session.snapshot().value("category"), 4);
        QCOMPARE(session.position(1), 700.0);
        QCOMPARE(session.focus(1), "mixed_port");
        QVERIFY(session.hasDraft());
    }
    void validatesPortsAndTarget() {
        SettingsSession session;
        session.observe(runtime());
        for (const auto& port : {"", "80", "65536", "1e4", " 7891", "abc", "9090"}) {
            session.edit("mixedPort", port);
            QVERIFY(!session.snapshot().value("networkValid").toBool());
            QVERIFY(session.prepareNetwork().isEmpty());
        }
        session.edit("mixedPort", "12345");
        QVERIFY(session.snapshot().value("networkValid").toBool());
        for (const auto& target : {"", "http://example.org", "https://user:pass@example.org",
                                   "https://example.org/#secret"}) {
            session.edit("target", target);
            QVERIFY(!session.snapshot().value("targetValid").toBool());
        }
        session.edit("target", "https://example.org/ok");
        QVERIFY(session.snapshot().value("targetValid").toBool());
    }
    void staleBaselinePreservesInputAndRequiresReview() {
        SettingsSession session;
        auto state = runtime();
        session.observe(state);
        session.edit("mixedPort", "8765");
        const auto confirmation = session.prepareNetwork();
        state["selectionGeneration"] = "5";
        session.observe(state);
        QVERIFY(session.snapshot().value("networkStale").toBool());
        QVERIFY(session.submitNetwork(confirmation).isEmpty());
        session.reset("network", true);
        QCOMPARE(session.snapshot().value("network").toMap().value("mixedPort"), "8765");
        QVERIFY(!session.prepareNetwork().isEmpty());
        session.reset("network");
        QVERIFY(!session.hasDraft());
    }
    void epochChangeInvalidatesConfirmationWithoutDiscardingInput() {
        SettingsSession session;
        auto state = runtime();
        session.observe(state);
        session.edit("mixedPort", "8765");
        const auto confirmation = session.prepareNetwork();
        state["epoch"] = "session-b";
        session.observe(state);
        QVERIFY(session.submitNetwork(confirmation).isEmpty());
        QVERIFY(!session.snapshot().value("networkStale").toBool());
        QVERIFY(!session.prepareNetwork().isEmpty());
    }
    void matchedReceiptCoordinatesOnlySubmittedChanges() {
        SettingsSession session;
        auto state = runtime();
        session.observe(state);
        session.edit("mixedPort", "08765");
        session.edit("target", "https://example.org/unsaved");
        const auto args = session.submitNetwork(session.prepareNetwork());
        QCOMPARE(args.size(), 7);
        QVERIFY(session.submitNetwork(args).isEmpty());
        session.edit("mixedPort", "9999"); // Frozen during this transaction.
        receipt(state, "settingsNetworkRequest", "another-request", "Rejected");
        session.observe(state);
        QVERIFY(session.snapshot().value("networkPending").toBool());
        state["busy"] = true;
        receipt(state, "settingsNetworkRequest", args.last(), "Accepted");
        session.observe(state);
        QVERIFY(session.snapshot().value("networkPending").toBool());
        auto settings = state.value("settingsState").toMap();
        auto saved = settings.value("saved").toMap();
        saved["mixedPort"] = 8765;
        settings["saved"] = saved;
        state["settingsState"] = settings;
        state["busy"] = false;
        session.observe(state);
        QVERIFY(!session.snapshot().value("networkPending").toBool());
        QVERIFY(!session.snapshot().value("networkDirty").toBool());
        QVERIFY(session.snapshot().value("targetDirty").toBool());
        QCOMPARE(session.snapshot().value("networkResult"), "Saved");
    }
    void rejectedAndFailedRequestsKeepDraft() {
        for (const auto phase : {"Rejected", "Accepted"}) {
            SettingsSession session;
            auto state = runtime();
            session.observe(state);
            session.edit("mixedPort", "8765");
            const auto args = session.submitNetwork(session.prepareNetwork());
            receipt(state, "settingsNetworkRequest", args.last(), phase);
            session.observe(state);
            QVERIFY(!session.snapshot().value("networkPending").toBool());
            QCOMPARE(session.snapshot().value("networkResult"), "Failed");
            QCOMPARE(session.snapshot().value("network").toMap().value("mixedPort"), "8765");
        }
    }
    void targetSuccessAndFailureDoNotResetNetwork() {
        for (const auto phase : {"Failed", "Saved"}) {
            SettingsSession session;
            auto state = runtime();
            session.observe(state);
            session.edit("mixedPort", "8765");
            session.edit("target", " https://example.org/new ");
            const auto args = session.submitTarget();
            QCOMPARE(args.size(), 2);
            if (QString::fromLatin1(phase) == "Saved")
                state["delayTarget"] = args.first();
            receipt(state, "settingsTargetRequest", args.last(), phase);
            session.observe(state);
            QVERIFY(!session.snapshot().value("targetPending").toBool());
            QCOMPARE(session.snapshot().value("targetDirty").toBool(),
                     QString::fromLatin1(phase) != "Saved");
            QVERIFY(session.snapshot().value("networkDirty").toBool());
        }
    }
    void viewStateAndInputsAreBounded() {
        SettingsSession session;
        session.observe(runtime());
        session.setCategory(999);
        QCOMPARE(session.snapshot().value("category"), 0);
        session.remember(1, std::numeric_limits<double>::infinity(), "invalid");
        QCOMPARE(session.position(1), 0.0);
        session.remember(1, -10, QString(1000, 'a'));
        QCOMPARE(session.position(1), 0.0);
        QCOMPARE(session.focus(1).size(), 128);
        session.edit("target", QString(8193, 'x'));
        QVERIFY(!session.hasDraft());
    }
};
QTEST_GUILESS_MAIN(SettingsSessionTest)
#include "settings_session_test.moc"
