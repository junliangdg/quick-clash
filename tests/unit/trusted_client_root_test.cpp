#include "platform/windows/trusted_client_root.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using quickclash::platform::makeTrustedClientRoot;
using quickclash::platform::readTrustedClientRoot;
using quickclash::platform::trustedClientImage;

namespace {
void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
} // namespace

class TrustedClientRootTest final : public QObject {
    Q_OBJECT
  private slots:
    void canonicalRootAndExactImage() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto root = QFileInfo(directory.path()).canonicalFilePath();
        QVERIFY(!root.isEmpty());
        const auto executable = QDir(root).filePath(QStringLiteral("quickclash.exe"));
        writeFile(executable, "test");
        const auto trusted = makeTrustedClientRoot(root);
        QVERIFY(trusted.has_value());
        QCOMPARE(trusted->executable, QDir(root).filePath(QStringLiteral("quickclash.exe")));
        QCOMPARE(trustedClientImage(executable, *trusted).value_or(QString{}), trusted->executable);

        const auto other = QDir(root).filePath(QStringLiteral("other.exe"));
        writeFile(other, "test");
        QVERIFY(!trustedClientImage(other, *trusted).has_value());
        QVERIFY(!makeTrustedClientRoot(QDir(root).filePath(QStringLiteral("missing"))).has_value());
        QVERIFY(!makeTrustedClientRoot(QStringLiteral("relative")).has_value());
    }

    void configurationFailsClosed() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto root = QFileInfo(directory.path()).canonicalFilePath();
        writeFile(QDir(root).filePath(QStringLiteral("quickclash.exe")), "test");
        const auto config = QDir(root).filePath(QStringLiteral("trusted_client_root.json"));
        writeFile(config, QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                                    {QStringLiteral("clientRoot"), root}})
                              .toJson(QJsonDocument::Compact));
        const auto loaded = readTrustedClientRoot(config, QStringLiteral("C:/missing"));
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->root, root);

        writeFile(config, QByteArrayLiteral("not json"));
        QVERIFY(!readTrustedClientRoot(config, root).has_value());
        QFile::remove(config);
        QVERIFY(readTrustedClientRoot(config, root).has_value());
        QVERIFY(!readTrustedClientRoot(config, QStringLiteral("C:/missing")).has_value());
    }
    void packagedCoreDoesNotFallBack() {
        QTemporaryDir directory;
        QDir root(directory.path());
        QVERIFY(root.mkpath(QStringLiteral("bin")));
        QVERIFY(root.mkpath(QStringLiteral("metadata")));
        writeFile(root.filePath(QStringLiteral("metadata/release_matrix.json")), "{}");
        const auto missing =
            quickclash::platform::portableCorePath(root.filePath(QStringLiteral("bin")));
        QCOMPARE(QDir::cleanPath(missing), root.filePath(QStringLiteral("core/mihomo.exe")));
        QVERIFY(!QFileInfo::exists(missing));
    }
};

QTEST_GUILESS_MAIN(TrustedClientRootTest)
#include "trusted_client_root_test.moc"
