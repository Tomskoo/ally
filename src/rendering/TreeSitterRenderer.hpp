#pragma once

#include "HighlightTheme.hpp"
#include "PlainRenderer.hpp"
#include "QueryStore.hpp"

namespace ally::rendering {

class TreeSitterRenderer : public PlainRenderer {
 public:
  explicit TreeSitterRenderer(HighlightTheme theme);

  auto RenderCodeBlock(const std::string& code, const std::string& language) -> ftxui::Element override;

 private:
  QueryStore store_;
};

}  // namespace ally::rendering
