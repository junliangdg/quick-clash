#include "infra/core/pipe_http.h"
#include "infra/core/service_protocol.h"
#include "platform/windows/service_host.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <iostream>

namespace quickclash::platform {
// Exercise the real fixed service reload handler and official core under the
// current user. Authentication/SCM and actual SYSTEM/TUN are separate acceptance scopes.
class ServiceReloadProbe final {
  public:
    static int run() {
        const auto wait = [](const std::function<bool()>& condition) {
            QElapsedTimer timer;
            timer.start();
            while (!condition() && timer.elapsed() < 15000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                QThread::msleep(2);
            }
            return condition();
        };
        QTemporaryDir temporary;
        ServiceHost host;
        host.store_ = infra::JsonStore(temporary.path());
        if (!temporary.isValid() || !std::holds_alternative<std::monostate>(host.store_.open()))
            return 20;
        host.directory_ = temporary.filePath("session");
        host.pipe_ = "quickclash_reload_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        host.epoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QTcpServer reservation;
        if (!reservation.listen(QHostAddress::LocalHost))
            return 21;
        const auto port = reservation.serverPort();
        reservation.close();
        const QByteArray yaml = "mixed-port: " + QByteArray::number(port) +
                                "\nexternal-controller-pipe: '" +
                                QDir::toNativeSeparators("//./pipe/" + host.pipe_).toUtf8() +
                                "'\nallow-lan: false\nmode: rule\nproxies: []\nproxy-groups: "
                                "[]\nrules: ['MATCH,DIRECT']\n";
        if (!std::holds_alternative<std::monostate>(
                host.store_.writeBytes("session/config.yaml", yaml)) ||
            !std::holds_alternative<std::monostate>(host.core_.start(
                qEnvironmentVariable("QUICKCLASH_TEST_CORE"),
                {"-d", host.directory_, "-f", temporary.filePath("session/config.yaml")},
                host.directory_, false)))
            return 22;
        host.coreLaunched_ = true;
        bool ready = false;
        bool pending = false;
        if (!wait([&] {
                if (!ready && !pending) {
                    pending = true;
                    infra::pipeHttp(&host, host.pipe_, "GET", "/configs", {}, [&](auto result) {
                        ready = std::holds_alternative<QJsonObject>(result);
                        pending = false;
                    });
                }
                return ready;
            }))
            return 23;
        host.ready_ = true;
        host.mixedPort_ = port;
        const auto pid = host.core_.pid();
        QLocalServer server;
        if (!server.listen("quickclash_reload_peer_" + host.epoch_))
            return 24;
        QLocalSocket client;
        client.connectToServer(server.serverName());
        if (!wait([&] { return server.hasPendingConnections(); }))
            return 25;
        std::unique_ptr<QLocalSocket> peer(server.nextPendingConnection());
        host.owner_ = peer.get();
        int request = 0;
        const auto reload = [&](QJsonObject payload) {
            host.reload(peer.get(), QString::number(++request), payload);
            QByteArray bytes;
            if (!wait([&] {
                    bytes += client.readAll();
                    return bytes.contains('\n');
                }))
                return QJsonObject{{"error", "ProbeTimeout"}};
            return QJsonDocument::fromJson(bytes.left(bytes.indexOf('\n'))).object();
        };
        const QByteArray semantic = "proxies: []\nproxy-groups: []\nrules: ['MATCH,DIRECT']\n";
        QJsonObject payload{{"yaml", QString::fromLatin1(semantic.toBase64())},
                            {"mixedPort", port},
                            {"mode", "direct"},
                            {"dnsMode", "profile"},
                            {"tun", false}};
        for (int index = 0; index < 4; ++index) {
            const auto result = reload(payload);
            if (result.contains("error") || host.core_.pid() != pid ||
                !host.core_.ownsListener(port) || !host.ready_ || host.busy_) {
                std::cerr << "Service reload: " << QJsonDocument(result).toJson().constData();
                return 26;
            }
        }
        const auto revisionPath = std::get<QString>(host.store_.path("session/revisions"));
        if (QDir(revisionPath).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() > 3)
            return 27;
        auto invalid = payload;
        invalid.insert("path", "arbitrary");
        if (!reload(invalid).contains("error") || host.core_.pid() != pid)
            return 28;
        invalid = payload;
        invalid.insert("yaml", QString::fromLatin1(QByteArray("proxies: [invalid]").toBase64()));
        if (!reload(invalid).contains("error") || !host.core_.ownsListener(port))
            return 29;
        QTcpServer occupied;
        if (!occupied.listen(QHostAddress::LocalHost))
            return 30;
        auto migration = payload;
        migration.insert("mixedPort", occupied.serverPort());
        if (!reload(migration).contains("error") || reload(payload).contains("error") ||
            host.core_.pid() != pid || !host.core_.ownsListener(port))
            return 31;
        // A failed start must return bounded diagnostics before the follow-up stop,
        // including output from a service-terminated core (no finished signal).
        host.failStart(peer.get(), QString::number(++request), "ServiceTunReadbackFailed",
                       "ProbeFailed", "port mismatch password=do_not_retain");
        QByteArray failureBytes;
        if (!wait([&] {
                failureBytes += client.readAll();
                return failureBytes.contains('\n');
            }))
            return 33;
        const auto failure =
            QJsonDocument::fromJson(failureBytes.left(failureBytes.indexOf('\n'))).object();
        const auto detail = failure.value("detail").toObject();
        if (!infra::ServiceProtocol::validResponse(failure) ||
            failure.value("error") != "ServiceTunReadbackFailed" ||
            detail.value("stopReason") != "ProbeFailed" ||
            !detail.value("stopDetail").toString().contains("port mismatch") ||
            failureBytes.contains("do_not_retain") ||
            detail.value("coreStderr").toString().isEmpty() || !detail.contains("coreExitCode"))
            return 34;
        host.stop();
        if (host.coreDiagnostics().value("stopReason") != "ProbeFailed")
            return 35;
        // The next validation can time out before launching a new core. Its error must
        // not inherit the previous process's output or exit code.
        host.coreLaunched_ = false;
        host.busy_ = true;
        host.failStart(peer.get(), QString::number(++request), "ServiceStartTimeout",
                       "StartTimeout", {});
        failureBytes.clear();
        if (!wait([&] {
                failureBytes += client.readAll();
                return failureBytes.contains('\n');
            }))
            return 36;
        const auto validationFailure =
            QJsonDocument::fromJson(failureBytes.left(failureBytes.indexOf('\n'))).object();
        const auto validationDetail = validationFailure.value("detail").toObject();
        if (!infra::ServiceProtocol::validResponse(validationFailure) ||
            validationDetail.value("stopReason") != "StartTimeout" ||
            validationDetail.contains("coreStderr") || validationDetail.contains("coreExitCode"))
            return 37;
        host.owner_.clear();
        if (host.core_.running() || !host.configClean_ ||
            !std::get<QStringList>(host.store_.files("session/revisions")).isEmpty())
            return 32;
        std::cout << "Service fixed reload: same PID, preflight, occupied-port recovery and "
                     "bounded cache passed\n";
        return 0;
    }
};
} // namespace quickclash::platform
int serviceReloadProbe() {
    return quickclash::platform::ServiceReloadProbe::run();
}
