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

#include "src/opencode/Types.hpp"

namespace ally::views::detail {

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
};

struct QuickChatViewState {
  std::mutex mtx;
  QuickChatPanelState chat;
};

}  // namespace ally::views::detail
