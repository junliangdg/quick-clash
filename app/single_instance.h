#pragma once
#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>
#include <memory>

namespace quickclash {
class SingleInstance final : public QObject {
    Q_OBJECT
  public:
    enum class Result { Primary, Activated, Unavailable };
    explicit SingleInstance(QString key, QObject* parent = nullptr);
    [[nodiscard]] Result start();
  signals:
    void activationRequested();

  private:
    QString endpoint_;
    std::unique_ptr<QLockFile> lock_;
    QLocalServer server_;
};
} // namespace quickclash
