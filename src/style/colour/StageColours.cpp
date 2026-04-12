#include "StageColours.hpp"

#include <unordered_map>

using namespace ftxui;

namespace ally::style::colour {

auto stage_palette(const std::string& slug) -> std::optional<StagePalette> {
  static const std::unordered_map<std::string, StagePalette> kPalette = {
      {"intent", {Color::RGB(224, 242, 254), Color::RGB(3, 105, 161)}},
      {"question", {Color::RGB(254, 243, 199), Color::RGB(146, 64, 14)}},
      {"research", {Color::RGB(237, 233, 254), Color::RGB(109, 40, 217)}},
      {"design", {Color::RGB(252, 231, 243), Color::RGB(190, 24, 93)}},
      {"structure", {Color::RGB(209, 250, 229), Color::RGB(6, 95, 70)}},
      {"plan", {Color::RGB(224, 231, 255), Color::RGB(55, 48, 163)}},
      {"implement", {Color::RGB(219, 234, 254), Color::RGB(30, 64, 175)}},
      {"pr", {Color::RGB(220, 252, 231), Color::RGB(22, 101, 52)}},
      {"document", {Color::RGB(243, 244, 246), Color::RGB(55, 65, 81)}},
      {"tangent", {Color::RGB(255, 237, 213), Color::RGB(154, 52, 18)}},
  };
  auto iter = kPalette.find(slug);
  return iter != kPalette.end() ? std::optional(iter->second) : std::nullopt;
}

auto stage_fg_color(const std::string& slug) -> std::optional<ftxui::Color> {
  static const std::unordered_map<std::string, ftxui::Color> kColors = {
      {"intent", Color::Cyan},        {"question", Color::Yellow},  {"research", Color::Blue}, {"design", Color::Magenta},
      {"structure", Color::Green},    {"plan", Color::GrayLight},   {"implement", Color::Red}, {"pr", Color::GreenLight},
      {"document", Color::CyanLight}, {"tangent", Color::GrayDark},
  };
  auto iter = kColors.find(slug);
  return iter != kColors.end() ? std::optional(iter->second) : std::nullopt;
}

auto render_stage_badge(const std::string& slug) -> ftxui::Element {
  if (slug.empty()) {
    return text("");
  }
  auto palette = stage_palette(slug);
  if (palette.has_value()) {
    return text(" " + slug + " ") | bgcolor(palette->bg) | color(palette->fg);
  }
  return text(slug);
}

}  // namespace ally::style::colour
