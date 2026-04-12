#pragma once
#include <ftxui/dom/elements.hpp>
#include <string>

#include "plain_renderer.hpp"

namespace ally::rendering {

class TreeSitterRenderer : public PlainRenderer {
 public:
  ftxui::Element RenderCodeBlock(const std::string& code, const std::string& language) override;
};

}  // namespace ally::rendering
