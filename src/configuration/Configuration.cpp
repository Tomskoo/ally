#include "src/configuration/Configuration.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

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
    if (opencode["model_per_provider"] && opencode["model_per_provider"].IsMap()) {
      for (const auto& pair : opencode["model_per_provider"]) {
        config.model_per_provider[pair.first.as<std::string>()] = pair.second.as<std::string>();
      }
    }
    return config;
  } catch (const YAML::Exception&) {
    return {};
  }
}

auto LoadRenderingConfig(const std::filesystem::path& project_root) -> RenderingConfig {
  auto config_path = project_root / ".ally" / "config.yaml";
  if (!std::filesystem::exists(config_path)) {
    return {};
  }

  try {
    auto root = YAML::LoadFile(config_path.string());
    auto rendering = root["rendering"];
    if (!rendering) {
      return {};
    }

    RenderingConfig config;
    if (rendering["theme"]) {
      config.theme = rendering["theme"].as<std::string>();
    }
    if (rendering["query_dirs"] && rendering["query_dirs"].IsSequence()) {
      const char* home = std::getenv("HOME");
      for (const auto& entry : rendering["query_dirs"]) {
        auto dir = entry.as<std::string>();
        if (!dir.empty() && dir[0] == '~' && (home != nullptr) && home[0] != '\0') {
          dir = std::string(home) + dir.substr(1);
        }
        config.query_dirs.emplace_back(std::move(dir));
      }
    }
    return config;
  } catch (const YAML::Exception&) {
    return {};
  }
}

auto SaveModelForProvider(const std::filesystem::path& project_root, const std::string& provider_id,
                          const std::string& model_id) -> void {
  auto config_path = project_root / ".ally" / "config.yaml";

  YAML::Node root;
  try {
    if (std::filesystem::exists(config_path)) {
      root = YAML::LoadFile(config_path.string());
    }
  } catch (const YAML::Exception&) {
    // Start fresh if the file is corrupt.
  }

  root["opencode"]["model_per_provider"][provider_id] = model_id;

  std::filesystem::create_directories(config_path.parent_path());
  std::ofstream out(config_path);
  out << root;
}

}  // namespace ally::configuration
