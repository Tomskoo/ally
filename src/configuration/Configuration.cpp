#include "src/configuration/Configuration.hpp"

#include <filesystem>

#include "yaml-cpp/yaml.h"

namespace ally::configuration {

auto LoadOpenCodeConfig(const std::filesystem::path& project_root) -> OpenCodeConfig {
  auto config_path = project_root / ".ally" / "config.yaml";
  if (!std::filesystem::exists(config_path)) {
    return {};
  }

  try {
    auto root = YAML::LoadFile(config_path.string());
    auto opencode = root["opencode"];
    if (!opencode) {
      return {};
    }

    OpenCodeConfig config;
    if (opencode["server_url"]) {
      config.server_url = opencode["server_url"].as<std::string>();
    }
    if (opencode["default_provider"]) {
      config.default_provider = opencode["default_provider"].as<std::string>();
    }
    if (opencode["lock_provider"]) {
      config.lock_provider = opencode["lock_provider"].as<bool>();
    }
    return config;
  } catch (const YAML::Exception&) {
    return {};
  }
}

}  // namespace ally::configuration
