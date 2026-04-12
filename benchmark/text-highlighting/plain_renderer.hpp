#pragma once
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace ally::rendering {

struct RenderedBlock {
  enum Kind { Paragraph, CodeBlock, Heading, Quote, List, Diff };
  Kind kind;
  ftxui::Element element;
};

class PlainRenderer {
 public:
  virtual ~PlainRenderer() = default;

  std::vector<RenderedBlock> Render(const std::string& markdown);
  virtual ftxui::Element RenderCodeBlock(const std::string& code, const std::string& language);
  ftxui::Element RenderDiff(const std::string& diff_text);
};

}  // namespace ally::rendering
