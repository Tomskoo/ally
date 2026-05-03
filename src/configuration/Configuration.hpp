#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "src/configuration/ConfigError.hpp"

namespace ally::configuration {

struct OpenCodeConfig {
  std::optional<std::string> server_url;
  std::optional<std::string> default_provider;
  bool lock_provider{false};
  std::unordered_map<std::string, std::string> model_per_provider;
};

struct RenderingConfig {
  std::vector<std::filesystem::path> query_dirs;
  std::optional<std::string> theme;
};

template <typename T>
using ConfigResult = std::variant<T, ConfigError>;

auto LoadOpenCodeConfig(const std::filesystem::path& project_root) -> ConfigResult<OpenCodeConfig>;
auto LoadRenderingConfig(const std::filesystem::path& project_root) -> ConfigResult<RenderingConfig>;
auto SaveModelForProvider(const std::filesystem::path& project_root, const std::string& provider_id,
                          const std::string& model_id) -> void;

}  // namespace ally::configuration
