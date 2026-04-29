#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

#include "src/configuration/InputConfig.hpp"
#include "src/opencode/Types.hpp"

namespace ally::vim {

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

enum class InteractionMode : std::uint8_t {
  Normal,
  Visual,
  Insert,
};

struct TextCursor {
  int row = 0;
  int col = 0;
};

struct VisualModeState {
  TextCursor anchor;
  TextCursor cursor;
  std::vector<std::string> lines;
  bool is_chat = false;  // true when visual mode is over chat content

  // For chat mode: viewport width for column bounds (since lines are placeholders).
  int viewport_width = 0;

  // Text captured from the rendered screen during the last render pass.
  // When available, yank uses this instead of ExtractSelection so the copied
  // text matches exactly what was highlighted on screen.
  std::string screen_captured_text;
};

enum class YankType : std::uint8_t {
  None,
  Clean,  // extract original markdown source
  Dirty,  // screen-captured pixels
};

struct VisualYankResult {
  YankType type = YankType::None;
  std::string text;  // pre-filled for dirty yank and non-chat clean yank;
                     // empty for chat clean yank (caller resolves from messages)
};

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

auto SplitLines(const std::string& s) -> std::vector<std::string>;

auto NormalizeSelection(const TextCursor& anchor, const TextCursor& cursor)
    -> std::pair<TextCursor, TextCursor>;

auto InSelection(int row, int col, const TextCursor& start, const TextCursor& end) -> bool;

auto ExtractSelection(const std::vector<std::string>& lines,
                      const TextCursor& anchor,
                      const TextCursor& cursor) -> std::string;

void CopyToClipboard(const std::string& content);

// ---------------------------------------------------------------------------
// Shared event handling
// ---------------------------------------------------------------------------

/// Handle a key event in Visual mode.
/// Returns a YankResult indicating what action was taken.
/// On exit (Escape/n) or yank, sets mode to Normal.
auto HandleVisualKeyEvent(VisualModeState& vs,
                          InteractionMode& mode,
                          const ftxui::Event& event,
                          const configuration::InputConfig& input_config) -> VisualYankResult;

/// Extract original markdown source text for messages whose rendered rows
/// overlap the selection [sel_start_row, sel_end_row].
auto ExtractCleanYankText(
    const std::vector<opencode::MessageWithParts>& messages,
    size_t visible_count,
    const std::vector<int>& msg_screen_rows,
    int content_height,
    int sel_start_row,
    int sel_end_row) -> std::string;

/// Compute message row ranges that overlap the selection, for visual feedback.
/// Returns (start_row, end_row) pairs in content-relative coordinates.
auto ComputeMessageRanges(
    const std::vector<int>& msg_screen_rows,
    int content_height,
    int sel_start_row,
    int sel_end_row) -> std::vector<std::pair<int, int>>;

}  // namespace ally::vim
