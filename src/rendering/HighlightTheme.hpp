#pragma once

#include <filesystem>
#include <ftxui/screen/color.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ally::rendering {

class HighlightTheme {
 public:
  /// Load a theme from a YAML file.
  /// Throws std::runtime_error if the file cannot be read or parsed.
  static auto Load(const std::filesystem::path& yaml_path) -> HighlightTheme;

  /// Load a theme from a YAML string.
  /// Throws std::runtime_error if the content cannot be parsed.
  static auto LoadFromString(const std::string& yaml_content) -> HighlightTheme;

  /// Load a theme by name. Tries themes_dir()/<name>.yaml first,
  /// then falls back to the compiled-in embedded theme for "tokyonight".
  /// If no name is given, defaults to "tokyonight".
  static auto LoadDefault(const std::optional<std::string>& theme_name = std::nullopt) -> HighlightTheme;

  /// Resolve a tree-sitter capture name to a color.
  /// Uses dotted fallback: "keyword.conditional.ternary" tries exact match,
  /// then "keyword.conditional", then "keyword", then fg().
  [[nodiscard]] auto Resolve(std::string_view capture) const -> ftxui::Color;

  /// Resolve a capture name without falling back to fg(). Returns std::nullopt
  /// if no entry exists at any level of the dotted-fallback walk. Useful when
  /// the caller wants to distinguish "theme defines this capture" from "theme
  /// is silent on this capture".
  [[nodiscard]] auto TryResolve(std::string_view capture) const -> std::optional<ftxui::Color>;

  [[nodiscard]] auto fg() const -> ftxui::Color { return fg_; }
  [[nodiscard]] auto bg() const -> ftxui::Color { return bg_; }

 private:
  std::unordered_map<std::string, ftxui::Color> captures_;
  ftxui::Color fg_;
  ftxui::Color bg_;
};

}  // namespace ally::rendering
