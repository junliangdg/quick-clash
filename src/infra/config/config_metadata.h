#pragma once
#include "domain/contracts.h"
#include <QIODevice>
#include <QSet>
#include <functional>
#include <string>
#include <vector>

namespace quickclash::infra {
struct ConfigMetadata {
    QByteArray fieldsYaml;
    QSet<QString> keys;
    QString defaultPolicy;
    bool geoRule = false;
    std::vector<std::string> rules;
    std::size_t events = 0;
};
// Read bounded metadata without allocating a Node for every routing rule.
// This does not replace schema validation or the official core's -t check.
// separateRules keeps all other fields for full schema validation, with scalar rules separate.
[[nodiscard]] domain::Result<ConfigMetadata> configMetadata(const QByteArray& input,
                                                            bool separateRules = false);
// Stream one root scalar sequence to a synchronous sink, retaining only bounded other fields.
// The caller rewinds an owned file; no borrowed view survives a callback.
[[nodiscard]] domain::Result<ConfigMetadata>
scanConfigScalars(QIODevice& input, const std::string& field,
                  const std::function<bool(const std::string&)>& sink, qint64 limit);
} // namespace quickclash::infra
