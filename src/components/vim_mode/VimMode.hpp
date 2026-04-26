#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

#include "src/configuration/InputConfig.hpp"

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
/// Returns the yanked text if 'y' was pressed, std::nullopt otherwise.
/// On exit (Escape/n) or yank, sets mode to Normal and clears visual state.
auto HandleVisualKeyEvent(VisualModeState& vs,
                          InteractionMode& mode,
                          const ftxui::Event& event,
                          const configuration::InputConfig& input_config) -> std::optional<std::string>;

}  // namespace ally::vim
