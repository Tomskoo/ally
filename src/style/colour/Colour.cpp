#include "Colour.hpp"

#include <string>

#include "yaml-cpp/yaml.h"

namespace ally::style::colour {

namespace {
auto from_string(const std::string& colour) -> Color { return Color{}; }
}  // namespace

auto from_yaml(const YAML::Node& node) -> Settings {
  Settings settings{};
  if (node["primary"]) {
    settings.primary = from_string(node["primary"].as<std::string>());
  }
  //... so on and so forth
  return settings;
}

}  // namespace ally::style::colour
