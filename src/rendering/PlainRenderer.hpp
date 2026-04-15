#pragma once
#include <cstdint>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace ally::rendering {

struct RenderedBlock {
  enum Kind : std::uint8_t { Paragraph, CodeBlock, Heading, Quote, List, Diff, Table };
  Kind kind;
  ftxui::Element element;
};

class PlainRenderer {
 public:
  virtual ~PlainRenderer() = default;

  auto Render(const std::string& markdown) -> std::vector<RenderedBlock>;
  virtual auto RenderCodeBlock(const std::string& code, const std::string& language) -> ftxui::Element;
  static auto RenderDiff(const std::string& diff_text) -> ftxui::Element;
};

}  // namespace ally::rendering
