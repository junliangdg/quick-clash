#include "presentation/settings_session.h"
#include <QUrl>
#include <QUuid>
#include <cmath>

namespace quickclash::presentation {
namespace {
QVariantMap networkValues(const QVariantMap& source) {
    return {{"mode", source.value("mode", "rule").toString()},
            {"mixedPort", source.value("mixedPort", 7890).toString()},
            {"controllerPort", source.value("controllerPort", 9090).toString()},
            {"dnsMode", source.value("dnsMode", "profile").toString()}};
}
bool validPort(const QString& value) {
    if (value.isEmpty() || value.size() > 5)
        return false;
    for (const auto c : value) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    }
    return value.toInt() >= 1024 && value.toInt() <= 65535;
}
QString requestId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace
bool SettingsSession::networkDirty() const {
    return network_ != baseline_;
}
bool SettingsSession::targetDirty() const {
    return target_ != baseTarget_;
}
bool SettingsSession::hasDraft() const {
    return (networkDirty() && networkRequest_.isEmpty()) ||
           (targetDirty() && targetRequest_.isEmpty());
}
bool SettingsSession::networkValid() const {
    return validPort(network_.value("mixedPort").toString()) &&
           validPort(network_.value("controllerPort").toString()) &&
           network_.value("mixedPort").toInt() != network_.value("controllerPort").toInt() &&
           QStringList{"rule", "global", "direct"}.contains(network_.value("mode").toString()) &&
           QStringList{"profile", "fake-ip", "redir-host"}.contains(
               network_.value("dnsMode").toString());
}
bool SettingsSession::targetValid() const {
    const auto text = target_.trimmed();
    const QUrl url(text);
    return text.size() <= 8192 && url.isValid() && url.scheme() == QStringLiteral("https") &&
           !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment();
}
QString SettingsSession::identity() const {
    return runtime_.value("selectedId").toString() + QLatin1Char('\n') +
           runtime_.value("selectionGeneration", "0").toString();
}
QVariantMap SettingsSession::snapshot() const {
    return {{"category", category_},
            {"network", network_},
            {"baseline", baseline_},
            {"networkDirty", networkDirty()},
            {"networkValid", networkValid()},
            {"networkStale", initialized_ && (baseline_ != saved_ || baseIdentity_ != identity())},
            {"networkPending", !networkRequest_.isEmpty()},
            {"networkResult", networkResult_},
            {"target", target_},
            {"targetDirty", targetDirty()},
            {"targetValid", targetValid()},
            {"targetStale", initialized_ && baseTarget_ != savedTarget_},
            {"targetPending", !targetRequest_.isEmpty()},
            {"targetResult", targetResult_},
            {"hasDraft", hasDraft()}};
}
bool SettingsSession::observe(const QVariantMap& runtime) {
    const auto before = snapshot();
    const bool dirtyNetwork = networkDirty();
    const bool dirtyTarget = targetDirty();
    runtime_ = {{"selectedId", runtime.value("selectedId")},
                {"selectionGeneration", runtime.value("selectionGeneration", "0")},
                {"epoch", runtime.value("epoch")},
                {"busy", runtime.value("busy")},
                {"readOnly", runtime.value("readOnly")},
                {"features",
                 QVariantMap{{"delayTest", runtime.value("features").toMap().value("delayTest")}}}};
    const auto state = runtime.value("settingsState").toMap();
    saved_ = networkValues(state.contains("saved") ? state.value("saved").toMap()
                                                   : runtime.value("preferences").toMap());
    savedTarget_ = runtime.value("delayTarget", "https://www.gstatic.com/generate_204").toString();
    const auto features = runtime.value("features").toMap();
    const auto receipt = features.value("settingsNetworkRequest").toMap();
    if (!networkRequest_.isEmpty() && receipt.value("id").toString() == networkRequest_ &&
        (receipt.value("state") == "Rejected" ||
         (!runtime.value("busy").toBool() && !state.value("pending").toBool()))) {
        const bool success = receipt.value("state") != "Rejected" && saved_ == submitted_;
        networkRequest_.clear();
        networkResult_ = success ? QStringLiteral("Saved") : QStringLiteral("Failed");
        if (success) {
            baseline_ = saved_;
            network_ = saved_;
            baseIdentity_ = identity();
        }
        submitted_.clear();
    }
    const auto targetReceipt = features.value("settingsTargetRequest").toMap();
    if (!targetRequest_.isEmpty() && targetReceipt.value("id").toString() == targetRequest_) {
        const bool success =
            targetReceipt.value("state") == "Saved" && savedTarget_ == submittedTarget_;
        targetRequest_.clear();
        targetResult_ = success ? QStringLiteral("Saved") : QStringLiteral("Failed");
        if (success) {
            baseTarget_ = savedTarget_;
            target_ = savedTarget_;
        }
        submittedTarget_.clear();
    }
    if (!initialized_ || (!dirtyNetwork && networkRequest_.isEmpty())) {
        baseline_ = saved_;
        network_ = saved_;
        baseIdentity_ = identity();
    }
    if (!initialized_ || (!dirtyTarget && targetRequest_.isEmpty())) {
        baseTarget_ = savedTarget_;
        target_ = savedTarget_;
    }
    initialized_ = true;
    return before != snapshot();
}
void SettingsSession::edit(const QString& field, const QString& value) {
    if (field == QStringLiteral("target")) {
        if (targetRequest_.isEmpty() && value.size() <= 8192) {
            target_ = value;
            targetResult_.clear();
        }
    } else if (network_.contains(field) && networkRequest_.isEmpty() && value.size() <= 16) {
        network_[field] = value;
        networkResult_.clear();
    }
}
void SettingsSession::reset(const QString& group, bool keepInput) {
    if (group == QStringLiteral("network") && networkRequest_.isEmpty()) {
        baseline_ = saved_;
        baseIdentity_ = identity();
        if (!keepInput)
            network_ = saved_;
        networkResult_.clear();
    } else if (group == QStringLiteral("target") && targetRequest_.isEmpty()) {
        baseTarget_ = savedTarget_;
        if (!keepInput)
            target_ = savedTarget_;
        targetResult_.clear();
    }
}
QStringList SettingsSession::prepareNetwork() const {
    if (!initialized_ || !networkDirty() || !networkValid() || !networkRequest_.isEmpty() ||
        baseline_ != saved_ || baseIdentity_ != identity() || runtime_.value("busy").toBool() ||
        runtime_.value("readOnly").toBool())
        return {};
    return {network_.value("mode").toString(),
            network_.value("mixedPort").toString(),
            network_.value("controllerPort").toString(),
            network_.value("dnsMode").toString(),
            runtime_.value("epoch").toString(),
            runtime_.value("selectionGeneration", "0").toString()};
}
QStringList SettingsSession::submitNetwork(const QStringList& confirmation) {
    if (confirmation.isEmpty() || confirmation != prepareNetwork())
        return {};
    networkRequest_ = requestId();
    submitted_ = network_;
    submitted_["mixedPort"] = QString::number(network_.value("mixedPort").toInt());
    submitted_["controllerPort"] = QString::number(network_.value("controllerPort").toInt());
    networkResult_.clear();
    auto args = confirmation;
    args.append(networkRequest_);
    return args;
}
QStringList SettingsSession::submitTarget() {
    if (!initialized_ || !targetDirty() || !targetValid() || !targetRequest_.isEmpty() ||
        baseTarget_ != savedTarget_ || runtime_.value("readOnly").toBool() ||
        runtime_.value("features").toMap().value("delayTest").toMap().value("state") == "Running")
        return {};
    targetRequest_ = requestId();
    submittedTarget_ = target_.trimmed();
    targetResult_.clear();
    return {submittedTarget_, targetRequest_};
}
void SettingsSession::setCategory(int category) {
    if (category >= 0 && category < 5)
        category_ = category;
}
void SettingsSession::remember(int category, double position, const QString& focus) {
    if (category < 0 || category >= 5 || !std::isfinite(position))
        return;
    positions_[static_cast<size_t>(category)] = qBound(0.0, position, 10000000.0);
    focuses_[static_cast<size_t>(category)] = focus.left(128);
}
double SettingsSession::position(int category) const {
    return category >= 0 && category < 5 ? positions_[static_cast<size_t>(category)] : 0;
}
QString SettingsSession::focus(int category) const {
    return category >= 0 && category < 5 ? focuses_[static_cast<size_t>(category)] : QString{};
}
} // namespace quickclash::presentation
