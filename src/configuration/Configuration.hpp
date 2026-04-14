#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace ally::configuration {

struct OpenCodeConfig {
  std::optional<std::string> server_url;
  std::optional<std::string> default_provider;
  bool lock_provider{false};
  std::unordered_map<std::string, std::string> model_per_provider;
};

auto LoadOpenCodeConfig(const std::filesystem::path& project_root) -> OpenCodeConfig;
auto SaveModelForProvider(const std::filesystem::path& project_root, const std::string& provider_id,
                          const std::string& model_id) -> void;

}  // namespace ally::configuration
