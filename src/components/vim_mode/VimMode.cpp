#include "VimMode.hpp"

#include <algorithm>
#include <cstdio>
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
                          const Event& event) -> std::optional<std::string> {
  // Escape, 'n', or 'v' exits visual mode.
  if (event == Event::Escape || event == Event::Character('n') || event == Event::Character('v')) {
    mode = InteractionMode::Normal;
    return std::nullopt;
  }

  // 'y' yanks selection to clipboard.
  if (event == Event::Character('y')) {
    auto selected = ExtractSelection(vs.lines, vs.anchor, vs.cursor);
    mode = InteractionMode::Normal;
    if (!selected.empty()) {
      return selected;
    }
    return std::nullopt;
  }

  // hjkl and arrow key cursor movement.
  bool is_left  = event == Event::Character('h') || event == Event::ArrowLeft;
  bool is_down  = event == Event::Character('j') || event == Event::ArrowDown;
  bool is_up    = event == Event::Character('k') || event == Event::ArrowUp;
  bool is_right = event == Event::Character('l') || event == Event::ArrowRight;

  if (is_left || is_down || is_up || is_right) {
    if (is_left) {
      vs.cursor.col = std::max(0, vs.cursor.col - 1);
    } else if (is_right) {
      int max_col = vs.cursor.row < static_cast<int>(vs.lines.size())
                        ? std::max(0, static_cast<int>(vs.lines[vs.cursor.row].size()) - 1)
                        : 0;
      vs.cursor.col = std::min(vs.cursor.col + 1, max_col);
    } else if (is_up) {
      vs.cursor.row = std::max(0, vs.cursor.row - 1);
      int max_col = vs.cursor.row < static_cast<int>(vs.lines.size())
                        ? std::max(0, static_cast<int>(vs.lines[vs.cursor.row].size()) - 1)
                        : 0;
      vs.cursor.col = std::min(vs.cursor.col, max_col);
    } else if (is_down) {
      vs.cursor.row = std::min(vs.cursor.row + 1, std::max(0, static_cast<int>(vs.lines.size()) - 1));
      int max_col = vs.cursor.row < static_cast<int>(vs.lines.size())
                        ? std::max(0, static_cast<int>(vs.lines[vs.cursor.row].size()) - 1)
                        : 0;
      vs.cursor.col = std::min(vs.cursor.col, max_col);
    }
    return std::nullopt;
  }

  // Consume all other keys in visual mode.
  return std::nullopt;
}

}  // namespace ally::vim
