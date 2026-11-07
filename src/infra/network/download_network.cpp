#include "infra/network/download_network.h"
#include "infra/network/network_relay.h"
#include <QAuthenticator>
#include <QNetworkReply>
#include <QPointer>
#include <QSslConfiguration>
#include <QTimer>
#include <algorithm>

namespace quickclash::infra {
namespace {
// A single 64 KiB TLS buffer can stall at peer close with Qt's Windows CONNECT
// transport. Keep a small bounded window while callers consume 64 KiB chunks.
constexpr qint64 kTransportReadBuffer = 256 * 1024;
class RelayReply final : public QNetworkReply {
  public:
    RelayReply(QNetworkAccessManager* manager, NetworkRelay& relay,
               QNetworkAccessManager::Operation operation, const QNetworkRequest& request,
               const QNetworkProxy& proxy)
        : QNetworkReply(manager), relay_(relay), proxy_(proxy),
          sslConfiguration_(request.sslConfiguration()) {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        connect(&relay_, &NetworkRelay::available, this, [this] {
            if (acquired_ && !reply_ && !isFinished())
                begin(relay_.proxy());
        });
        connect(&relay_, &NetworkRelay::failed, this, [this] {
            if (acquired_)
                finish(QNetworkReply::ProxyConnectionClosedError);
        });
        QTimer::singleShot(0, this, [this] {
            if (isFinished())
                return;
            if (proxy_.type() == QNetworkProxy::NoProxy ||
                proxy_.type() == QNetworkProxy::DefaultProxy) {
                acquired_ = true;
                relay_.acquire();
                if (!isFinished() && relay_.ready())
                    begin(relay_.proxy());
            } else
                begin(proxy_);
        });
    }
    ~RelayReply() override {
        if (reply_) {
            disconnect(reply_, nullptr, this, nullptr);
            reply_->abort();
        }
        if (acquired_)
            relay_.release();
    }
    void abort() override {
        const QPointer<RelayReply> self(this);
        if (reply_ && !isFinished())
            reply_->abort();
        if (self && !isFinished())
            finish(QNetworkReply::OperationCanceledError);
    }
    bool isSequential() const override {
        return true;
    }
    qint64 bytesAvailable() const override {
        return QNetworkReply::bytesAvailable() + (reply_ ? reply_->bytesAvailable() : 0);
    }
    void setReadBufferSize(qint64 size) override {
        QNetworkReply::setReadBufferSize(size);
        if (reply_)
            reply_->setReadBufferSize(std::max(size, kTransportReadBuffer));
    }

  protected:
    qint64 readData(char* data, qint64 maximum) override {
        return reply_ ? reply_->read(data, maximum) : (isFinished() ? -1 : 0);
    }
    void sslConfigurationImplementation(QSslConfiguration& configuration) const override {
        configuration = reply_ ? reply_->sslConfiguration() : sslConfiguration_;
    }
    void setSslConfigurationImplementation(const QSslConfiguration& configuration) override {
        sslConfiguration_ = configuration;
        if (reply_)
            reply_->setSslConfiguration(configuration);
    }

  private:
    void metadata() {
        setUrl(reply_->url());
        for (const auto& header : reply_->rawHeaderPairs())
            setRawHeader(header.first, header.second);
        for (const auto attribute :
             {QNetworkRequest::HttpStatusCodeAttribute, QNetworkRequest::HttpReasonPhraseAttribute,
              QNetworkRequest::RedirectionTargetAttribute,
              QNetworkRequest::ConnectionEncryptedAttribute,
              QNetworkRequest::SourceIsFromCacheAttribute})
            setAttribute(attribute, reply_->attribute(attribute));
    }
    void begin(const QNetworkProxy& proxy) {
        if (reply_ || isFinished())
            return;
        network_.setProxy(proxy);
        if (acquired_)
            connect(&network_, &QNetworkAccessManager::proxyAuthenticationRequired, this,
                    [proxy](const QNetworkProxy& challenged, QAuthenticator* authenticator) {
                        if (challenged.hostName() == proxy.hostName() &&
                            challenged.port() == proxy.port()) {
                            authenticator->setUser(proxy.user());
                            authenticator->setPassword(proxy.password());
                        }
                    });
        auto pending = request();
        pending.setSslConfiguration(sslConfiguration_);
        reply_ = operation() == QNetworkAccessManager::HeadOperation ? network_.head(pending)
                                                                     : network_.get(pending);
        reply_->setReadBufferSize(std::max(readBufferSize(), kTransportReadBuffer));
        connect(reply_, &QIODevice::readyRead, this, [this] { emit readyRead(); });
        connect(reply_, &QNetworkReply::metaDataChanged, this, [this] {
            metadata();
            emit metaDataChanged();
        });
        connect(reply_, &QNetworkReply::sslErrors, this,
                [this](const QList<QSslError>& errors) { emit sslErrors(errors); });
        connect(reply_, &QNetworkReply::encrypted, this, &QNetworkReply::encrypted);
        connect(reply_, &QNetworkReply::downloadProgress, this, &QNetworkReply::downloadProgress);
        connect(reply_, &QNetworkReply::finished, this, [this] {
            metadata();
            finish(reply_->error());
        });
    }
    void finish(QNetworkReply::NetworkError error) {
        if (isFinished())
            return;
        if (error != QNetworkReply::NoError && reply_) {
            disconnect(reply_, nullptr, this, nullptr);
            reply_->abort();
        }
        if (acquired_) {
            acquired_ = false;
            relay_.release();
        }
        setFinished(true);
        const QPointer<RelayReply> self(this);
        if (error != QNetworkReply::NoError) {
            // Socket error strings can include private origins. Keep diagnostics symbolic.
            setError(error, QStringLiteral("Download transport failed"));
            emit errorOccurred(error);
        }
        if (self)
            emit finished();
    }
    NetworkRelay& relay_;
    QNetworkProxy proxy_;
    QSslConfiguration sslConfiguration_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    bool acquired_ = false;
};
} // namespace
DownloadNetwork::DownloadNetwork(NetworkRelay& relay, QObject* parent)
    : QNetworkAccessManager(parent), relay_(relay) {}
QNetworkReply* DownloadNetwork::createRequest(Operation operation, const QNetworkRequest& request,
                                              QIODevice* outgoingData) {
    if ((operation != GetOperation && operation != HeadOperation) || outgoingData ||
        request.url().scheme() != QStringLiteral("https"))
        return QNetworkAccessManager::createRequest(operation, request, outgoingData);
    return new RelayReply(this, relay_, operation, request, proxy());
}
} // namespace quickclash::infra
