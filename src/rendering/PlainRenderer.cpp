#include "PlainRenderer.hpp"

#include <cmark-gfm.h>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/flexbox_config.hpp>
#include <sstream>

using namespace ftxui;

namespace ally::rendering {

namespace {

// Split a string into word-level text() elements, preserving the gap between
// words so flexbox can reflow them. Each word becomes one Element.
void SplitWords(const std::string& str, const Decorator& style, Elements& out) {
  std::istringstream stream(str);
  std::string word;
  while (stream >> word) {
    out.push_back(text(word) | style);
  }
}

// Collect plain text from an inline node tree (strips formatting).
auto CollectText(cmark_node* node) -> std::string {
  std::string result;
  for (auto* child = cmark_node_first_child(node); child != nullptr; child = cmark_node_next(child)) {
    auto type = cmark_node_get_type(child);
    if (type == CMARK_NODE_TEXT || type == CMARK_NODE_CODE) {
      const auto* lit = cmark_node_get_literal(child);
      if (lit != nullptr) { result += lit;
}
    } else if (type == CMARK_NODE_SOFTBREAK || type == CMARK_NODE_LINEBREAK) {
      result += " ";
    } else {
      result += CollectText(child);
    }
  }
  return result;
}

// Collect inline content as word-level elements with styling, suitable for
// flexbox word-wrapping. Each word is a separate Element with the appropriate
// decorator (bold, dim, inverted, etc.).
void CollectInlineWords(cmark_node* node, Elements& words) {
  for (auto* child = cmark_node_first_child(node); child != nullptr; child = cmark_node_next(child)) {
    switch (cmark_node_get_type(child)) {
      case CMARK_NODE_TEXT: {
        const auto* lit = cmark_node_get_literal(child);
        if (lit != nullptr) { SplitWords(std::string(lit), nothing, words);
}
        break;
      }
      case CMARK_NODE_STRONG: {
        auto inner = CollectText(child);
        SplitWords(inner, bold, words);
        break;
      }
      case CMARK_NODE_EMPH: {
        auto inner = CollectText(child);
        SplitWords(inner, dim, words);
        break;
      }
      case CMARK_NODE_CODE: {
        const auto* lit = cmark_node_get_literal(child);
        if (lit != nullptr) { words.push_back(text(std::string(lit)) | inverted);
}
        break;
      }
      case CMARK_NODE_LINK: {
        auto inner = CollectText(child);
        words.push_back(text("[" + inner + "]") | underlined | color(Color::Blue));
        break;
      }
      case CMARK_NODE_SOFTBREAK:
      case CMARK_NODE_LINEBREAK:
        break;  // flexbox gap handles spacing
      default:
        CollectInlineWords(child, words);
        break;
    }
  }
}

// Render inline content as a flexbox that word-wraps.
auto RenderInlineWrapped(cmark_node* node) -> Element {
  static const auto config = FlexboxConfig().SetGap(1, 0);
  Elements words;
  CollectInlineWords(node, words);
  if (words.empty()) { return text("");
}
  return flexbox(std::move(words), config);
}

// Forward declaration — RenderBlock and RenderChildren are mutually recursive.
auto RenderBlock(cmark_node* node, PlainRenderer& renderer) -> Element;

// Render all children of a node as a vertical stack of blocks.
auto RenderChildren(cmark_node* node, PlainRenderer& renderer) -> Elements {
  Elements elems;
  for (auto* child = cmark_node_first_child(node); child != nullptr; child = cmark_node_next(child)) {
    elems.push_back(RenderBlock(child, renderer));
  }
  return elems;
}

// Render a single AST node (block-level or inline) into an FTXUI Element.
auto RenderBlock(cmark_node* node, PlainRenderer& renderer) -> Element {
  switch (cmark_node_get_type(node)) {
    case CMARK_NODE_DOCUMENT:
      return vbox(RenderChildren(node, renderer));

    case CMARK_NODE_PARAGRAPH:
      return RenderInlineWrapped(node);

    case CMARK_NODE_HEADING: {
      int level = cmark_node_get_heading_level(node);
      auto heading_text = CollectText(node);
      return text(std::string(level, '#') + " " + heading_text) | bold | color(Color::Cyan);
    }

    case CMARK_NODE_CODE_BLOCK: {
      const auto* fence = cmark_node_get_fence_info(node);
      auto lang = std::string((fence != nullptr) ? fence : "");
      const auto* lit = cmark_node_get_literal(node);
      auto code = std::string((lit != nullptr) ? lit : "");
      if (lang == "diff") {
        return renderer.RenderDiff(code);
      }
      return renderer.RenderCodeBlock(code, lang);
    }

    case CMARK_NODE_BLOCK_QUOTE: {
      Elements quoted;
      for (auto* child = cmark_node_first_child(node); child != nullptr; child = cmark_node_next(child)) {
        quoted.push_back(hbox(text("│ ") | color(Color::GrayDark), RenderBlock(child, renderer) | dim));
      }
      return vbox(std::move(quoted));
    }

    case CMARK_NODE_LIST: {
      Elements items;
      int index = 1;
      auto list_type = cmark_node_get_list_type(node);
      for (auto* item = cmark_node_first_child(node); item != nullptr; item = cmark_node_next(item)) {
        auto item_children = RenderChildren(item, renderer);
        std::string bullet;
        if (list_type == CMARK_ORDERED_LIST) {
          bullet = "  " + std::to_string(index++) + ". ";
        } else {
          bullet = "  • ";
        }
        if (!item_children.empty()) {
          Elements item_elems;
          item_elems.push_back(hbox(text(bullet) | color(Color::GrayLight), std::move(item_children[0])));
          for (size_t i = 1; i < item_children.size(); ++i) {
            item_elems.push_back(hbox(text(std::string(bullet.size(), ' ')), std::move(item_children[i])));
          }
          items.push_back(vbox(std::move(item_elems)));
        }
      }
      return vbox(std::move(items));
    }

    case CMARK_NODE_THEMATIC_BREAK:
      return separator();

    case CMARK_NODE_TEXT:
    case CMARK_NODE_STRONG:
    case CMARK_NODE_EMPH:
    case CMARK_NODE_CODE:
    case CMARK_NODE_LINK:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
      return RenderInlineWrapped(node);

    default:
      return vbox(RenderChildren(node, renderer));
  }
}

}  // namespace

auto PlainRenderer::Render(const std::string& markdown) -> std::vector<RenderedBlock> {
  cmark_node* doc = cmark_parse_document(markdown.c_str(), markdown.size(), CMARK_OPT_DEFAULT);

  std::vector<RenderedBlock> blocks;
  for (auto* node = cmark_node_first_child(doc); node != nullptr; node = cmark_node_next(node)) {
    auto type = cmark_node_get_type(node);
    RenderedBlock::Kind kind;
    switch (type) {
      case CMARK_NODE_HEADING:
        kind = RenderedBlock::Heading;
        break;
      case CMARK_NODE_CODE_BLOCK:
        kind = RenderedBlock::CodeBlock;
        break;
      case CMARK_NODE_BLOCK_QUOTE:
        kind = RenderedBlock::Quote;
        break;
      case CMARK_NODE_LIST:
        kind = RenderedBlock::List;
        break;
      default:
        kind = RenderedBlock::Paragraph;
        break;
    }
    blocks.push_back({kind, RenderBlock(node, *this)});
  }

  cmark_node_free(doc);
  return blocks;
}

auto PlainRenderer::RenderCodeBlock(const std::string& code, const std::string& /*lang*/) -> Element {
  Elements lines;
  std::istringstream stream(code);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(text(line));
  }
  return vbox(std::move(lines)) | border | color(Color::GrayLight);
}

auto PlainRenderer::RenderDiff(const std::string& diff_text) -> Element {
  Elements lines;
  std::istringstream stream(diff_text);
  std::string line;
  int lineno = 1;
  while (std::getline(stream, line)) {
    auto gutter = text(std::to_string(lineno++) + " ") | dim;
    Element content;
    if (!line.empty() && line[0] == '+') {
      content = text(line) | color(Color::Green);
    } else if (!line.empty() && line[0] == '-') {
      content = text(line) | color(Color::Red);
    } else if (!line.empty() && line[0] == '@') {
      content = text(line) | color(Color::Cyan);
    } else {
      content = text(line);
}
    lines.push_back(hbox(gutter, content));
  }
  return vbox(std::move(lines)) | border;
}

}  // namespace ally::rendering
