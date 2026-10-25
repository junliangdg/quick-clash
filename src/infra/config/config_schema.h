#pragma once
#include "domain/contracts.h"
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace quickclash::infra {
// Called only after bounded parsing, duplicate-key and tree checks.
[[nodiscard]] domain::CommandResult
validateConfigSchema(const YAML::Node& root, const std::vector<std::string>* rules = nullptr);
} // namespace quickclash::infra
