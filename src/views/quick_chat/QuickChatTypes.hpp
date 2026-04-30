#pragma once

#include <climits>
#include <cstdint>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/components/vim_mode/VimMode.hpp"
#include "src/opencode/Types.hpp"

namespace ally::views::detail {

using ally::vim::InteractionMode;
using ally::vim::TextCursor;
using ally::vim::VisualModeState;

struct QuickChatCachedPartRender {
  size_t content_length = 0;
  ftxui::Element element;
};

struct QuickChatPanelState {
  std::optional<std::string> session_id;
  std::vector<opencode::MessageWithParts> messages;
  size_t visible_count = 30;
  bool is_loading = false;
  std::optional<std::string> error_msg;
  std::unordered_map<std::string, int64_t> part_timestamps;
  std::unordered_set<std::string> expanded_parts;
  std::unordered_map<std::string, ftxui::Box> collapsible_boxes;
  std::unordered_map<std::string, QuickChatCachedPartRender> rendered_parts_cache;
  int chat_scroll_y = INT_MAX;
  bool chat_follow = true;
  uint64_t frame_count = 0;

  // Sub-agent streaming
  std::unordered_map<std::string, std::string> subagent_msg_sessions;
  std::unordered_map<std::string, std::string> subagent_streaming_text;

  // Model selection
  std::vector<opencode::ModelInfo> all_models;
  std::vector<opencode::ModelInfo> filtered_models;
  std::vector<std::string> model_dropdown_names;
  int selected_model_idx = 0;
  bool model_menu_open = false;
  std::optional<std::string> last_seen_provider;

  // Chat cursor for Normal mode navigation (screen-coordinate row/col).
  std::optional<TextCursor> chat_cursor;

  // Message boundary tracking (populated during rendering).
  std::vector<int> message_screen_rows;
  std::vector<int> user_message_screen_rows;  // rows for user ($ input) messages only
  int content_height = 0;
  int viewport_height = 0;

  // Question overlay state.
  std::optional<opencode::QuestionRequest> active_question;
  int question_idx = 0;                       // current question in multi-question flow
  int question_cursor = 0;                    // cursor position in options list
  std::unordered_set<int> question_selected;  // selected option indices (multi-select)
  std::string question_custom_text;           // custom text input
  bool question_custom_active = false;        // typing custom answer
};

struct QuickChatViewState {
  std::mutex mtx;

  // Vim-like interaction mode (starts in Insert to preserve current behavior).
  InteractionMode interaction = InteractionMode::Insert;

  // Visual mode state (only valid when interaction == Visual).
  std::optional<VisualModeState> visual;

  QuickChatPanelState chat;
};

}  // namespace ally::views::detail
