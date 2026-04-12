#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ally::configuration {

struct OpenCodeConfig {
  std::optional<std::string> server_url;
  std::optional<std::string> default_provider;
  bool lock_provider{false};
};

auto LoadOpenCodeConfig(const std::filesystem::path& project_root) -> OpenCodeConfig;

}  // namespace ally::configuration
