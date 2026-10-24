#include "single_instance.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments().at(1) == QStringLiteral("--secondary")) {
        quickclash::SingleInstance secondary(app.arguments().at(2));
        return secondary.start() == quickclash::SingleInstance::Result::Activated ? 0 : 1;
    }
    QTemporaryDir directory;
    if (!directory.isValid()) {
        return 1;
    }
    const auto key = directory.path();
    quickclash::SingleInstance primary(key);
    if (primary.start() != quickclash::SingleInstance::Result::Primary) {
        return 2;
    }
    int activations = 0;
    QObject::connect(&primary, &quickclash::SingleInstance::activationRequested, &app,
                     [&activations] { ++activations; });
    QProcess child;
    QObject::connect(
        &child, &QProcess::finished, &app,
        [&app, &activations](int code, QProcess::ExitStatus status) {
            app.exit(code == 0 && status == QProcess::NormalExit && activations == 1 ? 0 : 3);
        });
    QTimer::singleShot(5000, &app, [&app, &child] {
        child.kill();
        app.exit(4);
    });
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--secondary"), key});
    const int result = app.exec();
    if (child.state() != QProcess::NotRunning) {
        child.kill();
        child.waitForFinished(1000);
    }
    if (result == 0) {
        std::cout << "Single-instance IPC activation passed\n";
    }
    return result;
}
