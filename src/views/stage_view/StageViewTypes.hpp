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

// Re-export shared vim types into this namespace for existing code.
using ally::vim::InteractionMode;
using ally::vim::TextCursor;
using ally::vim::VisualModeState;

enum class PanelMode : std::uint8_t {
  Artifact,
  Chat,
};

enum class ArtifactViewMode : std::uint8_t {
  Rendered,
  Raw,
};

struct CachedPartRender {
  size_t content_length = 0;
  ftxui::Element element;
};

struct ChatPanelState {
  std::optional<std::string> session_id;
  std::vector<opencode::MessageWithParts> messages;
  size_t visible_count = 30;
  bool is_loading = false;
  std::optional<std::string> error_msg;
  bool has_artifact = false;
  std::unordered_map<std::string, int64_t> part_timestamps;
  std::unordered_set<std::string> expanded_parts;
  std::unordered_map<std::string, ftxui::Box> collapsible_boxes;
  std::unordered_map<std::string, CachedPartRender> rendered_parts_cache;
  int chat_scroll_y = INT_MAX;
  bool chat_follow = true;  // Auto-scroll to bottom on new content.
  uint64_t frame_count = 0;

  // Sub-agent streaming: accumulate text from sub-agent SSE events so we can
  // render live output inside the collapsible card while the sub-agent runs.
  std::unordered_map<std::string, std::string> subagent_msg_sessions;    // msg_id → sub-agent session_id
  std::unordered_map<std::string, std::string> subagent_streaming_text;  // session_id → accumulated text

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
  std::vector<int> message_screen_rows;       // content-y where each message starts
  std::vector<int> user_message_screen_rows;  // rows for user ($ input) messages only
  int content_height = 0;                     // total rendered content height
  int viewport_height = 0;                    // visible viewport height (from scrollable node)
};

struct StageViewState {
  std::mutex mtx;

  // Which panel is displayed.
  PanelMode panel = PanelMode::Artifact;

  // Vim-like interaction mode.
  InteractionMode interaction = InteractionMode::Normal;

  // Visual mode state (only valid when interaction == Visual).
  std::optional<VisualModeState> visual;

  // Artifact content for the current stage (loaded asynchronously).
  std::optional<std::string> review_content;
  std::vector<ftxui::Element> review_rendered;
  bool review_loading = false;

  // View mode for the artifact viewer.
  ArtifactViewMode review_view_mode = ArtifactViewMode::Rendered;

  // Stage slugs that have artifacts on disk (used for [ ] navigation).
  std::vector<std::string> artifact_stages;

  // All stage slugs from the workflow definition (used for ordering).
  std::vector<std::string> all_workflow_stages;

  // Scroll position for the artifact content in Review mode.
  int review_scroll_y = 0;

  // Chat panel state.
  ChatPanelState chat;
};

}  // namespace ally::views::detail
