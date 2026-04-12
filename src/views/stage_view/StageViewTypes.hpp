#pragma once

#include <climits>
#include <cstdint>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/opencode/Types.hpp"

namespace ally::views::detail {

enum class PanelMode : std::uint8_t {
  Artifact,
  Chat,
};

enum class InteractionMode : std::uint8_t {
  Normal,
  Visual,
  Insert,
};

enum class ArtifactViewMode : std::uint8_t {
  Rendered,
  Raw,
};

struct TextCursor {
  int row = 0;
  int col = 0;
};

struct VisualModeState {
  TextCursor anchor;
  TextCursor cursor;
  std::vector<std::string> lines;
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
  std::unordered_map<std::string, CachedPartRender> rendered_parts_cache;
  int chat_scroll_y = INT_MAX;
  bool chat_follow = true;  // Auto-scroll to bottom on new content.
  uint64_t frame_count = 0;
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
