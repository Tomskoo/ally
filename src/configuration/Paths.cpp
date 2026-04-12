#include "Paths.hpp"

#include <cstdlib>

namespace ally::configuration {

auto config_dir() -> std::filesystem::path {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if ((xdg != nullptr) && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "ally";
  }
  const char* home = std::getenv("HOME");
  if ((home != nullptr) && home[0] != '\0') {
    return std::filesystem::path(home) / ".config" / "ally";
  }
  return std::filesystem::current_path() / ".config" / "ally";
}

auto themes_dir() -> std::filesystem::path { return config_dir() / "themes"; }

auto queries_dir() -> std::filesystem::path { return config_dir() / "queries"; }

}  // namespace ally::configuration
