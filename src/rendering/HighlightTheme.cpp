#include "HighlightTheme.hpp"

#include <fstream>
#include <stdexcept>

#include "src/configuration/Paths.hpp"
#include "yaml-cpp/yaml.h"

namespace ally::rendering {

namespace {

// Parse "#rrggbb" hex string to ftxui::Color::RGB.
auto ParseHex(const std::string& hex) -> ftxui::Color {
  if (hex.size() != 7 || hex[0] != '#') {
    return ftxui::Color::Default;
  }
  auto r = static_cast<uint8_t>(std::stoul(hex.substr(1, 2), nullptr, 16));
  auto g = static_cast<uint8_t>(std::stoul(hex.substr(3, 2), nullptr, 16));
  auto b = static_cast<uint8_t>(std::stoul(hex.substr(5, 2), nullptr, 16));
  return ftxui::Color::RGB(r, g, b);
}

}  // namespace

auto HighlightTheme::Load(const std::filesystem::path& yaml_path) -> HighlightTheme {
  YAML::Node root = YAML::LoadFile(yaml_path.string());

  // Parse palette: name -> Color
  std::unordered_map<std::string, ftxui::Color> palette;
  if (root["palette"]) {
    for (const auto& entry : root["palette"]) {
      auto name = entry.first.as<std::string>();
      auto hex = entry.second.as<std::string>();
      palette[name] = ParseHex(hex);
    }
  }

  HighlightTheme theme;
  theme.fg_ = (palette.count("fg") != 0U) ? palette["fg"] : ftxui::Color::GrayLight;
  theme.bg_ = (palette.count("bg") != 0U) ? palette["bg"] : ftxui::Color::Default;

  // Parse highlights: capture_name -> palette_ref or #hex
  if (root["highlights"]) {
    for (const auto& entry : root["highlights"]) {
      auto capture = entry.first.as<std::string>();
      auto value = entry.second.as<std::string>();
      if (palette.count(value) != 0U) {
        theme.captures_[capture] = palette[value];
      } else if (!value.empty() && value[0] == '#') {
        theme.captures_[capture] = ParseHex(value);
      } else {
        theme.captures_[capture] = theme.fg_;
      }
    }
  }

  return theme;
}

auto HighlightTheme::LoadDefault() -> HighlightTheme {
  auto path = configuration::themes_dir() / "tokyonight.yaml";
  return Load(path);
}

auto HighlightTheme::Resolve(std::string_view capture) const -> ftxui::Color {
  std::string key(capture);
  while (true) {
    auto iter = captures_.find(key);
    if (iter != captures_.end()) {
      return iter->second;
    }
    auto dot = key.rfind('.');
    if (dot == std::string::npos) {
      break;
    }
    key = key.substr(0, dot);
  }
  return fg_;
}

}  // namespace ally::rendering
