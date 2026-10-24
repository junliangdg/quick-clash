#pragma once
#include <QList>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace quickclash::presentation {
struct FeatureDescriptor {
    QString id;
    QString title;
    QString glyph;
    QUrl page;
    QString availability;
    QStringList contextKeys;
};
struct EmbeddedDescriptor {
    QString id;
    QString moduleId;
    QUrl component;
    QStringList contextKeys;
};
class FeatureRegistry final {
  public:
    FeatureRegistry();
    [[nodiscard]] const QList<FeatureDescriptor>& features() const {
        return features_;
    }
    [[nodiscard]] const FeatureDescriptor* resolve(const QString& id,
                                                   const QVariantMap& context = {}) const;
    [[nodiscard]] const EmbeddedDescriptor* resolveEmbedded(const QString& id,
                                                            const QVariantMap& context = {}) const;
    [[nodiscard]] QList<FeatureDescriptor> project(const QStringList& ids) const;

  private:
    QList<FeatureDescriptor> features_;
    QList<EmbeddedDescriptor> embedded_;
};
} // namespace quickclash::presentation
