#pragma once

#include <filesystem>
#include <vector>

#include "HighlightTheme.hpp"
#include "PlainRenderer.hpp"
#include "QueryStore.hpp"

namespace ally::rendering {

class TreeSitterRenderer : public PlainRenderer {
 public:
  explicit TreeSitterRenderer(HighlightTheme theme, std::vector<std::filesystem::path> query_dirs = {});

  auto RenderCodeBlock(const std::string& code, const std::string& language) -> ftxui::Element override;
  [[nodiscard]] auto InlineCodeStyle() const -> ftxui::Decorator override;

 private:
  HighlightTheme theme_;
  QueryStore store_;
};

}  // namespace ally::rendering
