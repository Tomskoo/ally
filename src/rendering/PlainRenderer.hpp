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

  /// The FTXUI decorator applied to inline backtick-code spans. Defaults to
  /// `inverted` (a highlighted block). Subclasses with a theme can override
  /// to apply a theme-driven color instead.
  [[nodiscard]] virtual auto InlineCodeStyle() const -> ftxui::Decorator;
};

}  // namespace ally::rendering
