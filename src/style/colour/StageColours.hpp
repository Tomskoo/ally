#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <optional>
#include <string>

namespace ally::style::colour {

struct StagePalette {
  ftxui::Color bg;
  ftxui::Color fg;
};

/// Returns the full bg/fg pair for rich badge rendering (Board-style).
/// Returns std::nullopt for unknown stage slugs.
auto stage_palette(const std::string& slug) -> std::optional<StagePalette>;

/// Returns just the foreground color for simpler inline usage.
/// Returns std::nullopt for unknown stage slugs.
auto stage_fg_color(const std::string& slug) -> std::optional<ftxui::Color>;

/// Renders text(" " + slug + " ") with bg/fg from the palette,
/// or plain text(slug) for unknown slugs.
auto render_stage_badge(const std::string& slug) -> ftxui::Element;

}  // namespace ally::style::colour
