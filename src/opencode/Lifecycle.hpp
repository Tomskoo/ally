#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "src/opencode/Error.hpp"
#include "src/opencode/State.hpp"
#include "src/opencode/Types.hpp"

namespace ally::opencode::lifecycle {

constexpr int kDefaultHealthTimeoutSeconds = 30;

struct SpawnArgs {
  std::filesystem::path working_dir;
  std::vector<std::string> extra_args;
};

auto FindBinary() -> Result<std::filesystem::path>;

auto Spawn(const SpawnArgs& args) -> Result<OpenCodeProcess>;

auto WaitForHealth(const std::string& base_url, std::chrono::seconds timeout = std::chrono::seconds{kDefaultHealthTimeoutSeconds})
    -> Result<HealthResponse>;

void Shutdown(OpenCodeProcess& process);

}  // namespace ally::opencode::lifecycle
