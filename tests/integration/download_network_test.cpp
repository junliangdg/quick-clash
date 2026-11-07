#include "infra/network/download_network.h"
#include "infra/network/network_relay.h"
#include "network_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QSslConfiguration>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
bool waitFor(const std::function<bool()>& condition, int milliseconds = 15000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() == 2 &&
        application.arguments()[1].startsWith(QStringLiteral("--relay-owner="))) {
        quickclash::infra::NetworkRelay owned;
        const auto path = application.arguments()[1].mid(14);
        QObject::connect(&owned, &quickclash::infra::NetworkRelay::available, &application, [&] {
            QSaveFile result(path);
            const auto bytes = QJsonDocument(owned.memory()).toJson();
            if (!result.open(QIODevice::WriteOnly) || result.write(bytes) != bytes.size() ||
                !result.commit())
                application.exit(20);
        });
        QObject::connect(&owned, &quickclash::infra::NetworkRelay::failed, &application,
                         [&] { application.exit(21); });
        QTimer::singleShot(15000, &application, [&] { application.exit(22); });
        owned.acquire();
        return application.exec();
    }
    quickclash::test::TlsFixture origin;
    if (!origin.initialize())
        return 1;
    origin.payloads.insert("/public-body", QByteArray(16 * 1024 * 1024 + 1, 'a'));
    quickclash::infra::NetworkRelay relay;
    quickclash::infra::DownloadNetwork network(relay);
    network.setProxy(QNetworkProxy::NoProxy);
    const auto request = [&](const char* path) {
        QNetworkRequest value(QUrl(QStringLiteral("https://127.0.0.1:%1%2")
                                       .arg(origin.serverPort())
                                       .arg(QString::fromLatin1(path))));
        value.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                           QNetworkRequest::ManualRedirectPolicy);
        value.setTransferTimeout(5000);
        return value;
    };
    auto* first = network.get(request("/public-body"));
    first->setReadBufferSize(4096);
    QByteArray body;
    QObject::connect(first, &QIODevice::readyRead, &application, [&] { body += first->readAll(); });
    if (!waitFor([&] { return first->isFinished(); }) || first->error() != QNetworkReply::NoError ||
        first->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
        body != origin.payloads.value("/public-body") ||
        first->sslConfiguration().peerCertificate().isNull()) {
        std::fprintf(stderr,
                     "stream error=%d status=%d received=%lld expected=%lld available=%lld\n",
                     static_cast<int>(first->error()),
                     first->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                     static_cast<long long>(body.size()),
                     static_cast<long long>(origin.payloads.value("/public-body").size()),
                     static_cast<long long>(first->bytesAvailable()));
        return 2;
    }
    first->deleteLater();
    if (!waitFor([&] { return !relay.running(); }, 4000))
        return 3;
    origin.redirects.insert("/public-redirect", "/public-body");
    auto* redirected = network.get(request("/public-redirect"));
    if (!waitFor([&] { return redirected->isFinished(); }) ||
        redirected->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 302 ||
        redirected->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl() !=
            QUrl(QStringLiteral("/public-body")))
        return 4;
    redirected->deleteLater();
    auto headRequest = request("/public-body");
    auto* head = network.head(headRequest);
    if (!waitFor([&] { return head->isFinished(); }) || head->error() != QNetworkReply::NoError ||
        !head->readAll().isEmpty() || head->manager() != &network)
        return 9;
    head->deleteLater();
    origin.entityTags.insert("/public-unchanged", "\"public-v1\"");
    auto unchangedRequest = request("/public-unchanged");
    unchangedRequest.setRawHeader("If-None-Match", "\"public-v1\"");
    auto* unchanged = network.get(unchangedRequest);
    if (!waitFor([&] { return unchanged->isFinished(); }) ||
        unchanged->error() != QNetworkReply::NoError ||
        unchanged->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 304 ||
        unchanged->rawHeader("ETag") != "\"public-v1\"" || !unchanged->readAll().isEmpty())
        return 10;
    unchanged->deleteLater();
    auto untrustedRequest = request("/public-body");
    auto untrustedConfiguration = untrustedRequest.sslConfiguration();
    untrustedConfiguration.setCaCertificates({});
    untrustedRequest.setSslConfiguration(untrustedConfiguration);
    auto* untrusted = network.get(untrustedRequest);
    if (!waitFor([&] { return untrusted->isFinished(); }) ||
        untrusted->error() != QNetworkReply::SslHandshakeFailedError)
        return 11;
    untrusted->deleteLater();
    origin.responseDelays.insert("/public-in-flight", 1500);
    auto* inFlight = network.get(request("/public-in-flight"));
    if (!waitFor([&] { return origin.requests.contains(QStringLiteral("GET /public-in-flight")); }))
        return 12;
    inFlight->abort();
    if (!inFlight->isFinished() || inFlight->error() != QNetworkReply::OperationCanceledError)
        return 13;
    inFlight->deleteLater();
#ifdef Q_OS_WIN
    origin.responseDelays.insert("/public-relay-crash", 5000);
    auto* interrupted = network.get(request("/public-relay-crash"));
    if (!waitFor(
            [&] { return origin.requests.contains(QStringLiteral("GET /public-relay-crash")); }))
        return 14;
    const auto processId = relay.memory().value(QStringLiteral("pid")).toString().toUInt();
    const auto process = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
    const bool killed = process && TerminateProcess(process, 1);
    if (process)
        CloseHandle(process);
    if (!killed || !waitFor([&] { return interrupted->isFinished(); }) ||
        interrupted->error() == QNetworkReply::NoError ||
        !waitFor([&] { return !relay.running(); }, 4000))
        return 15;
    interrupted->deleteLater();
    auto* restarted = network.head(request("/public-body"));
    if (!waitFor([&] { return restarted->isFinished(); }) ||
        restarted->error() != QNetworkReply::NoError)
        return 16;
    restarted->deleteLater();
    if (!waitFor([&] { return !relay.running(); }, 4000))
        return 17;
    QTemporaryDir ownerDirectory;
    if (!ownerDirectory.isValid())
        return 18;
    QProcess owner;
    owner.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
    const auto observation = ownerDirectory.filePath(QStringLiteral("relay.json"));
    owner.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--relay-owner=") + observation});
    if (!waitFor([&] { return QFile::exists(observation); }, 5000))
        return 19;
    QFile observed(observation);
    if (!observed.open(QIODevice::ReadOnly))
        return 23;
    const auto ownedPid = QJsonDocument::fromJson(observed.readAll())
                              .object()
                              .value(QStringLiteral("pid"))
                              .toString()
                              .toUInt();
    struct ProcessHandle {
        HANDLE value = nullptr;
        ~ProcessHandle() {
            if (value)
                CloseHandle(value);
        }
    } child{OpenProcess(SYNCHRONIZE, FALSE, ownedPid)};
    owner.kill();
    if (!child.value || !waitFor(
                            [&] {
                                return owner.state() == QProcess::NotRunning &&
                                       WaitForSingleObject(child.value, 0) == WAIT_OBJECT_0;
                            },
                            5000))
        return 24;
#endif
    origin.responseDelays.insert("/public-delayed", 5000);
    auto* cancelled = network.get(request("/public-delayed"));
    bool finished = false;
    QObject::connect(cancelled, &QNetworkReply::finished, &application, [&] { finished = true; });
    cancelled->abort();
    if (!waitFor([&] { return finished; }) ||
        cancelled->error() != QNetworkReply::OperationCanceledError)
        return 5;
    cancelled->deleteLater();
    if (!waitFor([&] { return !relay.running(); }, 4000))
        return 6;
    quickclash::test::ConnectFixture proxy(origin.serverPort());
    if (!proxy.listen(QHostAddress::LocalHost))
        return 7;
    network.setProxy(
        QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), proxy.serverPort()));
    auto* explicitProxy = network.get(request("/public-body"));
    QObject::connect(explicitProxy, &QIODevice::readyRead, &application,
                     [explicitProxy] { (void)explicitProxy->readAll(); });
    if (!waitFor([&] { return explicitProxy->isFinished(); }) ||
        explicitProxy->error() != QNetworkReply::NoError || proxy.targets.isEmpty() ||
        relay.running())
        return 8;
    explicitProxy->deleteLater();
    quickclash::infra::NetworkRelay missing(
        nullptr, QDir(QCoreApplication::applicationDirPath())
                     .filePath(QStringLiteral("absent_quickclash_transport.exe")));
    quickclash::infra::DownloadNetwork unavailable(missing);
    unavailable.setProxy(QNetworkProxy::NoProxy);
    const auto beforeMissing = origin.requests.size();
    auto* rejected = unavailable.get(request("/public-body"));
    if (!waitFor([&] { return rejected->isFinished(); }) ||
        rejected->error() != QNetworkReply::ProxyConnectionClosedError ||
        beforeMissing != origin.requests.size() || missing.running())
        return 25;
    rejected->deleteLater();
    std::puts("download transport lifecycle and bounded forwarding passed");
    return 0;
}
