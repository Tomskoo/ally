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
#include "src/components/autocomplete/SkillAutocomplete.hpp"
#include "src/components/autocomplete/SkillTypes.hpp"
#include "src/components/autocomplete/Types.hpp"
#include "src/components/scrollable/ScrollableNode.hpp"
#include "src/opencode/Service.hpp"
#include "src/rendering/HighlightTheme.hpp"
#include "src/rendering/TreeSitterRenderer.hpp"
#include "src/services/SkillService.hpp"
#include "src/utils/time_format.hpp"

using namespace ftxui;
using ally::components::make_scrollable;

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

// ---------------------------------------------------------------------------
// Helpers
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

void CopyToClipboard(const std::string& content) {
  FILE* pipe = popen("pbcopy", "w");
  if (pipe != nullptr) {
    fwrite(content.data(), 1, content.size(), pipe);
    pclose(pipe);
  }
}

auto NormalizeSelection(const TextCursor& anchor, const TextCursor& cursor) -> std::pair<TextCursor, TextCursor> {
  if (anchor.row < cursor.row || (anchor.row == cursor.row && anchor.col <= cursor.col)) {
    return {anchor, cursor};
  }
  return {cursor, anchor};
}

auto InSelection(int row, int col, const TextCursor& start, const TextCursor& end) -> bool {
  if (row < start.row || row > end.row) { return false;
}
  if (start.row == end.row) {
    return col >= start.col && col <= end.col;
  }
  if (row == start.row) { return col >= start.col;
}
  if (row == end.row) { return col <= end.col;
}
  return true;
}

auto ExtractSelection(const std::vector<std::string>& lines, const TextCursor& anchor, const TextCursor& cursor) -> std::string {
  auto [start, end] = NormalizeSelection(anchor, cursor);
  if (lines.empty()) { return "";
}

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
  // Create new entry.
  opencode::MessageWithParts new_msg;
  new_msg.info.id = msg_id;
  new_msg.info.extra = nlohmann::json::object();
  state.chat.messages.push_back(std::move(new_msg));
  return state.chat.messages.back();
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
    state->chat.messages = std::move(opencode::get_value(std::move(result)));
    state->chat.rendered_parts_cache.clear();
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
  std::shared_ptr<autocomplete::SkillAutocompleteState> skill_ac_state = std::make_shared<autocomplete::SkillAutocompleteState>();
  std::shared_ptr<autocomplete::ArtifactAutocompleteState> artifact_ac_state =
      std::make_shared<autocomplete::ArtifactAutocompleteState>();
  std::shared_ptr<autocomplete::AutocompleteState> file_ac_state = std::make_shared<autocomplete::AutocompleteState>();

  // Autocomplete overlay components
  Component skill_ac_component;
  Component artifact_ac_component;
  Component file_ac_component;

  // Skill service (must outlive the skills listener thread)
  std::shared_ptr<services::SkillService> skill_service;

  // Background threads
  std::atomic<bool> stop{false};
  std::thread artifact_event_thread;

  ~StageViewImpl() {
    stop.store(true, std::memory_order_relaxed);
    skill_ac_state->listener_stop.store(true, std::memory_order_relaxed);
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
        theme(rendering::HighlightTheme::LoadDefault()),
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

    if (interaction == InteractionMode::Visual) {
      return RenderVisualMode();
    }

    if (panel == PanelMode::Artifact) {
      return RenderArtifactPanel();
    }
    return RenderChatPanel();
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
      content_body = paragraph(*raw_content) | border | flex;
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
            emptyElement() | size(HEIGHT, EQUAL, 2),
        }),
    });
  }

  auto RenderToolResultPart(const std::string& msg_id, size_t part_idx, const std::string& content) const -> Element {
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
    });
  }

  auto RenderMessage(const opencode::MessageWithParts& msg) -> Element {
    std::string role = (msg.info.extra.is_object() && msg.info.extra.contains("role") && msg.info.extra["role"].is_string())
                           ? msg.info.extra["role"].get<std::string>()
                           : std::string{"unknown"};
    std::string icon;
    if (role == "user") {
      icon = "$ ";
    } else if (role == "assistant") {
      icon = "\u2726 ";
    } else {
      icon = ". ";
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
          rendering::TreeSitterRenderer renderer(theme);
          auto blocks = renderer.Render(content);
          Elements block_els;
          for (auto& block : blocks) {
            block_els.push_back(std::move(block.element));
          }
          part_el = vbox(std::move(block_els));
          cached = {content.size(), part_el};
        }
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
        message_elements.push_back(RenderMessage(msgs[idx]));
        message_elements.push_back(text(""));
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

    auto chat_body = vbox(std::move(message_elements)) | flex;

    return dbox({
        vbox({
            make_scrollable(chat_body | vscroll_indicator, &state->chat.chat_scroll_y) | flex,
            separator(),
            RenderInputBar(),
        }),
        vbox({
            filler(),
            RenderAutocompleteOverlay(),
            emptyElement() | size(HEIGHT, EQUAL, 2),
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

  auto RenderInputBar() const -> Element {
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

    auto prompt_label = (interaction == InteractionMode::Insert) ? (text(" $ ") | bold | inverted) : (text(" $ ") | bold | dim);

    auto send_label = !input_text.empty() ? (text(" M-⏎ Send ") | bold | inverted) : (text(" M-⏎ Send ") | dim);

    // Only render the live input component (with cursor) in Insert mode.
    // In Normal/Visual mode, show the text without a cursor.
    auto input_el = (interaction == InteractionMode::Insert) ? input_component->Render()
                                                             : text(input_text.empty() ? "type a message..." : input_text) | dim;

    return hbox({
        mode_label,
        separator(),
        prompt_label,
        input_el | flex,
        separator(),
        send_label,
    });
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

  // -- Event handling ---------------------------------------------------------

  auto HandleEvent(Event event) -> bool {
    // Drain SSE event queue on every event cycle.
    if (event == Event::Custom) {
      auto events = ctx.event_queue.drain();
      for (const auto& evt : events) {
        DispatchSseEvent(evt);
      }
      return false;
    }

    // Tab toggles panel (Artifact ↔ Chat). Also consume TabReverse.
    if (event == Event::Tab || event == Event::TabReverse) {
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
    // Let Escape bubble up to main.cpp for parent navigation.
    if (event == Event::Escape) {
      return false;
    }

    // 'v' enters Visual mode.
    if (event == Event::Character('v')) {
      EnterVisualMode();
      return true;
    }

    // 'i' enters Insert mode (focus input).
    if (event == Event::Character('i')) {
      {
        std::scoped_lock lock(state->mtx);
        state->interaction = InteractionMode::Insert;
      }
      input_component->TakeFocus();
      screen.PostEvent(Event::Custom);
      return true;
    }

    // Tab is handled in HandleEvent before mode dispatch, so '=' is unused.

    // Stage navigation with [ and ].
    if (event == Event::Character(']') || event == Event::Character('[')) {
      std::string target;
      {
        std::scoped_lock lock(state->mtx);
        auto& stages = state->all_workflow_stages;
        auto it = std::find(stages.begin(), stages.end(), stage);
        if (it == stages.end()) { return true;
}

        if (event == Event::Character(']') && std::next(it) != stages.end()) {
          target = *std::next(it);
        } else if (event == Event::Character('[') && it != stages.begin()) {
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

    // Scroll (arrow keys).
    if (event == Event::ArrowUp || event == Event::ArrowDown) {
      int delta = (event == Event::ArrowUp) ? -kScrollLines : kScrollLines;
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

    if (panel == PanelMode::Artifact) {
      // Toggle rendered/raw.
      if (event == Event::Character('r')) {
        std::scoped_lock lock(state->mtx);
        state->review_view_mode =
            (state->review_view_mode == ArtifactViewMode::Rendered) ? ArtifactViewMode::Raw : ArtifactViewMode::Rendered;
        screen.PostEvent(Event::Custom);
        return true;
      }

      // Force reload.
      if (event == Event::Character('R')) {
        RefreshArtifactStages();
        LoadReviewArtifact(stage);
        return true;
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
    if (event == Event::Special({27, 13}) && !input_text.empty()) {
      DoSend();
      return true;
    }

    // Consume all remaining events in Normal mode — the input component
    // should only receive events when in Insert mode.
    return true;
  }

  auto HandleVisualEvent(Event& event) -> bool {
    // Escape or 'n' exits visual mode.
    if (event == Event::Escape || event == Event::Character('n')) {
      {
        std::scoped_lock lock(state->mtx);
        state->interaction = InteractionMode::Normal;
        state->visual = std::nullopt;
      }
      screen.PostEvent(Event::Custom);
      return true;
    }

    // 'y' yanks selection to clipboard.
    if (event == Event::Character('y')) {
      std::string selected;
      {
        std::scoped_lock lock(state->mtx);
        if (state->visual.has_value()) {
          selected = ExtractSelection(state->visual->lines, state->visual->anchor, state->visual->cursor);
        }
        state->interaction = InteractionMode::Normal;
        state->visual = std::nullopt;
      }
      if (!selected.empty()) {
        CopyToClipboard(selected);
      }
      screen.PostEvent(Event::Custom);
      return true;
    }

    // hjkl cursor movement.
    if (event == Event::Character('h') || event == Event::Character('j') || event == Event::Character('k') ||
        event == Event::Character('l')) {
      std::scoped_lock lock(state->mtx);
      if (!state->visual.has_value()) { return true;
}
      auto& vs = *state->visual;

      if (event == Event::Character('h')) {
        vs.cursor.col = std::max(0, vs.cursor.col - 1);
      } else if (event == Event::Character('l')) {
        int max_col = vs.cursor.row < static_cast<int>(vs.lines.size()) ? std::max(0, static_cast<int>(vs.lines[vs.cursor.row].size()) - 1) : 0;
        vs.cursor.col = std::min(vs.cursor.col + 1, max_col);
      } else if (event == Event::Character('k')) {
        vs.cursor.row = std::max(0, vs.cursor.row - 1);
        int max_col = vs.cursor.row < static_cast<int>(vs.lines.size()) ? std::max(0, static_cast<int>(vs.lines[vs.cursor.row].size()) - 1) : 0;
        vs.cursor.col = std::min(vs.cursor.col, max_col);
      } else if (event == Event::Character('j')) {
        vs.cursor.row = std::min(vs.cursor.row + 1, std::max(0, static_cast<int>(vs.lines.size()) - 1));
        int max_col = vs.cursor.row < static_cast<int>(vs.lines.size()) ? std::max(0, static_cast<int>(vs.lines[vs.cursor.row].size()) - 1) : 0;
        vs.cursor.col = std::min(vs.cursor.col, max_col);
      }

      screen.PostEvent(Event::Custom);
      return true;
    }

    // Consume all other keys in visual mode.
    return true;
  }

  auto HandleInsertEvent(Event& event) -> bool {
    // Escape: close any open autocomplete overlay first, or exit insert mode.
    if (event == Event::Escape) {
      if (skill_ac_state->is_open || artifact_ac_state->is_open || file_ac_state->is_open) {
        skill_ac_state->is_open = false;
        artifact_ac_state->is_open = false;
        file_ac_state->is_open = false;
        screen.PostEvent(Event::Custom);
        return true;
      }
      {
        std::scoped_lock lock(state->mtx);
        state->interaction = InteractionMode::Normal;
      }
      // Keep focus on input_component — the CatchEvent wrapper handles
      // Normal-mode keys before FTXUI routes them to the focused child.
      screen.PostEvent(Event::Custom);
      return true;
    }

    // Alt+Enter sends message, stay in insert mode.
    if (event == Event::Special({27, 13}) && !input_text.empty()) {
      DoSend();
      return true;
    }

    // Route events through autocomplete handlers when overlay is open.
    if (skill_ac_state->is_open) {
      std::scoped_lock lock(skill_ac_state->mutex);
      auto trigger = skill_ac_state->trigger_position;
      std::string new_text;
      if (autocomplete::HandleSkillKeydown(*skill_ac_state, input_text, new_text, event)) {
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
      if (autocomplete::HandleArtifactKeydown(*artifact_ac_state, input_text, new_text, event)) {
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
    }

    // Delegate everything else to the input component.
    return false;
  }

  // -- Mode transitions -------------------------------------------------------

  void EnterVisualMode() {
    std::scoped_lock lock(state->mtx);

    std::vector<std::string> lines;
    if (state->panel == PanelMode::Artifact && state->review_content.has_value()) {
      lines = SplitLines(*state->review_content);
    }

    if (lines.empty()) { return;  // nothing to select
}

    VisualModeState vs;
    vs.anchor = {0, 0};
    vs.cursor = {0, 0};
    vs.lines = std::move(lines);

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
    auto& service = ctx.artifact_service;
    auto& scr = screen;

    std::thread([s, t, tid, thid, stage_slug, &service, &scr] -> void {
      auto content = service.get_artifact(tid, thid, stage_slug);

      std::vector<Element> rendered;
      if (content.has_value() && !content->empty()) {
        rendering::TreeSitterRenderer renderer(t);
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
      ApplyPartDelta(state, evt.data);
      screen.PostEvent(Event::Custom);
    } else if (type == "message.part.updated") {
      ApplyPartUpdated(state, evt.data);
      screen.PostEvent(Event::Custom);
    } else if (type == "message.created") {
      auto sptr = state;
      auto& ocs = ctx.opencode_state;
      auto& ocm = ctx.opencode_mutex;
      auto& scr = screen;
      std::thread([sptr, &ocs, &ocm, &scr] -> void { RefreshMessages(sptr, ocs, ocm, scr); }).detach();
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
    }
    // Silently ignore: server.heartbeat, server.connected, session.updated,
    // session.diff, message.updated
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

    std::thread([sptr, sid, trimmed, &ocs, &ocm, &scr] -> void {
      opencode::AsyncPromptRequest req;
      req.data = {{"parts", {{{"type", "text"}, {"text", trimmed}}}}};
      auto result = opencode::PromptAsync(ocs, ocm, sid, req);
      if (opencode::is_ok(result)) {
        // Query session status after sending
        auto sess_result = opencode::GetSession(ocs, ocm, sid);
        // Also check messages
      } else {
        const auto& err = opencode::get_error(result);
        std::scoped_lock lock(sptr->mtx);
        sptr->chat.error_msg = "Send failed: " + err.message;
        sptr->chat.is_loading = false;
      }
      scr.PostEvent(Event::Custom);
    }).detach();

    screen.PostEvent(Event::Custom);
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
  input_opts.on_change = [impl] -> void {
    // Check all three autocomplete triggers on every text change.
    {
      std::scoped_lock lock(impl->skill_ac_state->mutex);
      autocomplete::CheckSkillTrigger(*impl->skill_ac_state, impl->input_text, impl->cursor_pos);
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
      autocomplete::SkillAutocompleteComponent(impl->skill_ac_state, screen, [impl](const std::string& new_text) -> void {
        auto trigger = impl->skill_ac_state->trigger_position;
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
      });

  // -- Background listeners ---------------------------------------------------

  // Skill listener
  impl->skill_service = std::make_shared<services::SkillService>(ctx.project_root.string());
  autocomplete::SetupSkillsListener(impl->skill_ac_state, *impl->skill_service, ctx.skills_broadcast, screen);

  // Artifact listener
  autocomplete::SetupArtifactsListener(impl->artifact_ac_state, ctx.project_root.string(), task_id, thread_id,
                                       impl->workflow_stages, ctx.artifact_broadcast, screen);

  // -- Initial artifact load on a background thread ---------------------------

  std::thread([impl] -> void {
    impl->RefreshArtifactStages();
    impl->LoadReviewArtifact(impl->stage);
  }).detach();

  // -- Chat session resolution on a background thread -------------------------

  std::thread([impl] -> void { impl->ResolveSession(); }).detach();

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

  auto comp = Renderer(impl->input_component, [impl] -> Element { return impl->Render(); });
  comp = CatchEvent(comp, [impl](Event event) -> bool { return impl->HandleEvent(std::move(event)); });
  return comp;
}

}  // namespace ally::views
