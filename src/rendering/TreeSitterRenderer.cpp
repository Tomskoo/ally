#include "TreeSitterRenderer.hpp"

#include <tree_sitter/api.h>

#include <cstring>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <sstream>
#include <vector>

using namespace ftxui;

namespace ally::rendering {

namespace {

struct ColorSpan {
  uint32_t start;
  uint32_t end;
  ftxui::Color color;
};

// Run the cached highlight query and collect color spans.
auto RunHighlightQuery(CompiledLang& lang, TSNode root, const std::string& code) -> std::vector<ColorSpan> {
  if (lang.query == nullptr) { return {};
}

  std::vector<uint8_t> byte_slots(code.size(), 0);

  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, lang.query, root);

  TSQueryMatch match;
  uint32_t capture_index = 0;
  while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
    const TSQueryCapture& cap = match.captures[capture_index];
    uint32_t start = ts_node_start_byte(cap.node);
    uint32_t end = ts_node_end_byte(cap.node);
    uint8_t slot = lang.capture_to_slot[cap.index];

    uint32_t clamp_end = (end < code.size()) ? end : static_cast<uint32_t>(code.size());
    std::memset(byte_slots.data() + start, slot, clamp_end - start);
  }

  ts_query_cursor_delete(cursor);

  std::vector<ColorSpan> spans;
  if (code.empty()) { return spans;
}

  uint32_t span_start = 0;
  uint8_t span_slot = byte_slots[0];
  for (uint32_t i = 1; i <= code.size(); ++i) {
    uint8_t cur = (i < code.size()) ? byte_slots[i] : 255;
    if (cur != span_slot) {
      spans.push_back({span_start, i, lang.slot_colors[span_slot]});
      span_start = i;
      span_slot = cur;
    }
  }

  return spans;
}

}  // namespace

TreeSitterRenderer::TreeSitterRenderer(HighlightTheme theme) : store_(std::move(theme)) {}

auto TreeSitterRenderer::RenderCodeBlock(const std::string& code, const std::string& language) -> Element {
  auto* lang = store_.Get(language);
  if (lang == nullptr) {
    return PlainRenderer::RenderCodeBlock(code, language);
  }

  // RAII wrappers for tree-sitter objects
  auto parser = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>(ts_parser_new(), ts_parser_delete);
  ts_parser_set_language(parser.get(), lang->language);

  auto tree = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>(
      ts_parser_parse_string(parser.get(), nullptr, code.c_str(), static_cast<uint32_t>(code.size())), ts_tree_delete);
  TSNode root = ts_tree_root_node(tree.get());

  auto spans = RunHighlightQuery(*lang, root, code);

  auto default_color = store_.Get(language)->slot_colors[0];

  // Build colored elements line by line
  Elements lines;
  Elements current_line;

  auto flush_text = [&](const std::string& txt, ftxui::Color col) -> void {
    std::istringstream stream(txt);
    std::string line;
    bool first = true;
    while (std::getline(stream, line)) {
      if (!first) {
        lines.push_back(hbox(std::move(current_line)));
        current_line.clear();
      }
      if (!line.empty()) {
        current_line.push_back(text(line) | color(col));
      }
      first = false;
    }
    if (!txt.empty() && txt.back() == '\n') {
      lines.push_back(hbox(std::move(current_line)));
      current_line.clear();
    }
  };

  size_t pos = 0;
  for (const auto& span : spans) {
    if (span.start > pos) {
      flush_text(code.substr(pos, span.start - pos), default_color);
    }
    flush_text(code.substr(span.start, span.end - span.start), span.color);
    pos = span.end;
  }
  if (pos < code.size()) {
    flush_text(code.substr(pos), default_color);
  }
  if (!current_line.empty()) {
    lines.push_back(hbox(std::move(current_line)));
  }

  return vbox(std::move(lines)) | border;
}

}  // namespace ally::rendering
