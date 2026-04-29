#include "VimMode.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

using namespace ftxui;

namespace ally::vim {

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

auto SplitLines(const std::string& s) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::istringstream stream(s);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(std::move(line));
  }
  if (lines.empty()) {
    lines.emplace_back();
  }
  return lines;
}

auto NormalizeSelection(const TextCursor& anchor, const TextCursor& cursor)
    -> std::pair<TextCursor, TextCursor> {
  if (anchor.row < cursor.row || (anchor.row == cursor.row && anchor.col <= cursor.col)) {
    return {anchor, cursor};
  }
  return {cursor, anchor};
}

auto InSelection(int row, int col, const TextCursor& start, const TextCursor& end) -> bool {
  if (row < start.row || row > end.row) { return false; }
  if (start.row == end.row) {
    return col >= start.col && col <= end.col;
  }
  if (row == start.row) { return col >= start.col; }
  if (row == end.row) { return col <= end.col; }
  return true;
}

auto ExtractSelection(const std::vector<std::string>& lines,
                      const TextCursor& anchor,
                      const TextCursor& cursor) -> std::string {
  auto [start, end] = NormalizeSelection(anchor, cursor);
  if (lines.empty()) { return ""; }

  std::string result;
  for (int row = start.row; row <= end.row && row < static_cast<int>(lines.size()); ++row) {
    const auto& line = lines[row];
    int col_begin = (row == start.row) ? start.col : 0;
    int col_end = (row == end.row) ? std::min(end.col + 1, static_cast<int>(line.size())) : static_cast<int>(line.size());
    if (col_begin < static_cast<int>(line.size())) {
      result += line.substr(col_begin, std::max(0, col_end - col_begin));
    }
    if (row < end.row) {
      result += '\n';
    }
  }
  return result;
}

void CopyToClipboard(const std::string& content) {
  FILE* pipe = popen("pbcopy", "w");
  if (pipe != nullptr) {
    fwrite(content.data(), 1, content.size(), pipe);
    pclose(pipe);
  }
}

// ---------------------------------------------------------------------------
// Shared event handling
// ---------------------------------------------------------------------------

auto HandleVisualKeyEvent(VisualModeState& vs,
                          InteractionMode& mode,
                          const Event& event,
                          const configuration::InputConfig& input_config) -> VisualYankResult {
  // Escape, 'n', or 'v' exits visual mode.
  if (input_config.vim.exit_visual.matches(event)) {
    mode = InteractionMode::Normal;
    return {};
  }

  // Clean yank (default 'y'): for chat, caller resolves from messages;
  // for non-chat (artifact), extract directly from lines.
  if (input_config.vim.yank.matches(event)) {
    mode = InteractionMode::Normal;
    if (vs.is_chat) {
      return {YankType::Clean, {}};
    }
    std::string selected = ExtractSelection(vs.lines, vs.anchor, vs.cursor);
    return selected.empty() ? VisualYankResult{} : VisualYankResult{YankType::Clean, std::move(selected)};
  }

  // Dirty yank: use screen-captured text (rendered pixels).
  if (input_config.vim.dirty_yank.matches(event)) {
    std::string selected = vs.screen_captured_text.empty()
                               ? ExtractSelection(vs.lines, vs.anchor, vs.cursor)
                               : vs.screen_captured_text;
    mode = InteractionMode::Normal;
    return selected.empty() ? VisualYankResult{} : VisualYankResult{YankType::Dirty, std::move(selected)};
  }

  // hjkl and arrow key cursor movement.
  bool is_left  = input_config.vim.left.matches(event);
  bool is_down  = input_config.vim.down.matches(event);
  bool is_up    = input_config.vim.up.matches(event);
  bool is_right = input_config.vim.right.matches(event);

  if (is_left || is_down || is_up || is_right) {
    auto max_col_for_row = [&](int row) -> int {
      if (vs.is_chat && vs.viewport_width > 0) {
        return std::max(0, vs.viewport_width - 1);
      }
      if (row >= 0 && row < static_cast<int>(vs.lines.size())) {
        return std::max(0, static_cast<int>(vs.lines[row].size()) - 1);
      }
      return 0;
    };

    if (is_left) {
      vs.cursor.col = std::max(0, vs.cursor.col - 1);
    } else if (is_right) {
      vs.cursor.col = std::min(vs.cursor.col + 1, max_col_for_row(vs.cursor.row));
    } else if (is_up) {
      vs.cursor.row = std::max(0, vs.cursor.row - 1);
      vs.cursor.col = std::min(vs.cursor.col, max_col_for_row(vs.cursor.row));
    } else if (is_down) {
      vs.cursor.row = std::min(vs.cursor.row + 1, std::max(0, static_cast<int>(vs.lines.size()) - 1));
      vs.cursor.col = std::min(vs.cursor.col, max_col_for_row(vs.cursor.row));
    }
    return {};
  }

  // Consume all other keys in visual mode.
  return {};
}

/// Extract text content from a single message part.
static auto ExtractPartText(const nlohmann::json& part) -> std::string {
  auto part_type = part.value("type", std::string{});

  // Text, reasoning, thinking parts: content is in "text" or "content".
  if (part.contains("text") && part["text"].is_string()) {
    return part["text"].get<std::string>();
  }
  if (part.contains("content") && part["content"].is_string()) {
    return part["content"].get<std::string>();
  }

  // Tool parts: rendered in a bordered box in the UI — wrap as fenced code block.
  if (part_type == "tool" && part.contains("state") && part["state"].is_object()) {
    const auto& tool_state = part["state"];
    std::string title;
    if (tool_state.contains("title") && tool_state["title"].is_string()) {
      title = tool_state["title"].get<std::string>();
    }

    // Collect the inner content lines (command/input + output).
    std::string inner;

    if (tool_state.contains("input") && tool_state["input"].is_object()) {
      const auto& input = tool_state["input"];
      std::string input_line;
      if (input.contains("command") && input["command"].is_string()) {
        input_line = input["command"].get<std::string>();
      } else if (input.contains("file_path") && input["file_path"].is_string()) {
        input_line = input["file_path"].get<std::string>();
      } else if (input.contains("filePath") && input["filePath"].is_string()) {
        input_line = input["filePath"].get<std::string>();
      } else if (input.contains("pattern") && input["pattern"].is_string()) {
        input_line = input["pattern"].get<std::string>();
      }
      if (!input_line.empty()) { inner = input_line; }
    }

    if (tool_state.contains("output") && tool_state["output"].is_string()) {
      auto output = tool_state["output"].get<std::string>();
      if (!output.empty()) {
        if (!inner.empty()) { inner += '\n'; }
        inner += output;
      }
    }

    // Wrap entire tool content (title + input + output) in a single code fence.
    std::string body;
    if (!title.empty()) { body = title; }
    if (!inner.empty()) {
      if (!body.empty()) { body += '\n'; }
      body += inner;
    }
    if (!body.empty()) {
      return "````\n" + body + "\n````";
    }
    return {};
  }

  return {};
}

auto ExtractCleanYankText(
    const std::vector<opencode::MessageWithParts>& messages,
    size_t visible_count,
    const std::vector<int>& msg_screen_rows,
    int content_height,
    int sel_start_row,
    int sel_end_row) -> std::string {
  size_t total = messages.size();
  size_t vis = std::min(visible_count, total);
  size_t first_visible = total - vis;

  bool debug = std::getenv("ALLY_DEBUG_YANK") != nullptr;
  if (debug) {
    fprintf(stderr, "[YANK] sel=[%d,%d] content_height=%d msgs=%zu vis=%zu first_visible=%zu screen_rows=%zu\n",
            sel_start_row, sel_end_row, content_height, total, vis, first_visible, msg_screen_rows.size());
    for (size_t i = 0; i < msg_screen_rows.size(); ++i) {
      fprintf(stderr, "[YANK]   msg_screen_rows[%zu] = %d\n", i, msg_screen_rows[i]);
    }
  }

  std::string result;
  for (size_t i = 0; i < msg_screen_rows.size(); ++i) {
    int msg_start = msg_screen_rows[i];
    int msg_end = (i + 1 < msg_screen_rows.size())
                      ? msg_screen_rows[i + 1] - 1
                      : content_height - 1;

    // Check overlap with selection.
    if (msg_end < sel_start_row || msg_start > sel_end_row) { continue; }

    size_t msg_idx = first_visible + i;
    if (msg_idx >= total) { continue; }

    if (debug) {
      fprintf(stderr, "[YANK]   MATCH msg[%zu] rows=[%d,%d] parts=%zu\n",
              msg_idx, msg_start, msg_end, messages[msg_idx].parts.size());
    }

    const auto& msg = messages[msg_idx];
    std::string msg_text;
    for (size_t pidx = 0; pidx < msg.parts.size(); ++pidx) {
      const auto& part = msg.parts[pidx];
      std::string part_text = ExtractPartText(part);
      if (debug) {
        auto ptype = part.value("type", std::string{"?"});
        fprintf(stderr, "[YANK]     part[%zu] type=%s extracted=%zu bytes keys=",
                pidx, ptype.c_str(), part_text.size());
        for (auto it = part.begin(); it != part.end(); ++it) {
          fprintf(stderr, "%s ", it.key().c_str());
        }
        fprintf(stderr, "\n");
      }
      if (!part_text.empty()) {
        if (!msg_text.empty()) { msg_text += '\n'; }
        msg_text += part_text;
      }
    }

    if (!msg_text.empty()) {
      if (!result.empty()) { result += "\n\n"; }
      result += msg_text;
    }
  }
  return result;
}

auto ComputeMessageRanges(
    const std::vector<int>& msg_screen_rows,
    int content_height,
    int sel_start_row,
    int sel_end_row) -> std::vector<std::pair<int, int>> {
  std::vector<std::pair<int, int>> ranges;
  for (size_t i = 0; i < msg_screen_rows.size(); ++i) {
    int msg_start = msg_screen_rows[i];
    int msg_end = (i + 1 < msg_screen_rows.size())
                      ? msg_screen_rows[i + 1] - 1
                      : content_height - 1;
    if (msg_end < sel_start_row || msg_start > sel_end_row) { continue; }
    ranges.emplace_back(msg_start, msg_end);
  }
  return ranges;
}

}  // namespace ally::vim
