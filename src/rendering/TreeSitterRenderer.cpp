#include "TreeSitterRenderer.hpp"

#include <tree_sitter/api.h>

#include <cstring>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <regex>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace ftxui;

namespace ally::rendering {

namespace {

struct ColorSpan {
  uint32_t start;
  uint32_t end;
  ftxui::Color color;
};

// Convert a simple Lua pattern to a C++ regex. Returns empty string on failure.
auto LuaPatternToRegex(std::string_view lua_pat) -> std::string {
  std::string result;
  result.reserve(lua_pat.size() * 2);
  for (size_t i = 0; i < lua_pat.size(); ++i) {
    char ch = lua_pat[i];
    if (ch == '%' && i + 1 < lua_pat.size()) {
      ++i;
      char cls = lua_pat[i];
      switch (cls) {
        case 'u': result += "[A-Z]"; break;
        case 'l': result += "[a-z]"; break;
        case 'd': result += "[0-9]"; break;
        case 'a': result += "[a-zA-Z]"; break;
        case 'w': result += "[a-zA-Z0-9]"; break;
        case 's': result += "\\s"; break;
        case 'p': result += "[[:punct:]]"; break;
        // Escaped special characters
        case '.': case '^': case '$': case '*': case '+':
        case '-': case '?': case '(': case ')': case '[':
        case ']': case '%':
          result += '\\';
          result += cls;
          break;
        default:
          return "";  // unsupported Lua class
      }
    } else if (ch == '-') {
      // Lua's '-' is a non-greedy '*'
      result += "*?";
    } else {
      // Most regex metacharacters are the same in Lua and C++ regex
      result += ch;
    }
  }
  return result;
}

// Get a cached compiled regex, or compile and cache it.
auto GetCachedRegex(const std::string& pattern) -> const std::regex* {
  static std::unordered_map<std::string, std::regex> cache;
  auto iter = cache.find(pattern);
  if (iter != cache.end()) {
    return &iter->second;
  }
  try {
    auto [it, inserted] = cache.emplace(pattern, std::regex(pattern, std::regex::ECMAScript));
    return &it->second;
  } catch (const std::regex_error&) {
    return nullptr;
  }
}

// Extract the text of a captured node from the source code.
auto NodeText(const TSNode& node, const std::string& code) -> std::string_view {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (end > code.size()) { end = static_cast<uint32_t>(code.size()); }
  return std::string_view(code.data() + start, end - start);
}

// Find a capture node by its index in a match.
auto FindCaptureNode(const TSQueryMatch& match, uint32_t capture_id) -> const TSNode* {
  for (uint16_t i = 0; i < match.capture_count; ++i) {
    if (match.captures[i].index == capture_id) {
      return &match.captures[i].node;
    }
  }
  return nullptr;
}

// Evaluate all predicates for a match's pattern. Returns true if all pass.
auto EvaluatePredicates(const TSQuery* query, const TSQueryMatch& match,
                        const std::string& code) -> bool {
  uint32_t step_count = 0;
  const TSQueryPredicateStep* steps =
      ts_query_predicates_for_pattern(query, match.pattern_index, &step_count);

  if (step_count == 0) { return true; }

  uint32_t i = 0;
  while (i < step_count) {
    // First step is the predicate name (string type)
    if (steps[i].type != TSQueryPredicateStepTypeString) {
      // Skip to next Done
      while (i < step_count && steps[i].type != TSQueryPredicateStepTypeDone) { ++i; }
      ++i;
      continue;
    }

    uint32_t name_len = 0;
    const char* pred_name = ts_query_string_value_for_id(query, steps[i].value_id, &name_len);
    std::string_view name(pred_name, name_len);
    ++i;

    // Collect arguments (captures and strings) until Done
    struct Arg {
      bool is_capture;
      uint32_t id;  // capture index or string id
    };
    std::vector<Arg> args;
    while (i < step_count && steps[i].type != TSQueryPredicateStepTypeDone) {
      args.push_back({steps[i].type == TSQueryPredicateStepTypeCapture, steps[i].value_id});
      ++i;
    }
    ++i;  // skip Done

    // Evaluate the predicate
    if (name == "match?" || name == "vim-match?" || name == "not-match?" || name == "lua-match?" || name == "not-lua-match?") {
      if (args.size() < 2 || !args[0].is_capture || args[1].is_capture) { continue; }

      const TSNode* node = FindCaptureNode(match, args[0].id);
      if (node == nullptr) { return false; }
      auto node_txt = NodeText(*node, code);

      uint32_t pat_len = 0;
      const char* pat_raw = ts_query_string_value_for_id(query, args[1].id, &pat_len);
      std::string pattern(pat_raw, pat_len);

      bool is_lua = (name == "lua-match?" || name == "not-lua-match?");
      if (is_lua) {
        pattern = LuaPatternToRegex(pattern);
        if (pattern.empty()) { continue; }  // unsupported pattern, skip
      }

      const std::regex* re = GetCachedRegex(pattern);
      if (re == nullptr) { continue; }  // bad regex, skip

      bool matched = std::regex_search(node_txt.begin(), node_txt.end(), *re);
      bool negated = (name == "not-match?" || name == "not-lua-match?");
      if (negated == matched) { return false; }

    } else if (name == "any-of?") {
      if (args.empty() || !args[0].is_capture) { continue; }

      const TSNode* node = FindCaptureNode(match, args[0].id);
      if (node == nullptr) { return false; }
      auto node_txt = NodeText(*node, code);

      bool found = false;
      for (size_t a = 1; a < args.size(); ++a) {
        if (args[a].is_capture) { continue; }
        uint32_t str_len = 0;
        const char* str_val = ts_query_string_value_for_id(query, args[a].id, &str_len);
        if (node_txt == std::string_view(str_val, str_len)) {
          found = true;
          break;
        }
      }
      if (!found) { return false; }

    } else if (name == "eq?" || name == "not-eq?") {
      if (args.size() < 2 || !args[0].is_capture) { continue; }

      const TSNode* node = FindCaptureNode(match, args[0].id);
      if (node == nullptr) { return false; }
      auto node_txt = NodeText(*node, code);

      bool eq = false;
      if (args[1].is_capture) {
        // Compare two captures
        const TSNode* other = FindCaptureNode(match, args[1].id);
        if (other == nullptr) { return false; }
        eq = (node_txt == NodeText(*other, code));
      } else {
        // Compare capture to string literal
        uint32_t str_len = 0;
        const char* str_val = ts_query_string_value_for_id(query, args[1].id, &str_len);
        eq = (node_txt == std::string_view(str_val, str_len));
      }
      if ((name == "eq?") != eq) { return false; }

    } else if (name == "has-parent?" || name == "not-has-parent?") {
      if (args.empty() || !args[0].is_capture) { continue; }

      const TSNode* node = FindCaptureNode(match, args[0].id);
      if (node == nullptr) { return false; }
      TSNode parent = ts_node_parent(*node);

      bool found = false;
      if (!ts_node_is_null(parent)) {
        std::string_view parent_type(ts_node_type(parent));
        for (size_t a = 1; a < args.size(); ++a) {
          if (args[a].is_capture) { continue; }
          uint32_t str_len = 0;
          const char* str_val = ts_query_string_value_for_id(query, args[a].id, &str_len);
          if (parent_type == std::string_view(str_val, str_len)) {
            found = true;
            break;
          }
        }
      }
      bool want = (name == "has-parent?");
      if (found != want) { return false; }

    } else if (name == "has-ancestor?" || name == "not-has-ancestor?") {
      if (args.empty() || !args[0].is_capture) { continue; }

      const TSNode* node = FindCaptureNode(match, args[0].id);
      if (node == nullptr) { return false; }

      // Collect target type names
      std::vector<std::string_view> targets;
      for (size_t a = 1; a < args.size(); ++a) {
        if (args[a].is_capture) { continue; }
        uint32_t str_len = 0;
        const char* str_val = ts_query_string_value_for_id(query, args[a].id, &str_len);
        targets.emplace_back(str_val, str_len);
      }

      bool found = false;
      TSNode cur = ts_node_parent(*node);
      while (!ts_node_is_null(cur)) {
        std::string_view cur_type(ts_node_type(cur));
        for (const auto& target : targets) {
          if (cur_type == target) { found = true; break; }
        }
        if (found) { break; }
        cur = ts_node_parent(cur);
      }
      bool want = (name == "has-ancestor?");
      if (found != want) { return false; }
    }
    // Unknown predicates (e.g. #set!): pass (permissive fallback)
  }

  return true;
}

// Sentinel value for span boundary detection (must differ from any valid slot).
constexpr uint8_t kSpanEnd = 254;

// Run the cached highlight query and collect color spans.
auto RunHighlightQuery(CompiledLang& lang, TSNode root, const std::string& code) -> std::vector<ColorSpan> {
  if (lang.query == nullptr) { return {}; }

  std::vector<uint8_t> byte_slots(code.size(), 0);
  std::vector<int> byte_priority(code.size(), 0);

  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, lang.query, root);

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    if (!EvaluatePredicates(lang.query, match, code)) {
      continue;
    }
    int priority = (match.pattern_index < lang.pattern_priority.size())
                       ? lang.pattern_priority[match.pattern_index]
                       : kDefaultPriority;
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture& cap = match.captures[i];
      uint8_t slot = lang.capture_to_slot[cap.index];
      if (slot == kSkipSlot) { continue; }
      uint32_t start = ts_node_start_byte(cap.node);
      uint32_t end = ts_node_end_byte(cap.node);
      uint32_t clamp_end = std::min(end, static_cast<uint32_t>(code.size()));
      for (uint32_t b = start; b < clamp_end; ++b) {
        if (priority >= byte_priority[b]) {
          byte_slots[b] = slot;
          byte_priority[b] = priority;
        }
      }
    }
  }

  ts_query_cursor_delete(cursor);

  std::vector<ColorSpan> spans;
  if (code.empty()) { return spans; }

  uint32_t span_start = 0;
  uint8_t span_slot = byte_slots[0];
  for (uint32_t i = 1; i <= code.size(); ++i) {
    uint8_t cur = (i < code.size()) ? byte_slots[i] : kSpanEnd;
    if (cur != span_slot) {
      spans.push_back({span_start, i, lang.slot_colors[span_slot]});
      span_start = i;
      span_slot = cur;
    }
  }

  return spans;
}

}  // namespace

TreeSitterRenderer::TreeSitterRenderer(HighlightTheme theme, std::vector<std::filesystem::path> query_dirs)
    : theme_(theme), store_(std::move(theme), std::move(query_dirs)) {}

auto TreeSitterRenderer::InlineCodeStyle() const -> Decorator {
  if (auto col = theme_.TryResolve("markup.raw")) {
    return color(*col);
  }
  return inverted;
}

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
      if (line.empty()) {
        current_line.push_back(text(" "));
      } else {
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
