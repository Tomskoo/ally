#pragma once

#include <filesystem>
#include <ftxui/screen/color.hpp>
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

  /// Load the default theme. Tries ~/.config/ally/themes/tokyonight.yaml first,
  /// then falls back to the compiled-in embedded theme.
  static auto LoadDefault() -> HighlightTheme;

  /// Resolve a tree-sitter capture name to a color.
  /// Uses dotted fallback: "keyword.conditional.ternary" tries exact match,
  /// then "keyword.conditional", then "keyword", then fg().
  [[nodiscard]] auto Resolve(std::string_view capture) const -> ftxui::Color;

  [[nodiscard]] auto fg() const -> ftxui::Color { return fg_; }
  [[nodiscard]] auto bg() const -> ftxui::Color { return bg_; }

 private:
  std::unordered_map<std::string, ftxui::Color> captures_;
  ftxui::Color fg_;
  ftxui::Color bg_;
};

}  // namespace ally::rendering
