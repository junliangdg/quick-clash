#pragma once
#include "domain/contracts.h"
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
// Called only after bounded parsing, duplicate-key and tree checks.
[[nodiscard]] domain::CommandResult validateConfigSchema(const YAML::Node& root);
} // namespace quickclash::infra
