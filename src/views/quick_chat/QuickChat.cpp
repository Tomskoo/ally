#include "QuickChat.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

#include "QuickChatTypes.hpp"
#include "src/app/AppContext.hpp"
#include "src/app/NavState.hpp"
#include "src/app/Navigator.hpp"
#include "src/commands/storage/Storage.hpp"
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

using detail::InteractionMode;
using detail::QuickChatCachedPartRender;
using detail::QuickChatPanelState;
using detail::QuickChatViewState;
using detail::TextCursor;
using detail::VisualModeState;

using ally::vim::CopyToClipboard;
using ally::vim::SplitLines;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// SplitLines, CopyToClipboard etc. are in src/components/vim_mode/VimMode.hpp.

auto ExtractFileContent(const std::string& output) -> std::string {
  auto content_start = output.find("<content>");
  auto content_end = output.rfind("</content>");
  if (content_start == std::string::npos || content_end == std::string::npos) { return "";
}
  content_start += 9;
  auto raw = output.substr(content_start, content_end - content_start);

  std::string result;
  std::istringstream stream(raw);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find("(End of file") == 0) { continue;
}
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
// Chat helpers
// ---------------------------------------------------------------------------

void StampPart(QuickChatViewState& state, const std::string& msg_id, size_t part_idx) {
  auto key = msg_id + ":" + std::to_string(part_idx);
  if (state.chat.part_timestamps.count(key) == 0) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    state.chat.part_timestamps[key] = now;
  }
}

auto GetOrCreateMessage(QuickChatViewState& state, const std::string& msg_id) -> auto& {
  for (auto& msg : state.chat.messages) {
    if (msg.info.id == msg_id) {
      return msg;
    }
  }
  auto& msgs = state.chat.messages;
  msgs.erase(std::remove_if(msgs.begin(), msgs.end(),
                            [](const opencode::MessageWithParts& msg) -> bool { return msg.info.id.rfind("optimistic-", 0) == 0; }),
             msgs.end());
  opencode::MessageWithParts new_msg;
  new_msg.info.id = msg_id;
  new_msg.info.extra = nlohmann::json::object();
  new_msg.info.extra["role"] = "assistant";
  state.chat.messages.push_back(std::move(new_msg));
  return state.chat.messages.back();
}

void MergeMessages(QuickChatViewState& state, std::vector<opencode::MessageWithParts> fetched) {
  std::unordered_map<std::string, size_t> existing_index;
  for (size_t i = 0; i < state.chat.messages.size(); ++i) {
    existing_index[state.chat.messages[i].info.id] = i;
  }

  std::unordered_set<std::string> fetched_ids;
  std::vector<opencode::MessageWithParts> merged;
  merged.reserve(fetched.size() + 2);

  for (auto& fetched_msg : fetched) {
    fetched_ids.insert(fetched_msg.info.id);
    auto it = existing_index.find(fetched_msg.info.id);
    if (it != existing_index.end()) {
      auto& existing_msg = state.chat.messages[it->second];
      existing_msg.info.extra = fetched_msg.info.extra;
      if (existing_msg.parts.size() > fetched_msg.parts.size()) {
        merged.push_back(std::move(existing_msg));
      } else {
        merged.push_back(std::move(fetched_msg));
      }
    } else {
      merged.push_back(std::move(fetched_msg));
    }
  }

  for (auto& existing_msg : state.chat.messages) {
    if (fetched_ids.count(existing_msg.info.id) == 0 &&
        existing_msg.info.id.rfind("optimistic-", 0) != 0) {
      merged.push_back(std::move(existing_msg));
    }
  }

  state.chat.messages = std::move(merged);
  state.chat.rendered_parts_cache.clear();
}

void RefreshMessages(const std::shared_ptr<QuickChatViewState>& state, opencode::OpenCodeState& oc_state, std::shared_mutex& oc_mutex,
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

void ApplyPartDelta(const std::shared_ptr<QuickChatViewState>& state, const nlohmann::json& data) {
  std::scoped_lock lock(state->mtx);
  auto props = data.value("properties", nlohmann::json::object());
  auto msg_id = props.value("messageID", "");
  auto part_id = props.value("partID", "");
  auto delta = props.value("delta", "");
  auto field = props.value("field", "text");

  if (msg_id.empty()) { return;
}

  auto& msg = GetOrCreateMessage(*state, msg_id);

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

  if (state->chat.chat_follow) {
    state->chat.chat_scroll_y = INT_MAX;
  }

  StampPart(*state, msg_id, static_cast<size_t>(part_idx));
}

void ApplyPartUpdated(const std::shared_ptr<QuickChatViewState>& state, const nlohmann::json& data) {
  auto props = data.value("properties", nlohmann::json::object());
  if (!props.contains("part") || !props["part"].is_object()) { return;
}
  auto part_json = props["part"];

  auto part_type =
      part_json.contains("type") && part_json["type"].is_string() ? part_json["type"].get<std::string>() : std::string{};

  if (part_type == "step-start" || part_type == "step-finish") { return;
}

  auto msg_id = part_json.contains("messageID") && part_json["messageID"].is_string() ? part_json["messageID"].get<std::string>()
                                                                                      : std::string{};
  auto part_id = part_json.contains("id") && part_json["id"].is_string() ? part_json["id"].get<std::string>() : std::string{};
  if (msg_id.empty()) { return;
}

  std::scoped_lock lock(state->mtx);
  auto& msg = GetOrCreateMessage(*state, msg_id);

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

struct QuickChatImpl {
  AppContext& ctx;
  Navigator& nav;
  ScreenInteractive& screen;
  rendering::HighlightTheme theme;

  std::shared_ptr<QuickChatViewState> state;

  Component input_component;
  std::string input_text;
  int cursor_pos = 0;

  // Autocomplete state (command + file only, no artifact)
  std::shared_ptr<autocomplete::CommandAutocompleteState> command_ac_state = std::make_shared<autocomplete::CommandAutocompleteState>();
  std::shared_ptr<autocomplete::AutocompleteState> file_ac_state = std::make_shared<autocomplete::AutocompleteState>();

  Component skill_ac_component;
  Component file_ac_component;

  // Skill service
  std::shared_ptr<services::CommandService> command_service;

  // Model selector components
  Component model_button;
  Component model_menu;

  // Cursor overlay (lives on the struct so the pointer survives through render).
  std::optional<ally::vim::CursorOverlay> chat_overlay_;

  // Message boundary tracking for J/K jumps (populated during render).
  std::vector<ftxui::Box> msg_reflect_boxes_;
  std::vector<bool> msg_is_user_;
  ftxui::Box chat_body_box_;

  QuickChatImpl(AppContext& ctx, Navigator& nav, ScreenInteractive& screen)
      : ctx(ctx),
        nav(nav),
        screen(screen),
        theme(rendering::HighlightTheme::LoadDefault(ctx.theme_name)),
        state(std::make_shared<QuickChatViewState>()) {}

  ~QuickChatImpl() {
    command_ac_state->listener_stop.store(true, std::memory_order_relaxed);
  }

  QuickChatImpl(const QuickChatImpl&) = delete;
  auto operator=(const QuickChatImpl&) -> QuickChatImpl& = delete;
  QuickChatImpl(QuickChatImpl&&) = delete;
  auto operator=(QuickChatImpl&&) -> QuickChatImpl& = delete;

  // -- Render ----------------------------------------------------------------

  auto Render() -> Element {
    InteractionMode interaction;
    {
      std::scoped_lock lock(state->mtx);
      interaction = state->interaction;
    }

    Element content;
    if (interaction == InteractionMode::Normal || interaction == InteractionMode::Visual) {
      content = RenderChatWithOverlay(interaction == InteractionMode::Visual);
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

  static auto ExtractSubAgentSummary(const std::string& output, size_t max_len = 100) -> std::string {
    constexpr std::string_view kOpen = "<task_result>";
    constexpr std::string_view kClose = "</task_result>";
    auto start = output.find(kOpen);
    if (start != std::string::npos) {
      start += kOpen.size();
      auto end = output.find(kClose, start);
      if (end != std::string::npos) {
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

    std::string type_label = subagent_type;
    if (!type_label.empty()) {
      type_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(type_label[0])));
    }

    bool is_complete = (status == "completed");
    auto base_key = "subagent:" + msg_id + ":" + std::to_string(part_idx);

    bool expanded = state->chat.expanded_parts.count(base_key) > 0;

    auto cache_key = base_key + (expanded ? ":e" : ":c");
    auto& cached = state->chat.rendered_parts_cache[cache_key];
    size_t cache_sig = output.size() + (is_complete ? 1 : 0);
    if (cached.element && cached.content_length == cache_sig) {
      return cached.element | reflect(state->chat.collapsible_boxes[base_key]);
    }

    std::string chevron = expanded ? "\u25BE " : "\u25B8 ";

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
      rendering::TreeSitterRenderer renderer(theme, ctx.query_dirs);
      auto blocks = renderer.Render(output);
      Elements block_els;
      for (auto& block : blocks) {
        block_els.push_back(std::move(block.element));
      }
      card_els.push_back(vbox(std::move(block_els)) | color(Color::GrayLight) | xflex);
    } else if (!expanded && !output.empty()) {
      auto summary = ExtractSubAgentSummary(output);
      if (!summary.empty()) {
        card_els.push_back(text(summary) | dim);
      }
    } else if (!is_complete && output.empty()) {
      card_els.push_back(text("  Waiting for output...") | dim);
    }

    auto el = hbox({
        separator() | color(Color::Blue),
        text(" "),
        vbox(std::move(card_els)) | flex,
    });

    cached = {cache_sig, el};
    return el | reflect(state->chat.collapsible_boxes[base_key]);
  }

  auto RenderToolPart(const std::string& msg_id, size_t part_idx, const nlohmann::json& part) -> Element {
    auto tool_name = part.value("tool", "tool");

    if (tool_name == "task") {
      return RenderSubAgentPart(msg_id, part_idx, part);
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
      if (!output.empty()) {
        return cached.element | reflect(state->chat.collapsible_boxes[base_key]);
      }
      return cached.element;
    }

    std::string header_text = title.empty() ? tool_name : title;
    Elements els;
    els.push_back(text("# " + header_text) | bold);

    if (input.contains("command") && input["command"].is_string()) {
      els.push_back(text("$ " + input["command"].get<std::string>()) | color(Color::GrayLight));
    } else if (input.contains("file_path") && input["file_path"].is_string()) {
      els.push_back(text("$ " + input["file_path"].get<std::string>()) | color(Color::GrayLight));
    } else if (input.contains("pattern") && input["pattern"].is_string()) {
      els.push_back(text("$ " + input["pattern"].get<std::string>()) | color(Color::GrayLight));
    }

    if (!output.empty()) {

      std::string file_path;
      if (input.contains("filePath") && input["filePath"].is_string()) {
        file_path = input["filePath"].get<std::string>();
      } else if (input.contains("file_path") && input["file_path"].is_string()) {
        file_path = input["file_path"].get<std::string>();
      }
      auto lang = rendering::QueryStore::LanguageFromPath(file_path);
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

      if (part_type == "tool" || part_type == "tool-result") {
        parts_elements.push_back(hbox({
            text(icon) | dim,
            part_el | flex,
        }));
      } else {
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

    if (parts_elements.empty()) {
      parts_elements.push_back(text(icon) | dim);
    }

    return vbox(std::move(parts_elements));
  }

  auto RenderChatPanel() -> Element {
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

      for (size_t idx = start_idx; idx < total; ++idx) {
        if (idx > start_idx) {
          message_elements.push_back(text(""));
        }
        message_elements.push_back(RenderMessage(msgs[idx]));
      }

      if (msgs.empty() && !state->chat.is_loading) {
        message_elements.push_back(text("  Send a message to start chatting.") | dim);
      }

      if (state->chat.is_loading) {
        const auto *label = (state->chat.frame_count % 2 == 0) ? "Thinking..." : "Thinking. . .";
        message_elements.push_back(text("  " + std::string(label)) | dim);
      }

      if (state->chat.error_msg) {
        message_elements.push_back(paragraph("  Error: " + *state->chat.error_msg) | color(Color::Red));
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
    });
  }

  /// Render chat with cursor/selection overlay (Normal or Visual mode).
  auto RenderChatWithOverlay(bool is_visual) -> Element {
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
        message_elements.push_back(text("  Send a message to start chatting.") | dim);
      }

      if (state->chat.is_loading) {
        const auto *label = (state->chat.frame_count % 2 == 0) ? "Thinking..." : "Thinking. . .";
        message_elements.push_back(text("  " + std::string(label)) | dim);
      }

      if (state->chat.error_msg) {
        message_elements.push_back(paragraph("  Error: " + *state->chat.error_msg) | color(Color::Red));
      }
    }

    auto chat_body = vbox(std::move(message_elements)) | reflect_layout(chat_body_box_);

    // Build cursor overlay state.
    chat_overlay_ = std::nullopt;
    {
      std::scoped_lock lock(state->mtx);
      if (is_visual && state->visual.has_value()) {
        auto [sel_s, sel_e] = ally::vim::NormalizeSelection(state->visual->anchor, state->visual->cursor);
        chat_overlay_ = ally::vim::CursorOverlay{
            ally::vim::CursorOverlay::Mode::Selection,
            state->visual->cursor.row, state->visual->cursor.col,
            sel_s.row, sel_s.col, sel_e.row, sel_e.col,
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

    return dbox({
        vbox({
            make_scrollable(overlaid | vscroll_indicator, &state->chat.chat_scroll_y, &state->chat.viewport_height, &state->chat.content_height) | flex,
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

  auto InputBarHeight() -> int {
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
          return text(" CHAT ") | bold | color(Color::Green);
      }
      return text("");
    }();
    auto prompt_label = (interaction == InteractionMode::Insert) ? (text(" $ ") | bold | color(Color::White)) : (text(" $ ") | bold | dim);
    auto send_label = !input_text.empty() ? (text(" M-\u23CE Send ") | bold | color(Color::Green)) : (text(" M-\u23CE Send ") | dim);
    auto input_el = (interaction == InteractionMode::Insert) ? input_component->Render()
                                                              : text(input_text.empty() ? "type a message..." : input_text) | dim;
    auto input_with_prompt = hbox({prompt_label, input_el | flex});
    auto model_el = RenderModelSelector();

    auto sep = []() -> Element {
      return separatorStyled(LIGHT) | color(Color::Green);
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
    if (file_ac_state->is_open) {
      overlay = file_ac_component->Render();
    }
    return overlay | clear_under;
  }

  // -- Scroll-follow-cursor ----------------------------------------------------

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

  auto HandleEvent(Event event) -> bool {
    // Drain SSE event queue on every event cycle.
    if (event == Event::Custom) {
      auto events = ctx.event_queue.drain();
      for (const auto& evt : events) {
        DispatchSseEvent(evt);
      }
      UpdateMessageBoundaries();
      return false;
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
    // Escape in Normal mode: exit the view.
    if (event == Event::Escape) {
      nav.back();
      return true;
    }

    // 'v' enters Visual mode.
    if (event == Event::Character('v')) {
      EnterVisualMode();
      return true;
    }

    // 'i' enters Insert mode.
    if (event == Event::Character('i')) {
      {
        std::scoped_lock lock(state->mtx);
        state->interaction = InteractionMode::Insert;
        state->chat.chat_cursor = std::nullopt;
      }
      input_component->TakeFocus();
      screen.PostEvent(Event::Custom);
      return true;
    }

    // hjkl and arrow keys move the cursor.
    {
      bool is_left  = event == Event::Character('h') || event == Event::ArrowLeft;
      bool is_down  = event == Event::Character('j') || event == Event::ArrowDown;
      bool is_up    = event == Event::Character('k') || event == Event::ArrowUp;
      bool is_right = event == Event::Character('l') || event == Event::ArrowRight;

      if (is_left || is_down || is_up || is_right) {
        std::scoped_lock lock(state->mtx);
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
    }

    // J (Shift+J) / Shift+Down / K (Shift+K) / Shift+Up: jump to next/previous message boundary.
    {
      bool is_next = event == Event::Character('J') || event == Event::Special("\x1b[1;2B");
      bool is_prev = event == Event::Character('K') || event == Event::Special("\x1b[1;2A");

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

    // Alt+J / Alt+Down / Alt+K / Alt+Up: jump to next/previous user input ($ message).
    {
      bool is_next = event == Event::AltJ || event == Event::Special("\x1b[1;3B");
      bool is_prev = event == Event::AltK || event == Event::Special("\x1b[1;3A");

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

    // Mouse scroll.
    if (event.is_mouse()) {
      auto& mouse = event.mouse();
      int delta = 0;
      if (mouse.button == Mouse::WheelUp) {
        delta = -kScrollLines;
      } else if (mouse.button == Mouse::WheelDown) {
        delta = kScrollLines;
      }
      if (delta != 0) {
        if (delta < 0) { state->chat.chat_follow = false; }
        state->chat.chat_scroll_y = std::max(0, state->chat.chat_scroll_y + delta);
        screen.PostEvent(Event::Custom);
        return true;
      }

      // Click on collapsible tool parts.
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

      return false;
    }

    // Alt+Enter sends message.
    if (event == Event::Special({27, 13}) && !input_text.empty()) {
      DoSend();
      return true;
    }

    // Consume all remaining events in Normal mode.
    return true;
  }

  auto HandleVisualEvent(Event& event) -> bool {
    std::string text_to_copy;
    {
      std::scoped_lock lock(state->mtx);
      if (!state->visual.has_value()) {
        state->interaction = InteractionMode::Normal;
        screen.PostEvent(Event::Custom);
        return true;
      }

      auto result = ally::vim::HandleVisualKeyEvent(*state->visual, state->interaction, event);

      // Scroll viewport to follow the visual cursor.
      if (state->visual.has_value()) {
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
      }

      if (state->interaction == InteractionMode::Normal) {
        state->visual = std::nullopt;
      }

      if (result.has_value() && !result->empty()) {
        text_to_copy = std::move(*result);
      }
    }

    if (!text_to_copy.empty()) {
      CopyToClipboard(text_to_copy);
    }

    screen.PostEvent(Event::Custom);
    return true;
  }

  auto HandleInsertEvent(Event& event) -> bool {
    // Tab: insert a tab character into the input.
    if (event == Event::Tab) {
      input_text.insert(cursor_pos, "\t");
      cursor_pos += 1;
      screen.PostEvent(Event::Custom);
      return true;
    }
    if (event == Event::TabReverse) {
      return true;
    }

    // Escape: close autocomplete overlay first, or enter Normal mode.
    if (event == Event::Escape) {
      if (command_ac_state->is_open || file_ac_state->is_open) {
        command_ac_state->is_open = false;
        file_ac_state->is_open = false;
        screen.PostEvent(Event::Custom);
        return true;
      }
      {
        std::scoped_lock lock(state->mtx);
        state->interaction = InteractionMode::Normal;
        int last_row = std::max(0, state->chat.content_height - 1);
        state->chat.chat_cursor = TextCursor{last_row, 0};
        state->chat.chat_follow = false;
      }
      screen.PostEvent(Event::Custom);
      return true;
    }

    // Alt+Enter sends message.
    if (event == Event::Special({27, 13}) && !input_text.empty()) {
      DoSend();
      return true;
    }

    // Route events through autocomplete handlers when overlay is open.
    if (command_ac_state->is_open) {
      std::scoped_lock lock(command_ac_state->mutex);
      auto trigger = command_ac_state->trigger_position;
      std::string new_text;
      if (autocomplete::HandleCommandKeydown(*command_ac_state, input_text, new_text, event)) {
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
        if (delta < 0) { state->chat.chat_follow = false; }
        state->chat.chat_scroll_y = std::max(0, state->chat.chat_scroll_y + delta);
        screen.PostEvent(Event::Custom);
        return true;
      }

      // Click on collapsible tool parts.
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

      return false;
    }

    // Delegate everything else to the input component.
    return false;
  }

  // -- Mode transitions -------------------------------------------------------

  void EnterVisualMode() {
    std::scoped_lock lock(state->mtx);

    std::vector<std::string> lines;
    for (const auto& msg : state->chat.messages) {
      for (size_t pidx = 0; pidx < msg.parts.size(); ++pidx) {
        const auto& part = msg.parts[pidx];
        auto part_type = part.contains("type") && part["type"].is_string() ? part["type"].get<std::string>() : std::string{};
        if (part_type == "step-start" || part_type == "step-finish") { continue; }
        std::string content;
        if (part.contains("text") && part["text"].is_string()) {
          content = part["text"].get<std::string>();
        } else if (part.contains("content") && part["content"].is_string()) {
          content = part["content"].get<std::string>();
        }
        if (!content.empty()) {
          auto part_lines = SplitLines(content);
          for (auto& line : part_lines) {
            lines.push_back(std::move(line));
          }
        }
      }
      lines.emplace_back();
    }

    if (lines.empty()) { return; }

    VisualModeState vis;
    if (state->chat.chat_cursor.has_value()) {
      vis.anchor = *state->chat.chat_cursor;
      vis.cursor = *state->chat.chat_cursor;
    } else {
      vis.anchor = {0, 0};
      vis.cursor = {0, 0};
    }
    vis.lines = std::move(lines);
    vis.is_chat = true;

    state->visual = std::move(vis);
    state->interaction = InteractionMode::Visual;
    screen.PostEvent(Event::Custom);
  }

  // -- Chat: SSE event dispatch -----------------------------------------------

  void DispatchSseEvent(const opencode::OpenCodeEvent& evt) {
    auto type = evt.data.value("type", "");

    if (type == "message.part.delta") {
      auto props = evt.data.value("properties", nlohmann::json::object());
      auto evt_sid = props.value("sessionID", "");
      auto msg_id = props.value("messageID", "");
      auto delta = props.value("delta", "");
      {
        std::scoped_lock lock(state->mtx);
        if (state->chat.session_id) {
          if (!evt_sid.empty() && evt_sid != *state->chat.session_id) {
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
      auto props = evt.data.value("properties", nlohmann::json::object());
      auto info = props.value("info", nlohmann::json::object());
      auto msg_id = info.value("id", "");
      auto evt_sid = info.value("sessionID", "");
      if (!msg_id.empty()) {
        std::scoped_lock lock(state->mtx);
        if (state->chat.session_id && !evt_sid.empty() && evt_sid != *state->chat.session_id) {
          state->chat.subagent_msg_sessions[msg_id] = evt_sid;
          return;
        }
        state->chat.is_loading = true;
        auto& msg = GetOrCreateMessage(*state, msg_id);
        if (info.contains("role")) {
          msg.info.extra["role"] = info["role"];
        }
      }
      screen.PostEvent(Event::Custom);
    } else if (type == "message.updated") {
      auto props = evt.data.value("properties", nlohmann::json::object());
      auto info = props.value("info", nlohmann::json::object());
      auto msg_id = info.value("id", "");
      auto evt_sid = info.value("sessionID", "");
      if (!msg_id.empty()) {
        std::scoped_lock lock(state->mtx);
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
      std::thread([sptr, &ocs, &ocm, &scr]() -> void {
        RefreshMessages(sptr, ocs, ocm, scr);
        {
          std::scoped_lock lock(sptr->mtx);
          sptr->chat.is_loading = false;
        }
        scr.PostEvent(Event::Custom);
      }).detach();
    }
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

  // -- Session resolution (ephemeral) -----------------------------------------

  void ResolveSession() {
    opencode::CreateSessionRequest req;
    req.title = "Quick Chat";
    auto result = opencode::CreateSession(ctx.opencode_state, ctx.opencode_mutex, req);
    if (opencode::is_ok(result)) {
      std::scoped_lock lock(state->mtx);
      state->chat.session_id = opencode::get_value(result).id;
    } else {
      std::scoped_lock lock(state->mtx);
      state->chat.error_msg = "Failed to create session: " + opencode::get_error(result).message;
    }
    screen.PostEvent(Event::Custom);
  }
};

}  // namespace

auto quick_chat(AppContext& ctx, Navigator& nav, ScreenInteractive& screen) -> Component {
  auto impl = std::make_shared<QuickChatImpl>(ctx, nav, screen);

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
  input_opts.on_change = [impl]() -> void {
    {
      std::scoped_lock lock(impl->command_ac_state->mutex);
      autocomplete::CheckCommandTrigger(*impl->command_ac_state, impl->input_text, impl->cursor_pos);
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

  impl->skill_ac_component =
      autocomplete::CommandAutocompleteComponent(impl->command_ac_state, screen, [impl](const std::string& new_text) -> void {
        auto trigger = impl->command_ac_state->trigger_position;
        if (!new_text.empty() && trigger.has_value()) {
          impl->input_text = new_text;
          auto space_pos = new_text.find(' ', *trigger);
          impl->cursor_pos = space_pos != std::string::npos ? static_cast<int>(space_pos + 1) : static_cast<int>(new_text.size());
        }
      });

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
      });

  // -- Background listeners ---------------------------------------------------

  impl->command_service = std::make_shared<services::CommandService>(ctx.project_root.string());
  autocomplete::SetupCommandsListener(impl->command_ac_state, *impl->command_service, ctx.commands_broadcast, screen);

  // -- Model dropdown component ------------------------------------------------

  {
    ButtonOption model_btn_opt = ButtonOption::Ascii();
    model_btn_opt.transform = [state = impl->state](const EntryState&) -> Element {
      std::scoped_lock lock(state->mtx);
      int idx = state->chat.selected_model_idx;
      if (idx >= 0 && idx < static_cast<int>(state->chat.model_dropdown_names.size())) {
        auto label = state->chat.model_dropdown_names[idx];
        auto arrow = state->chat.model_menu_open ? " \u25B4" : " \u25BE";
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

  // -- Session + model loading on background threads -------------------------

  std::thread([impl]() -> void { impl->ResolveSession(); }).detach();
  std::thread([impl]() -> void { impl->LoadModels(); }).detach();

  auto interactive = Container::Vertical({impl->input_component, impl->model_button, impl->model_menu});
  auto comp = Renderer(interactive, [impl]() -> Element { return impl->Render(); });
  comp = CatchEvent(comp, [impl](Event event) -> bool { return impl->HandleEvent(std::move(event)); });
  return comp;
}

}  // namespace ally::views
