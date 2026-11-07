#pragma once
#include <QNetworkAccessManager>

namespace quickclash::infra {
class NetworkRelay;
// Direct HTTPS connections use a small transport process, keeping address-space-wide
// reads associated with external connections away from the GUI's font/QML mappings.
class DownloadNetwork final : public QNetworkAccessManager {
  public:
    explicit DownloadNetwork(NetworkRelay& relay, QObject* parent = nullptr);

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoingData = nullptr) override;

  private:
    NetworkRelay& relay_;
};
} // namespace quickclash::infra
