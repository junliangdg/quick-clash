#include "single_instance.h"
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

namespace {
enum class Reply { Immediate, Fragmented, Invalid, Truncated, Empty, Timeout };

bool checkReply(Reply reply) {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "Reply fixture temporary directory failed\n";
        return false;
    }
    const auto key = directory.path();
    const auto digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    const auto endpoint = QStringLiteral("quickclash_") + QString::fromLatin1(digest);
    QLockFile lock(QDir::temp().filePath(endpoint + QStringLiteral(".lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        std::cerr << "Reply fixture lock failed\n";
        return false;
    }
    QLocalServer::removeServer(endpoint);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(endpoint)) {
        std::cerr << "Reply fixture listen failed: " << server.errorString().toStdString() << "\n";
        return false;
    }

    bool receivedRequest = false;
    QByteArray request;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        if (!socket) {
            return;
        }
        socket->setParent(&server);
        const auto receive = [&, socket] {
            request += socket->readAll();
            if (!request.contains('\n') || receivedRequest) {
                return;
            }
            receivedRequest = request == QByteArrayLiteral("activate\n");
            if (!receivedRequest) {
                socket->disconnectFromServer();
                return;
            }
            switch (reply) {
            case Reply::Immediate:
                socket->write("ok\n");
                break;
            case Reply::Fragmented:
                socket->write("o");
                QTimer::singleShot(100, socket, [socket] {
                    socket->write("k\n");
                    socket->disconnectFromServer();
                });
                return;
            case Reply::Invalid:
                socket->write("no\n");
                break;
            case Reply::Truncated:
                socket->write("o");
                break;
            case Reply::Empty:
                break;
            case Reply::Timeout:
                return;
            }
            socket->disconnectFromServer();
        };
        QObject::connect(socket, &QLocalSocket::readyRead, socket, receive);
        if (socket->bytesAvailable() > 0) {
            receive();
        }
    });

    QProcess child;
    child.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool finished = false;
    bool succeeded = false;
    QObject::connect(&child, &QProcess::finished, &loop,
                     [&](int code, QProcess::ExitStatus status) {
                         finished = true;
                         succeeded = code == 0 && status == QProcess::NormalExit;
                         loop.quit();
                     });
    QObject::connect(&child, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(4000);
    const bool expectSuccess = reply == Reply::Immediate || reply == Reply::Fragmented;
    QElapsedTimer elapsed;
    elapsed.start();
    child.start(
        QCoreApplication::applicationFilePath(),
        {expectSuccess ? QStringLiteral("--secondary") : QStringLiteral("--expect-unavailable"),
         key});
    loop.exec();
    if (child.state() != QProcess::NotRunning) {
        child.kill();
        child.waitForFinished(1000);
    }
    if (!finished || !succeeded || !receivedRequest ||
        (reply == Reply::Timeout && elapsed.elapsed() < 750)) {
        std::cerr << "Reply case failed: case=" << static_cast<int>(reply)
                  << " finished=" << finished << " succeeded=" << succeeded
                  << " receivedRequest=" << receivedRequest << " elapsedMs=" << elapsed.elapsed()
                  << " error=" << child.errorString().toStdString() << "\n";
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 &&
        (app.arguments().at(1) == QStringLiteral("--secondary") ||
         app.arguments().at(1) == QStringLiteral("--background") ||
         app.arguments().at(1) == QStringLiteral("--expect-unavailable"))) {
        quickclash::SingleInstance secondary(app.arguments().at(2));
        const auto result =
            secondary.start(app.arguments().at(1) != QStringLiteral("--background"));
        const auto expected = app.arguments().at(1) == QStringLiteral("--expect-unavailable")
                                  ? quickclash::SingleInstance::Result::Unavailable
                                  : quickclash::SingleInstance::Result::Activated;
        if (result != expected) {
            std::cerr << "Secondary failed: result=" << static_cast<int>(result) << "\n";
            return 1;
        }
        return 0;
    }
    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "Temporary directory failed: " << directory.errorString().toStdString()
                  << "\n";
        return 1;
    }
    const auto key = directory.path();
    quickclash::SingleInstance primary(key);
    if (primary.start() != quickclash::SingleInstance::Result::Primary) {
        std::cerr << "Primary failed to start\n";
        return 2;
    }
    int activations = 0;
    QObject::connect(&primary, &quickclash::SingleInstance::activationRequested, &app,
                     [&activations] { ++activations; });
    QProcess child;
    child.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    bool backgroundChecked = false;
    QObject::connect(&child, &QProcess::finished, &app,
                     [&app, &activations, &backgroundChecked, &child,
                      &key](int code, QProcess::ExitStatus status) {
                         if (!backgroundChecked && code == 0 && status == QProcess::NormalExit &&
                             activations == 0) {
                             backgroundChecked = true;
                             child.start(QCoreApplication::applicationFilePath(),
                                         {QStringLiteral("--secondary"), key});
                             return;
                         }
                         if (code != 0 || status != QProcess::NormalExit || activations != 1 ||
                             !backgroundChecked) {
                             std::cerr << "Child failed: backgroundChecked=" << backgroundChecked
                                       << " code=" << code << " status=" << static_cast<int>(status)
                                       << " activations=" << activations
                                       << " error=" << child.errorString().toStdString() << "\n";
                             app.exit(3);
                             return;
                         }
                         app.exit(0);
                     });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&app, &child] {
        std::cerr << "Child timed out: " << child.errorString().toStdString() << "\n";
        child.kill();
        app.exit(4);
    });
    watchdog.start(5000);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--background"), key});
    const int result = app.exec();
    watchdog.stop();
    if (child.state() != QProcess::NotRunning) {
        child.kill();
        child.waitForFinished(1000);
    }
    if (result != 0) {
        return result;
    }
    for (const auto reply : {Reply::Immediate, Reply::Fragmented, Reply::Invalid, Reply::Truncated,
                             Reply::Empty, Reply::Timeout}) {
        if (!checkReply(reply)) {
            return 5;
        }
    }
    std::cout << "Single-instance IPC activation and acknowledgement checks passed\n";
    return 0;
}
