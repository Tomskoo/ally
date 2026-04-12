#pragma once

#include <filesystem>

namespace ally::configuration {

auto config_dir() -> std::filesystem::path;
auto themes_dir() -> std::filesystem::path;
auto queries_dir() -> std::filesystem::path;

}  // namespace ally::configuration
