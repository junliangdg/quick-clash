#pragma once
#include "domain/contracts.h"
#include <QSet>
#include <string>
#include <vector>

namespace quickclash::infra {
struct ConfigMetadata {
    QByteArray fieldsYaml;
    QSet<QString> keys;
    QString defaultPolicy;
    bool geoRule = false;
    std::vector<std::string> rules;
};
// Read bounded metadata without allocating a Node for every routing rule.
// This does not replace schema validation or the official core's -t check.
// separateRules keeps all other fields for full schema validation, with scalar rules separate.
[[nodiscard]] domain::Result<ConfigMetadata> configMetadata(const QByteArray& input,
                                                            bool separateRules = false);
} // namespace quickclash::infra
