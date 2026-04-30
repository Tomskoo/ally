#include "StageView.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

#include "StageViewTypes.hpp"
#include "src/app/AppContext.hpp"
#include "src/app/NavState.hpp"
#include "src/app/Navigator.hpp"
#include "src/commands/storage/Storage.hpp"
#include "src/components/autocomplete/ArtifactAutocomplete.hpp"
#include "src/components/autocomplete/ArtifactTypes.hpp"
#include "src/components/autocomplete/FileAutocomplete.hpp"
#include "src/components/autocomplete/CommandAutocomplete.hpp"
#include "src/components/autocomplete/CommandTypes.hpp"
#include "src/components/autocomplete/Types.hpp"
#include "src/components/scrollable/ScrollableNode.hpp"
#include "src/components/vim_mode/CursorOverlayNode.hpp"
#include "src/components/vim_mode/VimMode.hpp"
#include "src/opencode/Service.hpp"
#include "src/rendering/HighlightTheme.hpp"
#include "src/rendering/TreeSitterRenderer.hpp"
#include "src/services/CommandService.hpp"
#include "src/utils/time_format.hpp"

using namespace ftxui;
using ally::components::make_scrollable;
using ally::components::reflect_layout;

namespace ally::views {

namespace {

constexpr int kScrollLines = 3;

using detail::ArtifactViewMode;
using detail::ChatPanelState;
using detail::InteractionMode;
using detail::PanelMode;
using detail::StageViewState;
using detail::TextCursor;
using detail::VisualModeState;

using ally::vim::CopyToClipboard;
using ally::vim::ExtractSelection;
using ally::vim::InSelection;
using ally::vim::NormalizeSelection;
using ally::vim::SplitLines;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// SplitLines, NormalizeSelection, InSelection, ExtractSelection, CopyToClipboard
// are now in src/components/vim_mode/VimMode.hpp (ally::vim namespace).

// Extract raw code from Read tool output, stripping <content> tags and line-number prefixes.
auto ExtractFileContent(const std::string& output) -> std::string {
  auto content_start = output.find("<content>");
  auto content_end = output.rfind("</content>");
  if (content_start == std::string::npos || content_end == std::string::npos) { return "";
}
  content_start += 9;  // strlen("<content>")
  auto raw = output.substr(content_start, content_end - content_start);

  // Strip line-number prefixes ("1: ", "10: ", etc.) and the trailing "(End of file ...)" line.
  std::string result;
  std::istringstream stream(raw);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find("(End of file") == 0) { continue;
}
    // Strip leading "N: " prefix.
    auto colon = line.find(": ");
    if (colon != std::string::npos && colon <= 6) {
      bool all_digits = true;
      for (size_t i = 0; i < colon; ++i) {
        if (line[i] < '0' || line[i] > '9') { all_digits = false; break;
}
      }
      if (all_digits) {
        line = line.substr(colon + 2);
      }
    }
    if (!result.empty()) { result += '\n';
}
    result += line;
  }
  return result;
}

// ---------------------------------------------------------------------------
// Chat helpers (free functions)
// ---------------------------------------------------------------------------

void StampPart(StageViewState& state, const std::string& msg_id, size_t part_idx) {
  // Caller must hold state.mtx.
  auto key = msg_id + ":" + std::to_string(part_idx);
  if (state.chat.part_timestamps.count(key) == 0) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    state.chat.part_timestamps[key] = now;
  }
}

auto GetOrCreateMessage(StageViewState& state, const std::string& msg_id) -> auto& {
  // Caller must hold state.mtx.
  for (auto& msg : state.chat.messages) {
    if (msg.info.id == msg_id) {
      return msg;
    }
  }
  // Purge optimistic messages.
  auto& msgs = state.chat.messages;
  msgs.erase(std::remove_if(msgs.begin(), msgs.end(),
                            [](const opencode::MessageWithParts& msg) -> bool { return msg.info.id.rfind("optimistic-", 0) == 0; }),
             msgs.end());
  // Create new entry — default to assistant role since user messages are
  // always created locally as optimistic messages in DoSend().
  opencode::MessageWithParts new_msg;
  new_msg.info.id = msg_id;
  new_msg.info.extra = nlohmann::json::object();
  new_msg.info.extra["role"] = "assistant";
  state.chat.messages.push_back(std::move(new_msg));
  return state.chat.messages.back();
}

void MergeMessages(StageViewState& state, std::vector<opencode::MessageWithParts> fetched) {
  // Build lookup of existing messages by ID.
  std::unordered_map<std::string, size_t> existing_index;
  for (size_t i = 0; i < state.chat.messages.size(); ++i) {
    existing_index[state.chat.messages[i].info.id] = i;
  }

  // Use the fetched list as canonical ordering.
  std::unordered_set<std::string> fetched_ids;
  std::vector<opencode::MessageWithParts> merged;
  merged.reserve(fetched.size() + 2);

  for (auto& fetched_msg : fetched) {
    fetched_ids.insert(fetched_msg.info.id);
    auto it = existing_index.find(fetched_msg.info.id);
    if (it != existing_index.end()) {
      auto& existing_msg = state.chat.messages[it->second];
      // Always take metadata from the fetched version (has role, etc.).
      existing_msg.info.extra = fetched_msg.info.extra;
      // Keep in-memory parts if they are ahead of the HTTP response
      // (streaming deltas may have added content not yet persisted).
      if (existing_msg.parts.size() > fetched_msg.parts.size()) {
        merged.push_back(std::move(existing_msg));
      } else {
        merged.push_back(std::move(fetched_msg));
      }
    } else {
      merged.push_back(std::move(fetched_msg));
    }
  }

  // Keep any in-memory-only non-optimistic messages (may not be persisted yet).
  for (auto& existing_msg : state.chat.messages) {
    if (fetched_ids.count(existing_msg.info.id) == 0 &&
        existing_msg.info.id.rfind("optimistic-", 0) != 0) {
      merged.push_back(std::move(existing_msg));
    }
  }

  state.chat.messages = std::move(merged);
  state.chat.rendered_parts_cache.clear();
}

void RefreshMessages(const std::shared_ptr<StageViewState>& state, opencode::OpenCodeState& oc_state, std::shared_mutex& oc_mutex,
                     ScreenInteractive& screen) {
  std::string sid;
  {
    std::scoped_lock lock(state->mtx);
    if (!state->chat.session_id) { return;
}
    sid = *state->chat.session_id;
  }
  auto result = opencode::ListMessages(oc_state, oc_mutex, sid);
  if (opencode::is_ok(result)) {
    std::scoped_lock lock(state->mtx);
    MergeMessages(*state, std::move(opencode::get_value(std::move(result))));
    for (size_t idx = 0; idx < state->chat.messages.size(); ++idx) {
      for (size_t pidx = 0; pidx < state->chat.messages[idx].parts.size(); ++pidx) {
        StampPart(*state, state->chat.messages[idx].info.id, pidx);
      }
    }
    if (!state->chat.messages.empty()) {
      auto& last = state->chat.messages.back();
      if (last.info.extra.contains("role") && last.info.extra["role"] == "assistant") {
        state->chat.is_loading = false;
      }
    }
  }
  screen.PostEvent(Event::Custom);
}

void ApplyPartDelta(const std::shared_ptr<StageViewState>& state, const nlohmann::json& data) {
  std::scoped_lock lock(state->mtx);
  auto props = data.value("properties", nlohmann::json::object());
  auto msg_id = props.value("messageID", "");
  auto part_id = props.value("partID", "");
  auto delta = props.value("delta", "");
  auto field = props.value("field", "text");

  if (msg_id.empty()) { return;
}

  auto& msg = GetOrCreateMessage(*state, msg_id);

  // Find part by partID.
  int part_idx = -1;
  for (size_t idx = 0; idx < msg.parts.size(); ++idx) {
    if (msg.parts[idx].value("id", "") == part_id) {
      part_idx = static_cast<int>(idx);
      break;
    }
  }
  if (part_idx < 0) {
    nlohmann::json new_part;
    new_part["id"] = part_id;
    new_part["type"] = "text";
    new_part[field] = "";
    msg.parts.push_back(std::move(new_part));
    part_idx = static_cast<int>(msg.parts.size() - 1);
  }

  auto& part = msg.parts[part_idx];
  auto current = part.value(field, "");
  part[field] = current + delta;

  // Auto-scroll to bottom while streaming (only if user hasn't scrolled away).
  if (state->chat.chat_follow) {
    state->chat.chat_scroll_y = INT_MAX;
  }

  StampPart(*state, msg_id, static_cast<size_t>(part_idx));
}

void ApplyPartUpdated(const std::shared_ptr<StageViewState>& state, const nlohmann::json& data) {
  auto props = data.value("properties", nlohmann::json::object());
  // The part object is nested inside properties.part.
  if (!props.contains("part") || !props["part"].is_object()) { return;
}
  auto part_json = props["part"];

  auto part_type =
      part_json.contains("type") && part_json["type"].is_string() ? part_json["type"].get<std::string>() : std::string{};

  // Filter non-renderable types.
  if (part_type == "step-start" || part_type == "step-finish") { return;
}

  auto msg_id = part_json.contains("messageID") && part_json["messageID"].is_string() ? part_json["messageID"].get<std::string>()
                                                                                      : std::string{};
  auto part_id = part_json.contains("id") && part_json["id"].is_string() ? part_json["id"].get<std::string>() : std::string{};
  if (msg_id.empty()) { return;
}

  std::scoped_lock lock(state->mtx);
  auto& msg = GetOrCreateMessage(*state, msg_id);

  // Find and replace, or append.
  for (auto& existing : msg.parts) {
    if (existing.value("id", "") == part_id) {
      existing = part_json;
      return;
    }
  }
  msg.parts.push_back(part_json);
  StampPart(*state, msg_id, msg.parts.size() - 1);
}

auto FormatTimestamp(int64_t epoch_ms) -> std::string { return ally::utils::format_time_hms(epoch_ms); }

// ---------------------------------------------------------------------------

struct StageViewImpl {
  // Immutable after construction
  std::string task_id;
  std::string thread_id;
  std::string stage;  // the "active" stage (chat session target)
  AppContext& ctx;
  Navigator& nav;
  ScreenInteractive& screen;
  rendering::HighlightTheme theme;

  // State (protected by state->mtx)
  std::shared_ptr<StageViewState> state;

  // Child components
  Component input_component;
  std::string input_text;
  int cursor_pos = 0;

  // Autocomplete state
  std::shared_ptr<autocomplete::CommandAutocompleteState> command_ac_state = std::make_shared<autocomplete::CommandAutocompleteState>();
  std::shared_ptr<autocomplete::ArtifactAutocompleteState> artifact_ac_state =
      std::make_shared<autocomplete::ArtifactAutocompleteState>();
  std::shared_ptr<autocomplete::AutocompleteState> file_ac_state = std::make_shared<autocomplete::AutocompleteState>();

  // Autocomplete overlay components
  Component skill_ac_component;
  Component artifact_ac_component;
  Component file_ac_component;

  // Skill service (must outlive the skills listener thread)
  std::shared_ptr<services::CommandService> command_service;

  // Model selector components
  Component model_button;
  Component model_menu;

  // Cursor overlays (live on the struct so the pointer survives through render).
  std::optional<ally::vim::CursorOverlay> chat_overlay_;
  std::optional<ally::vim::CursorOverlay> artifact_overlay_;

  // Message boundary tracking for J/K jumps (populated during render).
  std::vector<ftxui::Box> msg_reflect_boxes_;
  std::vector<bool> msg_is_user_;
  ftxui::Box chat_body_box_;
  ftxui::Box artifact_body_box_;

  // Background threads
  std::atomic<bool> stop{false};
  std::thread artifact_event_thread;

  ~StageViewImpl() {
    stop.store(true, std::memory_order_relaxed);
    command_ac_state->listener_stop.store(true, std::memory_order_relaxed);
    artifact_ac_state->listener_stop.store(true, std::memory_order_relaxed);
    if (artifact_event_thread.joinable()) {
      artifact_event_thread.join();
    }
  }

  StageViewImpl(const StageViewImpl&) = delete;
  auto operator=(const StageViewImpl&) -> StageViewImpl& = delete;
  StageViewImpl(StageViewImpl&&) = delete;
  auto operator=(StageViewImpl&&) -> StageViewImpl& = delete;

  StageViewImpl(AppContext& ctx, Navigator& nav, ScreenInteractive& screen, std::string task_id, std::string thread_id,
                std::string stage)
      : task_id(std::move(task_id)),
        thread_id(std::move(thread_id)),
        stage(std::move(stage)),
        ctx(ctx),
        nav(nav),
        screen(screen),
        theme(rendering::HighlightTheme::LoadDefault(ctx.theme_name)),
        state(std::make_shared<StageViewState>()) {}

  // -- Render dispatch --------------------------------------------------------

  auto Render() -> Element {
    PanelMode panel;
    InteractionMode interaction;
    {
      std::scoped_lock lock(state->mtx);
      panel = state->panel;
      interaction = state->interaction;
    }

    // Check if visual mode is over chat content.
    bool visual_is_chat = false;
    {
      std::scoped_lock lock(state->mtx);
      if (state->visual.has_value()) {
        visual_is_chat = state->visual->is_chat;
      }
    }

    Element content;
    if (interaction == InteractionMode::Visual && visual_is_chat) {
      content = RenderChatWithOverlay(true);
    } else if (interaction == InteractionMode::Visual) {
      content = RenderArtifactWithOverlay(true);
    } else if (interaction == InteractionMode::Normal && panel == PanelMode::Chat) {
      content = RenderChatWithOverlay(false);
    } else if (interaction == InteractionMode::Normal && panel == PanelMode::Artifact) {
      content = RenderArtifactWithOverlay(false);
    } else if (panel == PanelMode::Artifact) {
      content = RenderArtifactPanel();
    } else {
      content = RenderChatPanel();
    }

    return dbox({
        content,
        vbox({
            filler(),
            hbox({filler(), RenderModelMenu()}),
            emptyElement() | size(HEIGHT, EQUAL, InputBarHeight()),
        }),
    });
  }

  auto RenderArtifactPanel() -> Element {
    bool loading;
    ArtifactViewMode view_mode;
    std::optional<std::string> raw_content;
    std::vector<Element> rendered;
    {
      std::scoped_lock lock(state->mtx);
      loading = state->review_loading;
      view_mode = state->review_view_mode;
      raw_content = state->review_content;
      rendered = state->review_rendered;
    }

    Element content_body;
    if (loading) {
      content_body = text("  Loading...") | dim | flex;
    } else if (view_mode == ArtifactViewMode::Raw && raw_content.has_value()) {
      Elements raw_lines;
      std::istringstream stream(*raw_content);
      std::string line;
      while (std::getline(stream, line)) {
        raw_lines.push_back(text(line.empty() ? " " : line));
      }
      content_body = vbox(std::move(raw_lines)) | flex;
    } else if (!rendered.empty()) {
      content_body = vbox(std::move(rendered)) | flex;
    } else {
      content_body = text("  No artifact for this stage.") | dim | flex;
    }

    return dbox({
        vbox({
            make_scrollable(content_body | vscroll_indicator, &state->review_scroll_y) | flex,
            separator(),
            RenderInputBar(),
        }),
        vbox({
            filler(),
            RenderAutocompleteOverlay(),
            emptyElement() | size(HEIGHT, EQUAL, InputBarHeight()),
        }),
    });
  }

  auto RenderToolResultPart(const std::string& msg_id, size_t part_idx, const std::string& content) -> Element {
    constexpr int kCollapseThreshold = 8;
    constexpr int kCollapsePreviewLines = 4;

    auto lines = SplitLines(content);
    auto key = msg_id + ":" + std::to_string(part_idx);
    bool expanded = state->chat.expanded_parts.count(key) > 0;

    if (static_cast<int>(lines.size()) <= kCollapseThreshold || expanded) {
      return paragraph(content) | dim;
    }

    std::string preview;
    for (int idx = 0; idx < kCollapsePreviewLines && idx < static_cast<int>(lines.size()); ++idx) {
      if (idx > 0) { preview += "\n";
}
      preview += lines[idx];
    }
    int remaining = static_cast<int>(lines.size()) - kCollapsePreviewLines;
    return vbox({
        paragraph(preview) | dim,
        text("  ... " + std::to_string(remaining) + " more lines") | dim,
    }) | reflect(state->chat.collapsible_boxes[key]);
  }

  static auto FormatDuration(double seconds) -> std::string {
    char buf[32];
    if (seconds < 60.0) {
      std::snprintf(buf, sizeof(buf), "%.1fs", seconds);
    } else {
      int mins = static_cast<int>(seconds) / 60;
      double secs = seconds - mins * 60;
      std::snprintf(buf, sizeof(buf), "%dm %.1fs", mins, secs);
    }
    return buf;
  }

  // Extract summary from sub-agent output: content between <task_result> tags,
  // or the first non-empty line of the output, truncated to max_len.
  static auto ExtractSubAgentSummary(const std::string& output, size_t max_len = 100) -> std::string {
    // Try <task_result>...</task_result> first.
    constexpr std::string_view kOpen = "<task_result>";
    constexpr std::string_view kClose = "</task_result>";
    auto start = output.find(kOpen);
    if (start != std::string::npos) {
      start += kOpen.size();
      auto end = output.find(kClose, start);
      if (end != std::string::npos) {
        // Grab the first non-empty line inside the tags.
        auto block = output.substr(start, end - start);
        std::istringstream ss(block);
        std::string line;
        while (std::getline(ss, line)) {
          auto pos = line.find_first_not_of(" \t\r\n");
          if (pos != std::string::npos) {
            line = line.substr(pos);
            if (line.size() > max_len) { line = line.substr(0, max_len) + "\u2026"; }
            return line;
          }
        }
      }
    }

    // Fallback: first non-empty line of raw output.
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
      auto pos = line.find_first_not_of(" \t\r\n");
      if (pos != std::string::npos) {
        line = line.substr(pos);
        if (line.size() > max_len) { line = line.substr(0, max_len) + "\u2026"; }
        return line;
      }
    }
    return "";
  }

  auto RenderSubAgentPart(const std::string& msg_id, size_t part_idx, const nlohmann::json& part) -> Element {
    auto tool_state = part.value("state", nlohmann::json::object());
    auto input = tool_state.value("input", nlohmann::json::object());
    auto title = tool_state.value("title", "");
    auto status = tool_state.value("status", "");
    auto output = tool_state.value("output", "");

    // While streaming, state.output is empty — use accumulated SSE text instead.
    auto metadata = tool_state.value("metadata", nlohmann::json::object());
    auto sub_session_id = metadata.value("sessionId", "");
    if (output.empty() && !sub_session_id.empty()) {
      auto it = state->chat.subagent_streaming_text.find(sub_session_id);
      if (it != state->chat.subagent_streaming_text.end()) {
        output = it->second;
      }
    }

    auto subagent_type = input.value("subagent_type", "task");
    auto description = input.value("description", title);

    // Capitalize first letter.
    std::string type_label = subagent_type;
    if (!type_label.empty()) {
      type_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(type_label[0])));
    }

    bool is_complete = (status == "completed");
    auto base_key = "subagent:" + msg_id + ":" + std::to_string(part_idx);

    // Collapsed by default; click toggles expand/collapse in any state.
    bool expanded = state->chat.expanded_parts.count(base_key) > 0;

    // Cache key encodes expand state so both versions are cached independently.
    auto cache_key = base_key + (expanded ? ":e" : ":c");
    auto& cached = state->chat.rendered_parts_cache[cache_key];
    size_t cache_sig = output.size() + (is_complete ? 1 : 0);
    if (cached.element && cached.content_length == cache_sig) {
      // Use base_key for click target so both cached versions share the same box.
      return cached.element | reflect(state->chat.collapsible_boxes[base_key]);
    }

    // Chevron indicator.
    std::string chevron = expanded ? "\u25BE " : "\u25B8 ";

    // Duration from time.start/end.
    Elements meta_parts;
    auto time_obj = tool_state.value("time", nlohmann::json::object());
    if (time_obj.contains("start") && time_obj.contains("end")) {
      double duration_s =
          static_cast<double>(time_obj["end"].get<int64_t>() - time_obj["start"].get<int64_t>()) / 1000.0;
      meta_parts.push_back(text(FormatDuration(duration_s)) | dim);
    }
    if (!is_complete) {
      meta_parts.push_back(text("Running...") | color(Color::Yellow));
    }

    Element header = hbox({
        text(chevron) | color(Color::Blue),
        text(type_label + " Task") | bold,
        text(" \u2014 ") | dim,
        text(description) | flex,
        filler(),
        hbox(std::move(meta_parts)),
    });

    Elements card_els;
    card_els.push_back(header);

    if (expanded && !output.empty()) {
      // Render the sub-agent output as markdown.
      rendering::TreeSitterRenderer renderer(theme, ctx.query_dirs);
      auto blocks = renderer.Render(output);
      Elements block_els;
      for (auto& block : blocks) {
        block_els.push_back(std::move(block.element));
      }
      card_els.push_back(vbox(std::move(block_els)) | color(Color::GrayLight) | xflex);
    } else if (!expanded && !output.empty()) {
      // Collapsed summary line.
      auto summary = ExtractSubAgentSummary(output);
      if (!summary.empty()) {
        card_els.push_back(text(summary) | dim);
      }
    } else if (!is_complete && output.empty()) {
      card_els.push_back(text("  Waiting for output...") | dim);
    }

    // Blue bar spans the full card height via a separator column in an hbox.
    auto el = hbox({
        separator() | color(Color::Blue),
        text(" "),
        vbox(std::move(card_els)) | flex,
    });

    cached = {cache_sig, el};
    return el | reflect(state->chat.collapsible_boxes[base_key]);
  }

  auto RenderQuestionToolPart(const nlohmann::json& part) -> Element {
    auto tool_state = part.value("state", nlohmann::json::object());
    auto status = tool_state.value("status", "");
    auto output = tool_state.value("output", "");
    auto input = tool_state.value("input", nlohmann::json::object());

    Elements els;

    std::string question_text;
    if (input.contains("questions") && input["questions"].is_array() && !input["questions"].empty()) {
      auto& first_q = input["questions"][0];
      question_text = first_q.value("question", "");
    } else if (input.contains("question") && input["question"].is_string()) {
      question_text = input["question"].get<std::string>();
    }

    els.push_back(text("# question") | bold);
    if (!question_text.empty()) {
      els.push_back(text(question_text));
    }

    if (status == "completed") {
      if (!output.empty()) {
        els.push_back(text("  Answer: " + output) | color(Color::Green));
      }
    } else {
      els.push_back(text("  Awaiting response...") | color(Color::Yellow));
    }

    return vbox(std::move(els)) | border | color(Color::Yellow) | xflex;
  }

  auto RenderToolPart(const std::string& msg_id, size_t part_idx, const nlohmann::json& part) -> Element {
    auto tool_name = part.value("tool", "tool");

    // Sub-agent tools get collapsible card rendering.
    if (tool_name == "task") {
      return RenderSubAgentPart(msg_id, part_idx, part);
    }

    if (tool_name == "question") {
      return RenderQuestionToolPart(part);
    }

    auto tool_state = part.value("state", nlohmann::json::object());
    auto title = tool_state.value("title", "");
    auto status = tool_state.value("status", "");
    auto output = tool_state.value("output", "");
    auto input = tool_state.value("input", nlohmann::json::object());

    auto base_key = msg_id + ":" + std::to_string(part_idx);
    bool expanded = !output.empty() && state->chat.expanded_parts.count(base_key) > 0;
    auto cache_key = base_key + (expanded ? ":e" : ":c");
    auto& cached = state->chat.rendered_parts_cache[cache_key];
    if (cached.element && cached.content_length == output.size()) {
      // Still need reflect for click tracking even on cache hit.
      if (!output.empty()) {
        return cached.element | reflect(state->chat.collapsible_boxes[base_key]);
      }
      return cached.element;
    }

    // Header: title or tool name.
    std::string header_text = title.empty() ? tool_name : title;
    Elements els;
    els.push_back(text("# " + header_text) | bold);

    // Show command/input if available.
    if (input.contains("command") && input["command"].is_string()) {
      els.push_back(text("$ " + input["command"].get<std::string>()) | color(Color::GrayLight));
    } else if (input.contains("file_path") && input["file_path"].is_string()) {
      els.push_back(text("$ " + input["file_path"].get<std::string>()) | color(Color::GrayLight));
    } else if (input.contains("pattern") && input["pattern"].is_string()) {
      els.push_back(text("$ " + input["pattern"].get<std::string>()) | color(Color::GrayLight));
    }

    // Show tool output as collapsible content.
    if (!output.empty()) {

      // Detect file path for syntax highlighting.
      std::string file_path;
      if (input.contains("filePath") && input["filePath"].is_string()) {
        file_path = input["filePath"].get<std::string>();
      } else if (input.contains("file_path") && input["file_path"].is_string()) {
        file_path = input["file_path"].get<std::string>();
      }
      auto lang = rendering::QueryStore::LanguageFromPath(file_path);
      // Always try to extract file content to strip XML tags, even without a known language.
      auto clean_code = !file_path.empty() ? ExtractFileContent(output) : std::string{};

      constexpr int kCollapseThreshold = 8;
      constexpr int kCollapsePreviewLines = 4;
      auto lines = SplitLines(!clean_code.empty() ? clean_code : output);

      if (static_cast<int>(lines.size()) <= kCollapseThreshold || expanded) {
        if (!clean_code.empty() && !lang.empty()) {
          rendering::TreeSitterRenderer renderer(theme, ctx.query_dirs);
          els.push_back(renderer.RenderCodeBlock(clean_code, lang));
        } else {
          els.push_back(paragraph(!clean_code.empty() ? clean_code : output));
        }
      } else {
        std::string preview;
        for (int idx = 0; idx < kCollapsePreviewLines && idx < static_cast<int>(lines.size()); ++idx) {
          if (idx > 0) { preview += "\n"; }
          preview += lines[idx];
        }
        if (!clean_code.empty() && !lang.empty()) {
          rendering::TreeSitterRenderer renderer(theme, ctx.query_dirs);
          els.push_back(renderer.RenderCodeBlock(preview, lang));
        } else {
          els.push_back(paragraph(preview));
        }
        els.push_back(text("\u2026") | dim);
        els.push_back(text("Click to expand") | dim);
      }

      // Cache the rendered element and register a clickable box for expand/collapse toggle.
      auto el = vbox(std::move(els)) | border | color(Color::GrayLight) | xflex;
      cached = {output.size(), el};
      return el | reflect(state->chat.collapsible_boxes[base_key]);
    }

    if (status != "completed") {
      els.push_back(text("  Running...") | dim);
    }

    auto el = vbox(std::move(els)) | border | color(Color::GrayLight) | xflex;
    cached = {output.size(), el};
    return el;
  }

  auto RenderMessage(const opencode::MessageWithParts& msg) -> Element {
    std::string role = (msg.info.extra.is_object() && msg.info.extra.contains("role") && msg.info.extra["role"].is_string())
                           ? msg.info.extra["role"].get<std::string>()
                           : std::string{"unknown"};
    std::string icon;
    if (role == "user") {
      icon = "$ ";
    } else {
      icon = "  ";
    }

    Elements parts_elements;
    for (size_t pidx = 0; pidx < msg.parts.size(); ++pidx) {
      const auto& part = msg.parts[pidx];
      auto part_type = part.contains("type") && part["type"].is_string() ? part["type"].get<std::string>() : std::string{};

      if (part_type == "step-start" || part_type == "step-finish") { continue;
}

      std::string content;
      if (part.contains("text") && part["text"].is_string()) {
        content = part["text"].get<std::string>();
      } else if (part.contains("content") && part["content"].is_string()) {
        content = part["content"].get<std::string>();
      }

      Element part_el;
      if (part_type == "text") {
        auto cache_key = msg.info.id + ":" + std::to_string(pidx);
        auto& cached = state->chat.rendered_parts_cache[cache_key];
        if (cached.element && cached.content_length == content.size()) {
          part_el = cached.element;
        } else {
          rendering::TreeSitterRenderer renderer(theme, ctx.query_dirs);
          auto blocks = renderer.Render(content);
          Elements block_els;
          for (auto& block : blocks) {
            block_els.push_back(std::move(block.element));
          }
          part_el = vbox(std::move(block_els));
          cached = {content.size(), part_el};
        }
      } else if (part_type == "tool") {
        part_el = RenderToolPart(msg.info.id, pidx, part);
      } else if (part_type == "tool-call") {
        auto tool_name =
            (part.contains("name") && part["name"].is_string()) ? part["name"].get<std::string>() : std::string{"tool"};
        part_el = text("> " + tool_name) | color(Color::Blue);
      } else if (part_type == "tool-result") {
        part_el = RenderToolResultPart(msg.info.id, pidx, content);
      } else if (part_type == "reasoning" || part_type == "thinking") {
        part_el = paragraph(content) | dim;
      } else {
        part_el = paragraph(content);
      }

      // Tool parts take the full width — no icon prefix, filler, or timestamp.
      if (part_type == "tool" || part_type == "tool-result") {
        parts_elements.push_back(hbox({
            text(icon) | dim,
            part_el | flex,
        }));
      } else {
        // Timestamp.
        auto ts_key = msg.info.id + ":" + std::to_string(pidx);
        Element timestamp_el = emptyElement();
        if (state->chat.part_timestamps.count(ts_key) != 0) {
          timestamp_el = text(FormatTimestamp(state->chat.part_timestamps[ts_key])) | dim;
        }

        parts_elements.push_back(hbox({
            text(icon) | dim,
            part_el | flex,
            filler(),
            timestamp_el,
        }));
      }
    }

    // If no renderable parts, show empty message with icon.
    if (parts_elements.empty()) {
      parts_elements.push_back(text(icon) | dim);
    }

    return vbox(std::move(parts_elements));
  }

  auto RenderChatPanel() -> Element {
    // Snapshot chat state under the lock, then release before calling
    // RenderInputBar / RenderAutocompleteOverlay (which also lock mtx).
    Elements message_elements;
    {
      std::scoped_lock lock(state->mtx);
      state->chat.frame_count++;

      auto& msgs = state->chat.messages;
      size_t total = msgs.size();
      size_t visible = std::min(total, state->chat.visible_count);
      size_t start_idx = total - visible;

      // "Load earlier" indicator.
      if (start_idx > 0) {
        size_t remaining = start_idx;
        size_t load_count = std::min(remaining, static_cast<size_t>(30));
        message_elements.push_back(text("  Load " + std::to_string(load_count) + " earlier messages") | dim | bold);
        message_elements.push_back(text(""));
      }

      // Render each visible message.
      for (size_t idx = start_idx; idx < total; ++idx) {
        if (idx > start_idx) {
          message_elements.push_back(text(""));
        }
        message_elements.push_back(RenderMessage(msgs[idx]));
      }

      // Empty state.
      if (msgs.empty() && !state->chat.is_loading) {
        message_elements.push_back(text("  No messages yet. Send a message to start the conversation.") | dim);
      }

      // Loading indicator.
      if (state->chat.is_loading) {
        const auto *label = (state->chat.frame_count % 2 == 0) ? "Thinking..." : "Thinking. . .";
        message_elements.push_back(text("  " + std::string(label)) | dim);
      }

      // Error bar.
      if (state->chat.error_msg) {
        message_elements.push_back(text("  Error: " + *state->chat.error_msg) | color(Color::Red));
      }
    }

    auto chat_body = vbox(std::move(message_elements));

    return dbox({
        vbox({
            make_scrollable(chat_body | vscroll_indicator, &state->chat.chat_scroll_y, &state->chat.viewport_height, &state->chat.content_height) | flex,
            separator(),
            RenderInputBar(),
        }),
        vbox({
            filler(),
            RenderAutocompleteOverlay(),
            emptyElement() | size(HEIGHT, EQUAL, InputBarHeight()),
        }),
        RenderQuestionOverlay(),
    });
  }

  /// Render chat panel with cursor overlay (Normal or Visual mode).
  /// When `is_visual` is true, renders selection highlight; otherwise just cursor.
  auto RenderChatWithOverlay(bool is_visual) -> Element {
    // Build the same chat content as RenderChatPanel, but with message
    // boundary tracking and a cursor/selection overlay.
    Elements message_elements;
    {
      std::scoped_lock lock(state->mtx);
      state->chat.frame_count++;

      auto& msgs = state->chat.messages;
      size_t total = msgs.size();
      size_t visible = std::min(total, state->chat.visible_count);
      size_t start_idx = total - visible;

      if (start_idx > 0) {
        size_t remaining = start_idx;
        size_t load_count = std::min(remaining, static_cast<size_t>(30));
        message_elements.push_back(text("  Load " + std::to_string(load_count) + " earlier messages") | dim | bold);
        message_elements.push_back(text(""));
      }

      msg_reflect_boxes_.resize(total - start_idx);
      msg_is_user_.resize(total - start_idx);
      size_t box_idx = 0;
      for (size_t idx = start_idx; idx < total; ++idx) {
        if (idx > start_idx) {
          message_elements.push_back(text(""));
        }
        auto& extra = msgs[idx].info.extra;
        msg_is_user_[box_idx] = extra.is_object() && extra.contains("role") &&
                                extra["role"].is_string() && extra["role"].get<std::string>() == "user";
        message_elements.push_back(RenderMessage(msgs[idx]) | reflect_layout(msg_reflect_boxes_[box_idx]));
        box_idx++;
      }

      if (msgs.empty() && !state->chat.is_loading) {
        message_elements.push_back(text("  No messages yet. Send a message to start the conversation.") | dim);
      }

      if (state->chat.is_loading) {
        const auto *label = (state->chat.frame_count % 2 == 0) ? "Thinking..." : "Thinking. . .";
        message_elements.push_back(text("  " + std::string(label)) | dim);
      }

      if (state->chat.error_msg) {
        message_elements.push_back(text("  Error: " + *state->chat.error_msg) | color(Color::Red));
      }
    }

    auto chat_body = vbox(std::move(message_elements)) | reflect_layout(chat_body_box_);

    // Build the cursor overlay state.
    chat_overlay_ = std::nullopt;
    {
      std::scoped_lock lock(state->mtx);
      if (is_visual && state->visual.has_value()) {
        auto [sel_s, sel_e] = NormalizeSelection(state->visual->anchor, state->visual->cursor);
        auto msg_ranges = ally::vim::ComputeMessageRanges(
            state->chat.message_screen_rows, state->chat.content_height,
            sel_s.row, sel_e.row);
        chat_overlay_ = ally::vim::CursorOverlay{
            ally::vim::CursorOverlay::Mode::Selection,
            state->visual->cursor.row, state->visual->cursor.col,
            sel_s.row, sel_s.col, sel_e.row, sel_e.col,
            &state->visual->screen_captured_text,
            std::move(msg_ranges),
        };
      } else if (state->chat.chat_cursor.has_value()) {
        chat_overlay_ = ally::vim::CursorOverlay{
            ally::vim::CursorOverlay::Mode::Cursor,
            state->chat.chat_cursor->row, state->chat.chat_cursor->col,
            0, 0, 0, 0,
        };
      }
    }

    const ally::vim::CursorOverlay* overlay_ptr = chat_overlay_.has_value() ? &*chat_overlay_ : nullptr;
    auto overlaid = ally::vim::make_cursor_overlay(chat_body, overlay_ptr);
    auto wrapped = overlaid | vscroll_indicator;

    return dbox({
        vbox({
            make_scrollable(wrapped, &state->chat.chat_scroll_y, &state->chat.viewport_height, &state->chat.content_height) | flex,
            separator(),
            RenderInputBar(),
        }),
        vbox({
            filler(),
            RenderAutocompleteOverlay(),
            emptyElement() | size(HEIGHT, EQUAL, InputBarHeight()),
        }),
        RenderQuestionOverlay(),
    });
  }

  /// Render artifact panel with cursor overlay (Normal or Visual mode).
  /// Mirrors the RenderChatWithOverlay approach: render the formatted content,
  /// overlay the cursor/selection on top, and use screen capture for yanking.
  auto RenderArtifactWithOverlay(bool is_visual) -> Element {
    bool loading;
    ArtifactViewMode view_mode;
    std::optional<std::string> raw_content;
    std::vector<Element> rendered;
    {
      std::scoped_lock lock(state->mtx);
      loading = state->review_loading;
      view_mode = state->review_view_mode;
      raw_content = state->review_content;
      rendered = state->review_rendered;
    }

    Element content_body;
    if (loading) {
      content_body = text("  Loading...") | dim | flex;
    } else if (view_mode == ArtifactViewMode::Raw && raw_content.has_value()) {
      Elements raw_lines;
      std::istringstream stream(*raw_content);
      std::string line;
      while (std::getline(stream, line)) {
        raw_lines.push_back(text(line.empty() ? " " : line));
      }
      content_body = vbox(std::move(raw_lines)) | flex;
    } else if (!rendered.empty()) {
      content_body = vbox(std::move(rendered)) | flex;
    } else {
      content_body = text("  No artifact for this stage.") | dim | flex;
    }

    // Build the cursor overlay state (same pattern as RenderChatWithOverlay).
    artifact_overlay_ = std::nullopt;
    {
      std::scoped_lock lock(state->mtx);
      if (is_visual && state->visual.has_value()) {
        auto [sel_s, sel_e] = NormalizeSelection(state->visual->anchor, state->visual->cursor);
        artifact_overlay_ = ally::vim::CursorOverlay{
            ally::vim::CursorOverlay::Mode::Selection,
            state->visual->cursor.row, state->visual->cursor.col,
            sel_s.row, sel_s.col, sel_e.row, sel_e.col,
            &state->visual->screen_captured_text,
        };
      } else if (state->artifact_cursor.has_value()) {
        artifact_overlay_ = ally::vim::CursorOverlay{
            ally::vim::CursorOverlay::Mode::Cursor,
            state->artifact_cursor->row, state->artifact_cursor->col,
            0, 0, 0, 0,
        };
      }
    }

    auto tracked_body = content_body | reflect_layout(artifact_body_box_);

    const ally::vim::CursorOverlay* overlay_ptr = artifact_overlay_.has_value() ? &*artifact_overlay_ : nullptr;
    auto overlaid = ally::vim::make_cursor_overlay(tracked_body, overlay_ptr);

    return dbox({
        vbox({
            make_scrollable(overlaid | vscroll_indicator, &state->review_scroll_y,
                            &state->review_viewport_height, &state->review_content_height) | flex,
            separator(),
            RenderInputBar(),
        }),
        vbox({
            filler(),
            RenderAutocompleteOverlay(),
            emptyElement() | size(HEIGHT, EQUAL, InputBarHeight()),
        }),
    });
  }

  auto RenderVisualMode() -> Element {
    std::optional<VisualModeState> vs;
    {
      std::scoped_lock lock(state->mtx);
      vs = state->visual;
    }

    if (!vs.has_value() || vs->lines.empty()) {
      return vbox({
          text("  No content to select.") | dim | flex,
          separator(),
          RenderInputBar(),
      });
    }

    auto [sel_start, sel_end] = NormalizeSelection(vs->anchor, vs->cursor);

    Elements line_elements;
    for (int row = 0; row < static_cast<int>(vs->lines.size()); ++row) {
      const auto& line = vs->lines[row];

      if (line.empty()) {
        // Empty line: show cursor if on this row.
        if (row == vs->cursor.row) {
          line_elements.push_back(text(" ") | inverted);
        } else {
          line_elements.push_back(text(" "));
        }
        continue;
      }

      // Batch contiguous spans with the same selection state.
      Elements spans;
      int col = 0;
      while (col < static_cast<int>(line.size())) {
        bool selected = InSelection(row, col, sel_start, sel_end);
        int span_start = col;
        while (col < static_cast<int>(line.size()) && InSelection(row, col, sel_start, sel_end) == selected) {
          ++col;
        }
        auto span = text(line.substr(span_start, col - span_start));
        if (selected) {
          span = span | inverted;
        }
        spans.push_back(std::move(span));
      }

      // Cursor at end of line.
      if (row == vs->cursor.row && vs->cursor.col >= static_cast<int>(line.size())) {
        spans.push_back(text(" ") | inverted);
      }

      line_elements.push_back(hbox(std::move(spans)));
    }

    auto content_body = vbox(std::move(line_elements)) | flex;

    return vbox({
        make_scrollable(content_body | vscroll_indicator, &state->review_scroll_y) | flex,
        separator(),
        RenderInputBar(),
    });
  }

  auto InputBarHeight() -> int {
    // +1 for the separator above the input bar
    int input_lines = 1 + static_cast<int>(std::count(input_text.begin(), input_text.end(), '\n'));
    return 1 + std::max(1, input_lines);
  }

  auto RenderInputBar() -> Element {
    InteractionMode interaction;
    {
      std::scoped_lock lock(state->mtx);
      interaction = state->interaction;
    }

    auto mode_label = [&]() -> Element {
      switch (interaction) {
        case InteractionMode::Normal:
          return text(" NORMAL ") | bold | dim;
        case InteractionMode::Visual:
          return text(" VISUAL ") | bold | inverted;
        case InteractionMode::Insert:
          return text(" INSERT ") | bold | color(Color::Green);
      }
      return text("");
    }();

    auto prompt_label = (interaction == InteractionMode::Insert) ? (text(" $ ") | bold | color(Color::White)) : (text(" $ ") | bold | dim);

    auto send_label = !input_text.empty() ? (text(" M-⏎ Send ") | bold | color(Color::Green)) : (text(" M-⏎ Send ") | dim);

    // Only render the live input component (with cursor) in Insert mode.
    // In Normal/Visual mode, show the text without a cursor.
    auto input_el = (interaction == InteractionMode::Insert) ? input_component->Render()
                                                             : text(input_text.empty() ? "type a message..." : input_text) | dim;

    // Combine prompt label with the input so they share the same flex area,
    // avoiding a separate column gap on multi-line input.
    auto input_with_prompt = hbox({prompt_label, input_el | flex});

    auto model_el = RenderModelSelector();

    auto sep = [&]() -> Element {
      return interaction == InteractionMode::Insert ? separatorStyled(LIGHT) | color(Color::Green)
                                                    : separator();
    };

    return hbox({
        vbox({mode_label, filler()}),
        sep(),
        input_with_prompt | flex,
        sep(),
        vbox({model_el, filler()}),
        sep(),
        vbox({send_label, filler()}),
    });
  }

  int last_model_idx = -1;

  auto RenderModelSelector() -> Element {
    CheckProviderChanged();

    std::string provider_id;
    {
      std::shared_lock plock(ctx.provider_mutex);
      provider_id = ctx.selected_provider.value_or("");
    }

    bool has_models = false;
    {
      std::scoped_lock lock(state->mtx);
      has_models = !state->chat.model_dropdown_names.empty();

      int current_idx = state->chat.selected_model_idx;
      if (has_models && current_idx != last_model_idx && current_idx >= 0 &&
          current_idx < static_cast<int>(state->chat.filtered_models.size())) {
        last_model_idx = current_idx;
        auto model_id = state->chat.filtered_models[current_idx].id;
        auto root = ctx.project_root;
        std::thread([root, provider_id, model_id]() -> void {
          commands::storage::SetModelForProvider(root, provider_id, model_id);
        }).detach();
      }
    }

    if (!has_models) {
      return text(" no models ") | dim;
    }
    return model_button->Render();
  }

  auto RenderModelMenu() -> Element {
    std::scoped_lock lock(state->mtx);
    if (!state->chat.model_menu_open || state->chat.model_dropdown_names.empty()) {
      return emptyElement();
    }
    return model_menu->Render() | border;
  }

  auto RenderAutocompleteOverlay() const -> Element {
    auto overlay = skill_ac_component->Render();
    if (artifact_ac_state->is_open) {
      overlay = artifact_ac_component->Render();
    } else if (file_ac_state->is_open) {
      overlay = file_ac_component->Render();
    }
    return overlay | clear_under;
  }

  auto RenderQuestionOverlay() -> Element {
    std::scoped_lock lock(state->mtx);
    if (!state->chat.active_question) { return emptyElement(); }

    auto& q = *state->chat.active_question;
    if (state->chat.question_idx >= static_cast<int>(q.questions.size())) { return emptyElement(); }
    auto& item = q.questions[state->chat.question_idx];

    Elements els;

    if (!item.header.empty()) {
      els.push_back(text(" " + item.header + " ") | bold | color(Color::Yellow));
    }
    els.push_back(text(item.question));
    if (item.multiple) {
      els.push_back(text("  (select multiple, Space to toggle)") | dim);
    }
    els.push_back(separator());

    for (int i = 0; i < static_cast<int>(item.options.size()); ++i) {
      bool selected = state->chat.question_selected.count(i) > 0;
      bool focused = (i == state->chat.question_cursor && !state->chat.question_custom_active);
      std::string indicator = item.multiple ? (selected ? "[x] " : "[ ] ") : (selected ? "(o) " : "( ) ");

      Elements line_els;
      line_els.push_back(text(indicator + item.options[i].label));
      if (!item.options[i].description.empty()) {
        line_els.push_back(text(" - " + item.options[i].description) | dim);
      }
      auto line = hbox(std::move(line_els));
      if (focused) { line = line | inverted; }
      els.push_back(line);
    }

    if (item.custom) {
      bool focused = state->chat.question_custom_active;
      auto custom_line = hbox({
          text(focused ? "> " : "  "),
          text(state->chat.question_custom_text + (focused ? "_" : "")),
      });
      if (focused) { custom_line = custom_line | inverted; }
      els.push_back(custom_line);
    }

    els.push_back(separator());
    Elements hints;
    hints.push_back(text(" Enter:submit ") | dim);
    if (item.multiple) { hints.push_back(text(" Space:toggle ") | dim); }
    if (item.custom) { hints.push_back(text(" Tab:custom ") | dim); }
    hints.push_back(text(" Esc:dismiss ") | dim);
    if (q.questions.size() > 1) {
      hints.push_back(text(" (" + std::to_string(state->chat.question_idx + 1) + "/" + std::to_string(q.questions.size()) + ") ") | dim);
    }
    els.push_back(hbox(std::move(hints)));

    auto box = vbox(std::move(els)) | border | color(Color::Yellow) | xflex;

    return vbox({
        filler(),
        box,
        emptyElement() | size(HEIGHT, EQUAL, InputBarHeight()),
    });
  }

  // -- Scroll-follow-cursor ----------------------------------------------------

  /// Adjusts review_scroll_y so the artifact cursor row is visible.
  /// Caller must hold state->mtx.
  void ScrollArtifactToCursor() {
    if (!state->artifact_cursor.has_value()) { return; }
    int cursor_row = state->artifact_cursor->row;
    int scroll_y = state->review_scroll_y;
    int viewport_h = state->review_viewport_height;
    if (viewport_h <= 0) { return; }

    if (cursor_row < scroll_y) {
      state->review_scroll_y = cursor_row;
    } else if (cursor_row >= scroll_y + viewport_h) {
      state->review_scroll_y = cursor_row - viewport_h + 1;
    }
  }

  /// Adjusts chat_scroll_y so the cursor row is within the visible viewport.
  /// Caller must hold state->mtx.
  void ScrollToCursor() {
    if (!state->chat.chat_cursor.has_value()) { return; }
    int cursor_row = state->chat.chat_cursor->row;
    int scroll_y = state->chat.chat_scroll_y;
    int viewport_h = state->chat.viewport_height;
    if (viewport_h <= 0) { return; }

    if (cursor_row < scroll_y) {
      state->chat.chat_scroll_y = cursor_row;
    } else if (cursor_row >= scroll_y + viewport_h) {
      state->chat.chat_scroll_y = cursor_row - viewport_h + 1;
    }
  }

  // -- Message boundary tracking -----------------------------------------------

  void UpdateMessageBoundaries() {
    std::scoped_lock lock(state->mtx);
    state->chat.message_screen_rows.clear();
    state->chat.user_message_screen_rows.clear();

    // Use the chat body's top edge as origin so boundary rows are in the same
    // content-relative coordinate space as the cursor (row 0 = top of content).
    int origin_y = chat_body_box_.y_min;

    for (size_t i = 0; i < msg_reflect_boxes_.size(); ++i) {
      int content_row = msg_reflect_boxes_[i].y_min - origin_y;
      state->chat.message_screen_rows.push_back(content_row);
      if (i < msg_is_user_.size() && msg_is_user_[i]) {
        state->chat.user_message_screen_rows.push_back(content_row);
      }
    }
  }

  // -- Event handling ---------------------------------------------------------

  // -- Question event handling --------------------------------------------------

  void SubmitQuestionAnswers() {
    std::scoped_lock lock(state->mtx);
    if (!state->chat.active_question) { return; }

    auto& q = *state->chat.active_question;
    auto& item = q.questions[state->chat.question_idx];

    std::vector<std::string> answer;
    if (state->chat.question_custom_active && !state->chat.question_custom_text.empty()) {
      answer.push_back(state->chat.question_custom_text);
    } else if (item.multiple) {
      for (int idx : state->chat.question_selected) {
        if (idx >= 0 && idx < static_cast<int>(item.options.size())) {
          answer.push_back(item.options[idx].label);
        }
      }
    } else {
      if (state->chat.question_cursor >= 0 && state->chat.question_cursor < static_cast<int>(item.options.size())) {
        answer.push_back(item.options[state->chat.question_cursor].label);
      }
    }

    if (!q.extra.contains("_answers") || !q.extra["_answers"].is_array()) {
      q.extra["_answers"] = nlohmann::json::array();
    }
    q.extra["_answers"].push_back(answer);

    if (state->chat.question_idx + 1 < static_cast<int>(q.questions.size())) {
      state->chat.question_idx++;
      state->chat.question_cursor = 0;
      state->chat.question_selected.clear();
      state->chat.question_custom_text.clear();
      state->chat.question_custom_active = false;
    } else {
      auto request_id = q.id;
      auto all_answers = q.extra["_answers"].get<std::vector<std::vector<std::string>>>();
      state->chat.active_question = std::nullopt;

      auto sptr = state;
      auto& ocs = ctx.opencode_state;
      auto& ocm = ctx.opencode_mutex;
      auto& scr = screen;
      std::thread([sptr, &ocs, &ocm, &scr, request_id, all_answers]() -> void {
        opencode::QuestionReplyRequest req;
        req.answers = all_answers;
        opencode::ReplyQuestion(ocs, ocm, request_id, req);
        scr.PostEvent(Event::Custom);
      }).detach();
    }
  }

  void RejectActiveQuestion() {
    std::string request_id;
    {
      std::scoped_lock lock(state->mtx);
      if (!state->chat.active_question) { return; }
      request_id = state->chat.active_question->id;
      state->chat.active_question = std::nullopt;
    }

    auto& ocs = ctx.opencode_state;
    auto& ocm = ctx.opencode_mutex;
    auto& scr = screen;
    std::thread([&ocs, &ocm, &scr, request_id]() -> void {
      opencode::RejectQuestion(ocs, ocm, request_id);
      scr.PostEvent(Event::Custom);
    }).detach();
  }

  auto HandleQuestionEvent(Event& event) -> bool {
    if (event == Event::ArrowUp || event == Event::Character("k")) {
      std::scoped_lock lock(state->mtx);
      if (state->chat.question_custom_active) {
        state->chat.question_custom_active = false;
        auto& item = state->chat.active_question->questions[state->chat.question_idx];
        state->chat.question_cursor = static_cast<int>(item.options.size()) - 1;
      } else if (state->chat.question_cursor > 0) {
        state->chat.question_cursor--;
      }
      return true;
    }

    if (event == Event::ArrowDown || event == Event::Character("j")) {
      std::scoped_lock lock(state->mtx);
      auto& item = state->chat.active_question->questions[state->chat.question_idx];
      int max_idx = static_cast<int>(item.options.size()) - 1;
      if (!state->chat.question_custom_active && state->chat.question_cursor < max_idx) {
        state->chat.question_cursor++;
      } else if (!state->chat.question_custom_active && item.custom && state->chat.question_cursor >= max_idx) {
        state->chat.question_custom_active = true;
      }
      return true;
    }

    if (event == Event::Character(" ")) {
      std::scoped_lock lock(state->mtx);
      auto& item = state->chat.active_question->questions[state->chat.question_idx];
      if (item.multiple && !state->chat.question_custom_active) {
        int idx = state->chat.question_cursor;
        if (state->chat.question_selected.count(idx) > 0) {
          state->chat.question_selected.erase(idx);
        } else {
          state->chat.question_selected.insert(idx);
        }
      }
      return true;
    }

    if (event == Event::Tab) {
      std::scoped_lock lock(state->mtx);
      auto& item = state->chat.active_question->questions[state->chat.question_idx];
      if (item.custom) {
        state->chat.question_custom_active = !state->chat.question_custom_active;
      }
      return true;
    }

    if (event == Event::Return) {
      SubmitQuestionAnswers();
      return true;
    }

    if (event == Event::Escape) {
      RejectActiveQuestion();
      return true;
    }

    if (event == Event::Backspace) {
      std::scoped_lock lock(state->mtx);
      if (state->chat.question_custom_active && !state->chat.question_custom_text.empty()) {
        state->chat.question_custom_text.pop_back();
      }
      return true;
    }

    if (event.is_character()) {
      std::scoped_lock lock(state->mtx);
      if (state->chat.question_custom_active) {
        state->chat.question_custom_text += event.character();
        return true;
      }
    }

    return true;  // Consume all events while question overlay is active.
  }

  // -- Event handling ---------------------------------------------------------

  auto HandleEvent(Event event) -> bool {
    // Drain SSE event queue on every event cycle.
    if (event == Event::Custom) {
      auto events = ctx.event_queue.drain();
      for (const auto& evt : events) {
        DispatchSseEvent(evt);
      }
      // Update message boundary cache from reflect boxes after each render cycle.
      UpdateMessageBoundaries();
      return false;
    }

    // Question overlay takes priority over all other input modes.
    {
      bool has_question = false;
      {
        std::scoped_lock lock(state->mtx);
        has_question = state->chat.active_question.has_value();
      }
      if (has_question) { return HandleQuestionEvent(event); }
    }

    // Tab toggles panel (Artifact ↔ Chat). Also consume TabReverse.
    if (ctx.input_config.chat.toggle_panel.matches(event)) {
      {
        std::scoped_lock lock(state->mtx);
        state->panel = (state->panel == PanelMode::Artifact) ? PanelMode::Chat : PanelMode::Artifact;
      }
      screen.PostEvent(Event::Custom);
      return true;
    }

    InteractionMode interaction;
    {
      std::scoped_lock lock(state->mtx);
      interaction = state->interaction;
    }

    switch (interaction) {
      case InteractionMode::Normal:
        return HandleNormalEvent(event);
      case InteractionMode::Visual:
        return HandleVisualEvent(event);
      case InteractionMode::Insert:
        return HandleInsertEvent(event);
    }
    return false;
  }

  auto HandleNormalEvent(Event& event) -> bool {
    const auto& keys = ctx.input_config;

    // Let Escape bubble up to main.cpp for parent navigation.
    if (keys.navigation.escape.matches(event)) {
      return false;
    }

    // 'v' enters Visual mode.
    if (keys.vim.enter_visual.matches(event)) {
      EnterVisualMode();
      return true;
    }

    // 'i' enters Insert mode (focus input), clears chat cursor.
    if (keys.vim.enter_insert.matches(event)) {
      {
        std::scoped_lock lock(state->mtx);
        state->interaction = InteractionMode::Insert;
        state->chat.chat_cursor = std::nullopt;
      }
      input_component->TakeFocus();
      screen.PostEvent(Event::Custom);
      return true;
    }

    // Tab is handled in HandleEvent before mode dispatch, so '=' is unused.

    // Stage navigation with [ and ].
    if (keys.navigation.cycle_right.matches(event) || keys.navigation.cycle_left.matches(event)) {
      std::string target;
      {
        std::scoped_lock lock(state->mtx);
        auto& stages = state->all_workflow_stages;
        auto it = std::find(stages.begin(), stages.end(), stage);
        if (it == stages.end()) { return true;
}

        if (keys.navigation.cycle_right.matches(event) && std::next(it) != stages.end()) {
          target = *std::next(it);
        } else if (keys.navigation.cycle_left.matches(event) && it != stages.begin()) {
          target = *std::prev(it);
        } else {
          return true;
        }
      }
      nav.replace(ally::StageViewState{task_id, thread_id, target});
      return true;
    }

    // Artifact-panel-only keys.
    PanelMode panel;
    {
      std::scoped_lock lock(state->mtx);
      panel = state->panel;
    }

    if (panel == PanelMode::Artifact) {
      // Toggle rendered/raw.
      if (keys.artifact.toggle_render.matches(event)) {
        std::scoped_lock lock(state->mtx);
        state->review_view_mode =
            (state->review_view_mode == ArtifactViewMode::Rendered) ? ArtifactViewMode::Raw : ArtifactViewMode::Rendered;
        screen.PostEvent(Event::Custom);
        return true;
      }

      // Force reload.
      if (keys.artifact.force_reload.matches(event)) {
        RefreshArtifactStages();
        LoadReviewArtifact(stage);
        return true;
      }

      // hjkl and arrow keys move the cursor in Normal mode.
      bool is_left  = keys.vim.left.matches(event);
      bool is_down  = keys.vim.down.matches(event);
      bool is_up    = keys.vim.up.matches(event);
      bool is_right = keys.vim.right.matches(event);

      if (is_left || is_down || is_up || is_right) {
        std::scoped_lock lock(state->mtx);
        if (!state->review_content.has_value()) { return true; }

        bool is_rendered = state->review_view_mode == ArtifactViewMode::Rendered;

        // Lazy-init cursor at top.
        if (!state->artifact_cursor.has_value()) {
          state->artifact_cursor = TextCursor{0, 0};
        }
        auto& cur = *state->artifact_cursor;

        // In rendered mode, use content_height from the scrollable node
        // (like chat). In raw mode, use the raw line structure.
        int max_row;
        if (is_rendered) {
          max_row = std::max(0, state->review_content_height - 1);
        } else {
          auto lines = SplitLines(*state->review_content);
          max_row = std::max(0, static_cast<int>(lines.size()) - 1);
        }

        if (is_left) {
          cur.col = std::max(0, cur.col - 1);
        } else if (is_right) {
          cur.col = cur.col + 1;
        } else if (is_up) {
          cur.row = std::max(0, cur.row - 1);
        } else if (is_down) {
          cur.row = std::min(cur.row + 1, max_row);
        }

        ScrollArtifactToCursor();

        screen.PostEvent(Event::Custom);
        return true;
      }
    }

    // Chat panel: hjkl and arrow keys move the cursor in Normal mode.
    if (panel == PanelMode::Chat) {
      bool is_left  = keys.vim.left.matches(event);
      bool is_down  = keys.vim.down.matches(event);
      bool is_up    = keys.vim.up.matches(event);
      bool is_right = keys.vim.right.matches(event);

      if (is_left || is_down || is_up || is_right) {
        std::scoped_lock lock(state->mtx);
        // Lazy-init cursor at bottom of content.
        if (!state->chat.chat_cursor.has_value()) {
          int last_row = std::max(0, state->chat.content_height - 1);
          state->chat.chat_cursor = TextCursor{last_row, 0};
          state->chat.chat_follow = false;
        }
        auto& cur = *state->chat.chat_cursor;
        int max_row = std::max(0, state->chat.content_height - 1);

        if (is_left) {
          cur.col = std::max(0, cur.col - 1);
        } else if (is_right) {
          cur.col = cur.col + 1;
        } else if (is_up) {
          cur.row = std::max(0, cur.row - 1);
        } else if (is_down) {
          cur.row = std::min(cur.row + 1, max_row);
        }

        ScrollToCursor();
        screen.PostEvent(Event::Custom);
        return true;
      }

      // J (Shift+J) / Shift+Down / K (Shift+K) / Shift+Up: jump to next/previous message boundary.
      {
        bool is_next = keys.chat.next_message.matches(event);
        bool is_prev = keys.chat.prev_message.matches(event);

        if (is_next || is_prev) {
          std::scoped_lock lock(state->mtx);
          if (!state->chat.chat_cursor.has_value()) {
            int last_row = std::max(0, state->chat.content_height - 1);
            state->chat.chat_cursor = TextCursor{last_row, 0};
            state->chat.chat_follow = false;
          }
          auto& cur = *state->chat.chat_cursor;
          auto& rows = state->chat.message_screen_rows;

          if (is_next) {
            // Find next message start after current row.
            for (int row : rows) {
              if (row > cur.row) {
                cur.row = row;
                cur.col = 0;
                break;
              }
            }
          } else {
            // Find previous message start before current row.
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
              if (*it < cur.row) {
                cur.row = *it;
                cur.col = 0;
                break;
              }
            }
          }

          ScrollToCursor();
          screen.PostEvent(Event::Custom);
          return true;
        }
      }

      // Alt+J / Alt+Down / Alt+K / Alt+Up: jump to next/previous user input ($ message).
      {
        bool is_next = keys.chat.next_user_message.matches(event);
        bool is_prev = keys.chat.prev_user_message.matches(event);

        if (is_next || is_prev) {
          std::scoped_lock lock(state->mtx);
          if (!state->chat.chat_cursor.has_value()) {
            int last_row = std::max(0, state->chat.content_height - 1);
            state->chat.chat_cursor = TextCursor{last_row, 0};
            state->chat.chat_follow = false;
          }
          auto& cur = *state->chat.chat_cursor;
          auto& rows = state->chat.user_message_screen_rows;

          if (is_next) {
            for (int row : rows) {
              if (row > cur.row) {
                cur.row = row;
                cur.col = 0;
                break;
              }
            }
          } else {
            for (auto rit = rows.rbegin(); rit != rows.rend(); ++rit) {
              if (*rit < cur.row) {
                cur.row = *rit;
                cur.col = 0;
                break;
              }
            }
          }

          ScrollToCursor();
          screen.PostEvent(Event::Custom);
          return true;
        }
      }
    }

    // Mouse scroll (both panels).
    if (event.is_mouse()) {
      auto& mouse = event.mouse();
      int delta = 0;
      if (mouse.button == Mouse::WheelUp) {
        delta = -kScrollLines;
      } else if (mouse.button == Mouse::WheelDown) {
        delta = kScrollLines;
      }
      if (delta != 0) {
        if (panel == PanelMode::Artifact) {
          state->review_scroll_y = std::max(0, state->review_scroll_y + delta);
        } else {
          if (delta < 0) { state->chat.chat_follow = false;
}
          state->chat.chat_scroll_y = std::max(0, state->chat.chat_scroll_y + delta);
        }
        screen.PostEvent(Event::Custom);
        return true;
      }
    }

    // Alt+Enter sends message.
    if (keys.chat.send_message.matches(event) && !input_text.empty()) {
      DoSend();
      return true;
    }

    // Click on collapsible tool parts to toggle expand/collapse.
    if (event.is_mouse()) {
      auto& mouse = event.mouse();
      if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
        std::scoped_lock lock(state->mtx);
        for (auto& [key, box] : state->chat.collapsible_boxes) {
          if (mouse.x >= box.x_min && mouse.x <= box.x_max && mouse.y >= box.y_min && mouse.y <= box.y_max) {
            if (state->chat.expanded_parts.count(key) > 0) {
              state->chat.expanded_parts.erase(key);
            } else {
              state->chat.expanded_parts.insert(key);
            }
            screen.PostEvent(Event::Custom);
            return true;
          }
        }
      }
    }

    // Let mouse events through so the model button/menu can receive clicks.
    if (event.is_mouse()) {
      return false;
    }

    // Consume all remaining events in Normal mode — the input component
    // should only receive events when in Insert mode.
    return true;
  }

  auto HandleVisualEvent(Event& event) -> bool {
    std::string text_to_copy;
    ally::vim::YankType yank_type = ally::vim::YankType::None;
    bool was_artifact = false;
    {
      std::scoped_lock lock(state->mtx);
      if (!state->visual.has_value()) {
        state->interaction = InteractionMode::Normal;
        screen.PostEvent(Event::Custom);
        return true;
      }

      // Save selection bounds before HandleVisualKeyEvent may trigger mode change.
      auto saved_anchor = state->visual->anchor;
      auto saved_cursor = state->visual->cursor;
      bool was_chat = state->visual->is_chat;
      was_artifact = !was_chat && state->panel == PanelMode::Artifact;

      auto result = ally::vim::HandleVisualKeyEvent(*state->visual, state->interaction, event, ctx.input_config);

      // Scroll viewport to follow the visual cursor.
      if (state->visual.has_value()) {
        if (state->visual->is_chat) {
          int cursor_row = state->visual->cursor.row;
          int scroll_y = state->chat.chat_scroll_y;
          int viewport_h = state->chat.viewport_height;
          if (viewport_h > 0) {
            if (cursor_row < scroll_y) {
              state->chat.chat_scroll_y = cursor_row;
            } else if (cursor_row >= scroll_y + viewport_h) {
              state->chat.chat_scroll_y = cursor_row - viewport_h + 1;
            }
          }
        } else {
          state->artifact_cursor = state->visual->cursor;
          ScrollArtifactToCursor();
        }
      }

      // If mode changed back to Normal, clear visual state.
      if (state->interaction == InteractionMode::Normal) {
        state->visual = std::nullopt;
      }

      yank_type = result.type;
      if (result.type != ally::vim::YankType::None) {
        if (!result.text.empty()) {
          text_to_copy = std::move(result.text);
        } else if (result.type == ally::vim::YankType::Clean && was_chat) {
          auto [sel_s, sel_e] = ally::vim::NormalizeSelection(saved_anchor, saved_cursor);
          text_to_copy = ally::vim::ExtractCleanYankText(
              state->chat.messages, state->chat.visible_count,
              state->chat.message_screen_rows, state->chat.content_height,
              sel_s.row, sel_e.row);
        }
      }
    }

    // Clean yank on artifacts: wrap in markdown code fencing.
    if (!text_to_copy.empty() && yank_type == ally::vim::YankType::Clean && was_artifact) {
      bool multiline = text_to_copy.find('\n') != std::string::npos;
      if (multiline) {
        text_to_copy = "````markdown\n" + text_to_copy + "\n````";
      } else {
        text_to_copy = "`" + text_to_copy + "`";
      }
    }

    if (!text_to_copy.empty()) {
      CopyToClipboard(text_to_copy);
      int line_count = 1 + static_cast<int>(std::count(text_to_copy.begin(), text_to_copy.end(), '\n'));
      ctx.SetStatus("Copied " + std::to_string(line_count) + " lines into clipboard");
    }

    screen.PostEvent(Event::Custom);
    return true;
  }

  auto HandleInsertEvent(Event& event) -> bool {
    const auto& keys = ctx.input_config;

    // Escape: close any open autocomplete overlay first, or exit insert mode.
    if (keys.navigation.escape.matches(event)) {
      if (command_ac_state->is_open || artifact_ac_state->is_open || file_ac_state->is_open) {
        command_ac_state->is_open = false;
        artifact_ac_state->is_open = false;
        file_ac_state->is_open = false;
        screen.PostEvent(Event::Custom);
        return true;
      }
      {
        std::scoped_lock lock(state->mtx);
        state->interaction = InteractionMode::Normal;
        // Initialize chat cursor when entering Normal on chat panel.
        if (state->panel == PanelMode::Chat) {
          int last_row = std::max(0, state->chat.content_height - 1);
          state->chat.chat_cursor = TextCursor{last_row, 0};
          state->chat.chat_follow = false;
        }
      }
      // Keep focus on input_component — the CatchEvent wrapper handles
      // Normal-mode keys before FTXUI routes them to the focused child.
      screen.PostEvent(Event::Custom);
      return true;
    }

    // Alt+Enter sends message, stay in insert mode.
    if (keys.chat.send_message.matches(event) && !input_text.empty()) {
      DoSend();
      return true;
    }

    // Route events through autocomplete handlers when overlay is open.
    if (command_ac_state->is_open) {
      std::scoped_lock lock(command_ac_state->mutex);
      auto trigger = command_ac_state->trigger_position;
      std::string new_text;
      if (autocomplete::HandleCommandKeydown(*command_ac_state, input_text, new_text, event, keys)) {
        if (!new_text.empty() && trigger.has_value()) {
          input_text = new_text;
          auto space_pos = new_text.find(' ', *trigger);
          cursor_pos = space_pos != std::string::npos ? static_cast<int>(space_pos + 1) : static_cast<int>(new_text.size());
        }
        screen.PostEvent(Event::Custom);
        return true;
      }
    }

    if (artifact_ac_state->is_open) {
      std::scoped_lock lock(artifact_ac_state->mutex);
      auto trigger = artifact_ac_state->trigger_position;
      std::string new_text;
      if (autocomplete::HandleArtifactKeydown(*artifact_ac_state, input_text, new_text, event, keys)) {
        if (!new_text.empty() && trigger.has_value()) {
          input_text = new_text;
          auto space_pos = new_text.find(' ', *trigger);
          cursor_pos = space_pos != std::string::npos ? static_cast<int>(space_pos + 1) : static_cast<int>(new_text.size());
        }
        screen.PostEvent(Event::Custom);
        return true;
      }
    }

    if (file_ac_state->is_open) {
      if (file_ac_component->OnEvent(event)) {
        return true;
      }
    }

    // Mouse scroll in insert mode.
    if (event.is_mouse()) {
      auto& mouse = event.mouse();
      int delta = 0;
      if (mouse.button == Mouse::WheelUp) {
        delta = -kScrollLines;
      } else if (mouse.button == Mouse::WheelDown) {
        delta = kScrollLines;
      }
      if (delta != 0) {
        PanelMode panel;
        {
          std::scoped_lock lock(state->mtx);
          panel = state->panel;
        }
        if (panel == PanelMode::Artifact) {
          state->review_scroll_y = std::max(0, state->review_scroll_y + delta);
        } else {
          if (delta < 0) { state->chat.chat_follow = false;
}
          state->chat.chat_scroll_y = std::max(0, state->chat.chat_scroll_y + delta);
        }
        screen.PostEvent(Event::Custom);
        return true;
      }

      // Click on collapsible tool parts to toggle expand/collapse.
      if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
        std::scoped_lock lock(state->mtx);
        for (auto& [key, box] : state->chat.collapsible_boxes) {
          if (mouse.x >= box.x_min && mouse.x <= box.x_max && mouse.y >= box.y_min && mouse.y <= box.y_max) {
            if (state->chat.expanded_parts.count(key) > 0) {
              state->chat.expanded_parts.erase(key);
            } else {
              state->chat.expanded_parts.insert(key);
            }
            screen.PostEvent(Event::Custom);
            return true;
          }
        }
      }
    }

    // Delegate everything else to the input component.
    return false;
  }

  // -- Mode transitions -------------------------------------------------------

  void EnterVisualMode() {
    std::scoped_lock lock(state->mtx);

    std::vector<std::string> lines;
    bool is_chat = false;

    if (state->panel == PanelMode::Artifact && state->review_content.has_value()) {
      if (state->review_view_mode == ArtifactViewMode::Rendered) {
        // Rendered mode: use screen-row coordinate space (like chat).
        // Build placeholder lines matching the rendered content height.
        int height = std::max(1, state->review_content_height);
        lines.resize(height);
      } else {
        lines = SplitLines(*state->review_content);
      }
    } else if (state->panel == PanelMode::Chat) {
      // For chat, use screen-row coordinate space. Build placeholder lines
      // matching content_height so cursor movement bounds are correct.
      // Actual text extraction uses screen capture (screen_captured_text).
      int height = std::max(1, state->chat.content_height);
      lines.resize(height);
      is_chat = true;
    }

    if (lines.empty()) { return; }

    VisualModeState vs;
    if (is_chat && state->chat.chat_cursor.has_value()) {
      vs.anchor = *state->chat.chat_cursor;
      vs.cursor = *state->chat.chat_cursor;
    } else if (!is_chat && state->artifact_cursor.has_value()) {
      vs.anchor = *state->artifact_cursor;
      vs.cursor = *state->artifact_cursor;
    } else {
      vs.anchor = {0, 0};
      vs.cursor = {0, 0};
    }
    int max_row = std::max(0, static_cast<int>(lines.size()) - 1);
    vs.anchor.row = std::clamp(vs.anchor.row, 0, max_row);
    vs.cursor.row = std::clamp(vs.cursor.row, 0, max_row);
    if (is_chat) {
      vs.viewport_width = std::max(0, chat_body_box_.x_max - chat_body_box_.x_min + 1);
    } else if (state->review_view_mode == ArtifactViewMode::Rendered) {
      vs.viewport_width = std::max(0, artifact_body_box_.x_max - artifact_body_box_.x_min + 1);
    }
    vs.lines = std::move(lines);
    vs.is_chat = is_chat;

    state->visual = std::move(vs);
    state->interaction = InteractionMode::Visual;
    screen.PostEvent(Event::Custom);
  }

  // -- Background operations --------------------------------------------------

  void LoadReviewArtifact(const std::string& stage_slug) {
    {
      std::scoped_lock lock(state->mtx);
      state->review_loading = true;
      state->review_content = std::nullopt;
      state->review_rendered.clear();
      state->artifact_cursor = std::nullopt;

      // If in visual mode, exit — content is changing.
      if (state->interaction == InteractionMode::Visual) {
        state->interaction = InteractionMode::Normal;
        state->visual = std::nullopt;
      }
    }
    screen.PostEvent(Event::Custom);

    auto s = state;
    auto t = theme;
    auto tid = task_id;
    auto thid = thread_id;
    auto qdirs = ctx.query_dirs;
    auto& service = ctx.artifact_service;
    auto& scr = screen;

    std::thread([s, t, tid, thid, stage_slug, qdirs, &service, &scr] -> void {
      auto content = service.get_artifact(tid, thid, stage_slug);

      // Strip YAML frontmatter (--- ... ---) so raw and rendered line numbers
      // align. The renderer also strips it internally; doing it here keeps
      // review_content consistent with the rendered output.
      if (content.has_value() && content->size() >= 3 && content->substr(0, 3) == "---") {
        auto close = content->find("\n---", 3);
        if (close != std::string::npos) {
          auto after = close + 4;
          if (after < content->size() && (*content)[after] == '\n') {
            ++after;
          }
          *content = content->substr(after);
        }
      }

      std::vector<Element> rendered;
      if (content.has_value() && !content->empty()) {
        rendering::TreeSitterRenderer renderer(t, qdirs);
        auto blocks = renderer.Render(*content);
        rendered.reserve(blocks.size());
        for (auto& block : blocks) {
          rendered.push_back(std::move(block.element));
        }
      }

      {
        std::scoped_lock lock(s->mtx);
        s->review_content = std::move(content);
        s->review_rendered = std::move(rendered);
        s->review_loading = false;
      }
      scr.PostEvent(Event::Custom);
    }).detach();
  }

  void RefreshArtifactStages() {
    auto slugs = ctx.artifact_service.list_stage_artifacts(task_id, thread_id);

    {
      std::scoped_lock lock(state->mtx);

      // Sort artifact slugs to match workflow stage order.
      if (!state->all_workflow_stages.empty()) {
        const auto& order = state->all_workflow_stages;
        std::sort(slugs.begin(), slugs.end(), [&order](const std::string& lhs, const std::string& rhs) -> bool {
          auto li = std::find(order.begin(), order.end(), lhs);
          auto ri = std::find(order.begin(), order.end(), rhs);
          return li < ri;
        });
      }

      state->artifact_stages = std::move(slugs);
    }
    screen.PostEvent(Event::Custom);
  }

  // Resolved workflow stages (populated by ResolveWorkflowStages).
  std::vector<models::WorkflowStage> workflow_stages;

  void ResolveWorkflowStages() {
    auto task = ctx.task_provider.get_task(task_id);
    if (!task.has_value()) { return;
}

    std::string workflow_id;
    for (const auto& thread : task->threads) {
      if (thread.id == thread_id) {
        workflow_id = thread.workflow_id;
        break;
      }
    }
    if (workflow_id.empty()) { return;
}

    auto workflow = ctx.workflow_service.get_workflow(workflow_id);
    if (!workflow.has_value()) { return;
}

    workflow_stages = workflow->stages;

    std::vector<std::string> stage_slugs;
    stage_slugs.reserve(workflow->stages.size());
    for (const auto& ws : workflow->stages) {
      stage_slugs.push_back(ws.id);
    }

    {
      std::scoped_lock lock(state->mtx);
      state->all_workflow_stages = std::move(stage_slugs);
    }
  }

  // -- Chat: SSE event dispatch -----------------------------------------------

  void DispatchSseEvent(const opencode::OpenCodeEvent& evt) {
    auto type = evt.data.value("type", "");

    if (type == "message.part.delta") {
      // Filter out events from sub-agent sessions to prevent ghost messages,
      // but accumulate their text so we can stream it inside the sub-agent card.
      auto props = evt.data.value("properties", nlohmann::json::object());
      auto evt_sid = props.value("sessionID", "");
      auto msg_id = props.value("messageID", "");
      auto delta = props.value("delta", "");
      {
        std::scoped_lock lock(state->mtx);
        if (state->chat.session_id) {
          if (!evt_sid.empty() && evt_sid != *state->chat.session_id) {
            // Sub-agent event — accumulate streaming text.
            if (!delta.empty()) {
              state->chat.subagent_streaming_text[evt_sid] += delta;
            }
            screen.PostEvent(Event::Custom);
            return;
          }
          if (evt_sid.empty() && !msg_id.empty()) {
            bool found = false;
            for (const auto& msg : state->chat.messages) {
              if (msg.info.id == msg_id) { found = true; break; }
            }
            if (!found) {
              // Unknown message — look up sub-agent session from mapping.
              auto sit = state->chat.subagent_msg_sessions.find(msg_id);
              if (sit != state->chat.subagent_msg_sessions.end() && !delta.empty()) {
                state->chat.subagent_streaming_text[sit->second] += delta;
                screen.PostEvent(Event::Custom);
              }
              return;
            }
          }
        }
      }
      ApplyPartDelta(state, evt.data);
      screen.PostEvent(Event::Custom);
    } else if (type == "message.part.updated") {
      // Filter out events from sub-agent sessions.
      auto props = evt.data.value("properties", nlohmann::json::object());
      auto part_json = props.value("part", nlohmann::json::object());
      auto evt_sid = part_json.value("sessionID", "");
      {
        std::scoped_lock lock(state->mtx);
        if (state->chat.session_id && !evt_sid.empty() && evt_sid != *state->chat.session_id) { return; }
      }
      ApplyPartUpdated(state, evt.data);
      screen.PostEvent(Event::Custom);
    } else if (type == "message.created") {
      // Seed the message entry with role so subsequent deltas find it.
      // Do NOT call RefreshMessages here — it races with streaming deltas
      // and can discard in-flight content.
      auto props = evt.data.value("properties", nlohmann::json::object());
      // Payload: properties.info = { id, role, sessionID }
      auto info = props.value("info", nlohmann::json::object());
      auto msg_id = info.value("id", "");
      auto evt_sid = info.value("sessionID", "");
      if (!msg_id.empty()) {
        std::scoped_lock lock(state->mtx);
        // Record sub-agent message→session mapping for delta accumulation, then skip.
        if (state->chat.session_id && !evt_sid.empty() && evt_sid != *state->chat.session_id) {
          state->chat.subagent_msg_sessions[msg_id] = evt_sid;
          return;
        }
        state->chat.is_loading = true;
        auto& msg = GetOrCreateMessage(*state, msg_id);
        // Carry over role and other metadata from the event.
        if (info.contains("role")) {
          msg.info.extra["role"] = info["role"];
        }
      }
      screen.PostEvent(Event::Custom);
    } else if (type == "message.updated") {
      // Update message metadata (role, etc.) without touching parts.
      // Payload: properties.info = { id, role, sessionID, ... }
      auto props = evt.data.value("properties", nlohmann::json::object());
      auto info = props.value("info", nlohmann::json::object());
      auto msg_id = info.value("id", "");
      auto evt_sid = info.value("sessionID", "");
      if (!msg_id.empty()) {
        std::scoped_lock lock(state->mtx);
        // Skip messages from sub-agent sessions.
        if (state->chat.session_id && !evt_sid.empty() && evt_sid != *state->chat.session_id) { return; }
        for (auto& msg : state->chat.messages) {
          if (msg.info.id == msg_id) {
            msg.info.extra = info;
            break;
          }
        }
      }
      screen.PostEvent(Event::Custom);
    } else if (type == "session.idle" ||
               (type == "session.status" && evt.data.contains("status") && evt.data["status"].value("type", "") == "idle")) {
      auto sptr = state;
      auto& ocs = ctx.opencode_state;
      auto& ocm = ctx.opencode_mutex;
      auto& scr = screen;
      std::thread([sptr, &ocs, &ocm, &scr] -> void {
        RefreshMessages(sptr, ocs, ocm, scr);
        {
          std::scoped_lock lock(sptr->mtx);
          sptr->chat.is_loading = false;
        }
        scr.PostEvent(Event::Custom);
      }).detach();
    } else if (type == "question.asked") {
      auto props = evt.data.value("properties", nlohmann::json::object());
      try {
        auto question = props.get<opencode::QuestionRequest>();
        std::scoped_lock lock(state->mtx);
        if (state->chat.session_id && question.session_id == *state->chat.session_id) {
          state->chat.active_question = std::move(question);
          state->chat.question_idx = 0;
          state->chat.question_cursor = 0;
          state->chat.question_selected.clear();
          state->chat.question_custom_text.clear();
          state->chat.question_custom_active = false;
        }
      } catch (const nlohmann::json::exception&) {
        // Ignore malformed question events.
      }
      screen.PostEvent(Event::Custom);
    } else if (type == "question.replied" || type == "question.rejected") {
      auto props = evt.data.value("properties", nlohmann::json::object());
      auto request_id = props.value("requestID", "");
      if (!request_id.empty()) {
        std::scoped_lock lock(state->mtx);
        if (state->chat.active_question && state->chat.active_question->id == request_id) {
          state->chat.active_question = std::nullopt;
        }
      }
      screen.PostEvent(Event::Custom);
    }
    // Silently ignore: server.heartbeat, server.connected, session.updated,
    // session.diff
  }

  // -- Chat: send message -----------------------------------------------------

  void DoSend() {
    std::string trimmed = input_text;
    auto start = trimmed.find_first_not_of(" \t\n\r");
    auto end = trimmed.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) { return;
}
    trimmed = trimmed.substr(start, end - start + 1);

    std::string sid;
    {
      std::scoped_lock lock(state->mtx);
      if (!state->chat.session_id || state->chat.is_loading) { return;
}
      sid = *state->chat.session_id;

      // Optimistic user message.
      opencode::MessageWithParts opt_msg;
      opt_msg.info.id = "optimistic-" + std::to_string(state->chat.messages.size());
      opt_msg.info.extra["role"] = "user";
      nlohmann::json text_part;
      text_part["type"] = "text";
      text_part["text"] = trimmed;
      opt_msg.parts.push_back(std::move(text_part));
      state->chat.messages.push_back(std::move(opt_msg));

      state->chat.is_loading = true;
      state->chat.error_msg = std::nullopt;
      state->chat.chat_follow = true;
      state->chat.chat_scroll_y = INT_MAX;
    }
    input_text.clear();

    auto sptr = state;
    auto& ocs = ctx.opencode_state;
    auto& ocm = ctx.opencode_mutex;
    auto& scr = screen;

    std::optional<std::pair<std::string, std::string>> model_spec;
    {
      std::scoped_lock lock(state->mtx);
      int idx = state->chat.selected_model_idx;
      if (idx >= 0 && idx < static_cast<int>(state->chat.filtered_models.size())) {
        auto& mdl = state->chat.filtered_models[idx];
        model_spec = {mdl.id, mdl.provider};
      }
    }

    std::thread([sptr, sid, trimmed, model_spec, &ocs, &ocm, &scr]() -> void {
      bool is_command = !trimmed.empty() && trimmed[0] == '/';

      if (is_command) {
        auto space_pos = trimmed.find(' ');
        std::string cmd_name =
            (space_pos != std::string::npos) ? trimmed.substr(1, space_pos - 1) : trimmed.substr(1);
        std::string arguments = (space_pos != std::string::npos) ? trimmed.substr(space_pos + 1) : "";

        opencode::CommandRequest cmd_req;
        cmd_req.command = cmd_name;
        cmd_req.arguments = arguments;
        if (model_spec.has_value()) {
          cmd_req.extra["model"] = model_spec->second + "/" + model_spec->first;
        }

        auto result = opencode::RunCommand(ocs, ocm, sid, cmd_req);
        if (opencode::is_ok(result)) {
          auto sess_result = opencode::GetSession(ocs, ocm, sid);
        } else {
          const auto& err = opencode::get_error(result);
          std::scoped_lock lock(sptr->mtx);
          sptr->chat.error_msg = "Command failed: " + err.message;
          sptr->chat.is_loading = false;
        }
      } else {
        opencode::AsyncPromptRequest req;
        req.data = {{"parts", {{{"type", "text"}, {"text", trimmed}}}}};
        if (model_spec.has_value()) {
          req.data["model"] = {{"modelID", model_spec->first}, {"providerID", model_spec->second}};
        }
        auto result = opencode::PromptAsync(ocs, ocm, sid, req);
        if (opencode::is_ok(result)) {
          auto sess_result = opencode::GetSession(ocs, ocm, sid);
        } else {
          const auto& err = opencode::get_error(result);
          std::scoped_lock lock(sptr->mtx);
          sptr->chat.error_msg = "Send failed: " + err.message;
          sptr->chat.is_loading = false;
        }
      }

      scr.PostEvent(Event::Custom);
    }).detach();

    screen.PostEvent(Event::Custom);
  }

  // -- Model selection ---------------------------------------------------------

  void FilterModelsForProvider() {
    std::string provider_id;
    {
      std::shared_lock plock(ctx.provider_mutex);
      provider_id = ctx.selected_provider.value_or("");
    }

    std::scoped_lock lock(state->mtx);
    state->chat.last_seen_provider = provider_id;
    state->chat.filtered_models.clear();
    state->chat.model_dropdown_names.clear();

    for (const auto& model : state->chat.all_models) {
      if (model.provider == provider_id) {
        state->chat.filtered_models.push_back(model);
        state->chat.model_dropdown_names.push_back(model.name);
      }
    }

    auto persisted = commands::storage::GetModelForProvider(ctx.project_root, provider_id);
    state->chat.selected_model_idx = 0;
    if (persisted.has_value()) {
      for (int idx = 0; idx < static_cast<int>(state->chat.filtered_models.size()); ++idx) {
        if (state->chat.filtered_models[idx].id == *persisted) {
          state->chat.selected_model_idx = idx;
          break;
        }
      }
    }
  }

  void LoadModels() {
    auto result = opencode::ListModels(ctx.opencode_state, ctx.opencode_mutex);
    if (opencode::is_ok(result)) {
      {
        std::scoped_lock lock(state->mtx);
        state->chat.all_models = opencode::get_value(result);
      }
      FilterModelsForProvider();
    }
    screen.PostEvent(Event::Custom);
  }

  void CheckProviderChanged() {
    std::string current_provider;
    {
      std::shared_lock plock(ctx.provider_mutex);
      current_provider = ctx.selected_provider.value_or("");
    }

    bool changed = false;
    {
      std::scoped_lock lock(state->mtx);
      changed = state->chat.last_seen_provider.has_value() && *state->chat.last_seen_provider != current_provider;
    }

    if (changed) {
      FilterModelsForProvider();
    }
  }

  // -- Chat: session resolution -----------------------------------------------

  void ResolveSession() {
    // 1. Try to restore a persisted session ID.
    auto stored_id = commands::storage::GetStageSessionId(ctx.project_root, task_id, thread_id, stage);

    std::string resolved_sid;

    if (stored_id.has_value()) {
      auto result = opencode::GetSession(ctx.opencode_state, ctx.opencode_mutex, *stored_id);
      if (opencode::is_ok(result)) {
        resolved_sid = *stored_id;
      }
      // If error, discard stored ID and create a new one.
    }

    // 2. Create a new session if none was resolved.
    if (resolved_sid.empty()) {
      opencode::CreateSessionRequest req;
      req.title = task_id + " - " + stage;
      auto result = opencode::CreateSession(ctx.opencode_state, ctx.opencode_mutex, req);
      if (opencode::is_ok(result)) {
        resolved_sid = opencode::get_value(result).id;
        commands::storage::SaveStageSessionId(ctx.project_root, task_id, thread_id, stage, resolved_sid);
      } else {
        std::scoped_lock lock(state->mtx);
        state->chat.error_msg = "Failed to create session: " + opencode::get_error(result).message;
        screen.PostEvent(Event::Custom);
        return;
      }
    }

    // 3. Store resolved session ID.
    {
      std::scoped_lock lock(state->mtx);
      state->chat.session_id = resolved_sid;
    }

    // 4. Load message history.
    RefreshMessages(state, ctx.opencode_state, ctx.opencode_mutex, screen);

    // 5. Pre-fill starting prompt if history is empty.
    {
      std::scoped_lock lock(state->mtx);
      if (state->chat.messages.empty() && !workflow_stages.empty()) {
        for (const auto& ws : workflow_stages) {
          if (ws.id == stage && !ws.starting_prompt.empty()) {
            std::string prompt = ws.starting_prompt;
            // Resolve $<slug> references to @path.
            for (const auto& other_stage : workflow_stages) {
              std::string token = "$" + other_stage.id;
              std::string replacement =
                  "@.ally/tasks/" + task_id + "/threads/" + thread_id + "/stages/" + other_stage.id + "/artifact.md";
              size_t pos = 0;
              while ((pos = prompt.find(token, pos)) != std::string::npos) {
                prompt.replace(pos, token.size(), replacement);
                pos += replacement.size();
              }
            }
            input_text = prompt;
            cursor_pos = static_cast<int>(prompt.size());
            break;
          }
        }
      }
    }
    screen.PostEvent(Event::Custom);
  }
};

}  // namespace

auto stage_view(AppContext& ctx, Navigator& nav, ScreenInteractive& screen, const std::string& task_id,
                const std::string& thread_id, const std::string& stage) -> Component {
  auto impl = std::make_shared<StageViewImpl>(ctx, nav, screen, task_id, thread_id, stage);

  impl->ResolveWorkflowStages();

  // -- Input component with cursor tracking and autocomplete triggers ---------

  InputOption input_opts;
  input_opts.multiline = true;
  input_opts.cursor_position = &impl->cursor_pos;
  input_opts.transform = [](InputState state) -> Element {
    if (state.is_placeholder) {
      state.element |= dim;
    }
    return state.element;
  };
  input_opts.on_change = [impl] -> void {
    // Check all three autocomplete triggers on every text change.
    {
      std::scoped_lock lock(impl->command_ac_state->mutex);
      autocomplete::CheckCommandTrigger(*impl->command_ac_state, impl->input_text, impl->cursor_pos);
    }
    {
      std::scoped_lock lock(impl->artifact_ac_state->mutex);
      autocomplete::CheckArtifactTrigger(*impl->artifact_ac_state, impl->input_text, impl->cursor_pos);
    }
    {
      auto on_open = [impl]() -> void {
        auto state_copy = impl->file_ac_state;
        auto root = impl->ctx.project_root;
        auto& scr = impl->screen;
        std::thread([state_copy, root, &scr]() -> void {
          commands::storage::list_directory_tree(root, 6, [state_copy, &scr](std::vector<autocomplete::DirTreeNode> nodes) -> void {
            std::scoped_lock lock(state_copy->mutex);
            state_copy->tree_cache = std::move(nodes);
            scr.PostEvent(Event::Custom);
          });
        }).detach();
      };
      autocomplete::CheckAutocompleteTrigger(*impl->file_ac_state, impl->input_text, impl->cursor_pos, on_open);
    }
  };
  impl->input_component = Input(&impl->input_text, "type a message...", input_opts);

  // -- Autocomplete overlay components ----------------------------------------

  // Skill autocomplete: on_insert callback for mouse-based selection.
  impl->skill_ac_component =
      autocomplete::CommandAutocompleteComponent(impl->command_ac_state, screen, [impl](const std::string& new_text) -> void {
        auto trigger = impl->command_ac_state->trigger_position;
        if (!new_text.empty() && trigger.has_value()) {
          impl->input_text = new_text;
          auto space_pos = new_text.find(' ', *trigger);
          impl->cursor_pos = space_pos != std::string::npos ? static_cast<int>(space_pos + 1) : static_cast<int>(new_text.size());
        }
      });

  // Artifact autocomplete: on_insert callback for mouse-based selection.
  impl->artifact_ac_component =
      autocomplete::ArtifactAutocompleteComponent(impl->artifact_ac_state, screen, [impl](const std::string& new_text) -> void {
        auto trigger = impl->artifact_ac_state->trigger_position;
        if (!new_text.empty() && trigger.has_value()) {
          impl->input_text = new_text;
          auto space_pos = new_text.find(' ', *trigger);
          impl->cursor_pos = space_pos != std::string::npos ? static_cast<int>(space_pos + 1) : static_cast<int>(new_text.size());
        }
      });

  // File autocomplete: on_insert callback splices @path into text.
  impl->file_ac_component =
      autocomplete::FileAutocompleteComponent(impl->file_ac_state, ctx.project_root, screen, [impl](const std::string& insertion) -> void {
        if (impl->file_ac_state->trigger_position.has_value()) {
          int trigger = *impl->file_ac_state->trigger_position;
          int end_pos = trigger + 1;
          while (end_pos < static_cast<int>(impl->input_text.size()) && impl->input_text[end_pos] != ' ' &&
                 impl->input_text[end_pos] != '\n') {
            ++end_pos;
          }
          std::string before = impl->input_text.substr(0, trigger);
          std::string after = impl->input_text.substr(end_pos);
          impl->input_text = before + insertion + " " + after;
          impl->cursor_pos = static_cast<int>(before.size() + insertion.size() + 1);
        }
      }, ctx.input_config);

  // -- Background listeners ---------------------------------------------------

  // Skill listener
  impl->command_service = std::make_shared<services::CommandService>(ctx.project_root.string());
  autocomplete::SetupCommandsListener(impl->command_ac_state, *impl->command_service, ctx.commands_broadcast, screen);

  // Artifact listener
  autocomplete::SetupArtifactsListener(impl->artifact_ac_state, ctx.project_root.string(), task_id, thread_id,
                                       impl->workflow_stages, ctx.artifact_broadcast, screen);

  // -- Initial artifact load on a background thread ---------------------------

  std::thread([impl] -> void {
    impl->RefreshArtifactStages();
    impl->LoadReviewArtifact(impl->stage);
  }).detach();

  // -- Model dropdown component ------------------------------------------------

  {
    ButtonOption model_btn_opt = ButtonOption::Ascii();
    model_btn_opt.transform = [state = impl->state](const EntryState&) -> Element {
      std::scoped_lock lock(state->mtx);
      int idx = state->chat.selected_model_idx;
      if (idx >= 0 && idx < static_cast<int>(state->chat.model_dropdown_names.size())) {
        auto label = state->chat.model_dropdown_names[idx];
        auto arrow = state->chat.model_menu_open ? " ▴" : " ▾";
        return text(" " + label + arrow + " ");
      }
      return text(" no models ") | dim;
    };
    impl->model_button = Button(
        "",
        [state = impl->state]() -> void {
          std::scoped_lock lock(state->mtx);
          state->chat.model_menu_open = !state->chat.model_menu_open;
        },
        model_btn_opt);

    auto select_model = [state = impl->state]() -> void {
      std::scoped_lock lock(state->mtx);
      state->chat.model_menu_open = false;
    };
    impl->model_menu = Menu({
        .entries = &impl->state->chat.model_dropdown_names,
        .selected = &impl->state->chat.selected_model_idx,
        .on_change = select_model,
        .on_enter = select_model,
    });
  }

  // -- Chat session resolution on a background thread -------------------------

  std::thread([impl]() -> void { impl->ResolveSession(); }).detach();

  // -- Model loading on a background thread -----------------------------------

  std::thread([impl]() -> void { impl->LoadModels(); }).detach();

  // Artifact event monitoring thread
  impl->artifact_event_thread = std::thread([impl] -> void {
    auto queue = impl->ctx.artifact_broadcast.subscribe();

    while (!impl->stop.load(std::memory_order_relaxed)) {
      auto events = queue->drain();
      for (const auto& evt : events) {
        if (evt.task_id != impl->task_id || evt.thread_id != impl->thread_id) {
          continue;
        }

        impl->RefreshArtifactStages();

        if (evt.stage == impl->stage) {
          impl->LoadReviewArtifact(evt.stage);
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });

  auto interactive = Container::Vertical({impl->input_component, impl->model_button, impl->model_menu});
  auto comp = Renderer(interactive, [impl]() -> Element { return impl->Render(); });
  comp = CatchEvent(comp, [impl](Event event) -> bool { return impl->HandleEvent(std::move(event)); });
  return comp;
}

}  // namespace ally::views
