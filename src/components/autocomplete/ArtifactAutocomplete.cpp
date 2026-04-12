#include "src/components/autocomplete/ArtifactAutocomplete.hpp"

#include <algorithm>
#include <chrono>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <thread>
#include <utility>

#include "src/commands/storage/Storage.hpp"
#include "src/components/autocomplete/FileAutocomplete.hpp"  // DefaultMatchStrategy

namespace ally::autocomplete {

// ---------------------------------------------------------------------------
// CheckArtifactTrigger
// ---------------------------------------------------------------------------

void CheckArtifactTrigger(ArtifactAutocompleteState& state, const std::string& text, int cursor_pos) {
  // Clamp cursor_pos.
  cursor_pos = std::max(cursor_pos, 0);
  cursor_pos = std::min(cursor_pos, static_cast<int>(text.size()));

  // Scan backwards for '$'.
  int dollar_pos = -1;
  for (int idx = cursor_pos - 1; idx >= 0; --idx) {
    if (text[idx] == '$') {
      dollar_pos = idx;
      break;
    }
  }

  if (dollar_pos < 0) {
    if (state.is_open) {
      state.is_open = false;
      state.trigger_position = std::nullopt;
    }
    return;
  }

  // '$' must be at position 0 or preceded by whitespace.
  if (dollar_pos > 0) {
    char prev = text[dollar_pos - 1];
    if (prev != ' ' && prev != '\t' && prev != '\n') {
      if (state.is_open) {
        state.is_open = false;
        state.trigger_position = std::nullopt;
      }
      return;
    }
  }

  // Extract query between '$' + 1 and cursor.
  std::string query = text.substr(dollar_pos + 1, cursor_pos - dollar_pos - 1);

  // Close if query contains space, newline, or another '$'.
  if (query.find(' ') != std::string::npos || query.find('\n') != std::string::npos || query.find('$') != std::string::npos) {
    state.is_open = false;
    state.trigger_position = std::nullopt;
    return;
  }

  state.trigger_position = dollar_pos;
  state.query = query;
  state.selected_index = 0;
  state.is_open = true;
}

// ---------------------------------------------------------------------------
// GetFilteredArtifacts
// ---------------------------------------------------------------------------

auto GetFilteredArtifacts(const std::vector<ArtifactEntry>& artifacts_cache, std::string_view query)
    -> std::vector<const ArtifactEntry*> {
  std::vector<const ArtifactEntry*> result;
  for (const auto& entry : artifacts_cache) {
    if (DefaultMatchStrategy(query, entry.stage) || DefaultMatchStrategy(query, entry.product)) {
      result.push_back(&entry);
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// SelectCurrentArtifact
// ---------------------------------------------------------------------------

void SelectCurrentArtifact(ArtifactAutocompleteState& state, const std::string& current_text, std::string& text_out) {
  if (!state.artifacts_cache.has_value() || !state.trigger_position.has_value()) {
    return;
  }

  auto filtered = GetFilteredArtifacts(*state.artifacts_cache, state.query);
  if (filtered.empty() || state.selected_index < 0 || state.selected_index >= static_cast<int>(filtered.size())) {
    return;
  }

  const auto* entry = filtered[state.selected_index];
  int trigger_pos = *state.trigger_position;
  int cursor_pos = trigger_pos + 1 + static_cast<int>(state.query.size());

  std::string replacement = "@" + entry->relative_path;
  text_out = current_text.substr(0, trigger_pos) + replacement + " " + current_text.substr(cursor_pos);

  state.is_open = false;
  state.trigger_position = std::nullopt;
}

// ---------------------------------------------------------------------------
// HandleArtifactKeydown
// ---------------------------------------------------------------------------

auto HandleArtifactKeydown(ArtifactAutocompleteState& state, const std::string& current_text, std::string& text_out,
                           const ftxui::Event& event) -> bool {
  if (!state.is_open) {
    return false;
  }

  if (!state.artifacts_cache.has_value()) {
    return false;
  }

  auto filtered = GetFilteredArtifacts(*state.artifacts_cache, state.query);
  int item_count = static_cast<int>(filtered.size());

  if (event == ftxui::Event::ArrowDown) {
    if (item_count > 0) {
      state.selected_index = std::min(state.selected_index + 1, item_count - 1);
    }
    return true;
  }

  if (event == ftxui::Event::ArrowUp) {
    if (item_count > 0) {
      state.selected_index = std::max(state.selected_index - 1, 0);
    }
    return true;
  }

  if (event == ftxui::Event::Escape) {
    state.is_open = false;
    state.trigger_position = std::nullopt;
    return true;
  }

  if (event == ftxui::Event::Return || event == ftxui::Event::Tab) {
    SelectCurrentArtifact(state, current_text, text_out);
    return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// ArtifactAutocompleteComponent
// ---------------------------------------------------------------------------

auto ArtifactAutocompleteComponent(const std::shared_ptr<ArtifactAutocompleteState>& state, ftxui::ScreenInteractive& screen,
                                   std::function<void(const std::string& new_text)> on_insert) -> ftxui::Component {
  using namespace ftxui;

  auto component = Renderer([state]() -> Element {
    std::scoped_lock lock(state->mutex);

    if (!state->is_open || !state->artifacts_cache.has_value()) {
      return text("");
    }

    auto& cache = *state->artifacts_cache;
    auto filtered = GetFilteredArtifacts(cache, state->query);

    // Clamp selected_index.
    int item_count = static_cast<int>(filtered.size());
    if (item_count > 0) {
      state->selected_index = std::max(0, std::min(state->selected_index, item_count - 1));
    } else {
      state->selected_index = 0;
    }

    if (filtered.empty()) {
      auto empty_row = text("No artifacts found") | dim | center;
      return vbox({empty_row}) | vscroll_indicator | yframe | xflex | size(HEIGHT, LESS_THAN, 10) | border;
    }

    Elements rows;
    for (int idx = 0; idx < item_count; ++idx) {
      const auto* entry = filtered[idx];
      bool selected = (idx == state->selected_index);

      auto icon = text("$ ");
      auto stage_el = text(entry->stage);

      Elements row_parts = {icon, stage_el};
      if (!entry->product.empty()) {
        row_parts.push_back(text(" "));
        row_parts.push_back(text(entry->product) | dim | flex);
      }

      auto row = hbox(std::move(row_parts));
      if (selected) {
        row = row | inverted | focus;
      }
      rows.push_back(row);
    }

    return vbox(std::move(rows)) | vscroll_indicator | yframe | xflex | size(HEIGHT, LESS_THAN, 10) | border;
  });

  // Wrap with CatchEvent for mouse handling.
  component = CatchEvent(component, [state, on_insert = std::move(on_insert)](Event event) -> bool {
    std::scoped_lock lock(state->mutex);

    if (!state->is_open || !state->artifacts_cache.has_value()) {
      return false;
    }

    // Mouse hover: update selected_index to hovered row.
    if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
      int row = event.mouse().y;
      auto filtered = GetFilteredArtifacts(*state->artifacts_cache, state->query);
      if (row >= 0 && row < static_cast<int>(filtered.size())) {
        state->selected_index = row;
      }
      return false;
    }

    // Mouse click: select the clicked row.
    if (event.is_mouse() && event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Released) {
      int row = event.mouse().y;
      auto filtered = GetFilteredArtifacts(*state->artifacts_cache, state->query);
      if (row >= 0 && row < static_cast<int>(filtered.size())) {
        state->selected_index = row;
      }
      return false;
    }

    return false;
  });

  return component;
}

// ---------------------------------------------------------------------------
// SetupArtifactsListener
// ---------------------------------------------------------------------------

void SetupArtifactsListener(const std::shared_ptr<ArtifactAutocompleteState>& state, const std::string& project_root,
                            const std::string& task_id, const std::string& thread_id,
                            const std::vector<models::WorkflowStage>& stages,
                            watcher::WatcherBroadcast<watcher::ArtifactChangedEvent>& broadcast, ftxui::ScreenInteractive& screen) {
  std::thread([state, project_root, task_id, thread_id, stages, &broadcast, &screen]() -> void {
    // Initial one-shot load
    auto artifacts = commands::storage::ListArtifactCompletions(project_root, task_id, thread_id, stages);
    {
      std::scoped_lock lock(state->mutex);
      state->artifacts_cache = std::move(artifacts);
    }
    screen.PostEvent(ftxui::Event::Custom);

    // Subscribe to broadcast for live updates
    auto queue = broadcast.subscribe();

    while (!state->listener_stop.load()) {
      auto events = queue->drain();
      if (!events.empty()) {
        // Any artifact change triggers a full re-fetch
        auto refreshed = commands::storage::ListArtifactCompletions(project_root, task_id, thread_id, stages);
        {
          std::scoped_lock lock(state->mutex);
          state->artifacts_cache = std::move(refreshed);
        }
        screen.PostEvent(ftxui::Event::Custom);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }).detach();
}

}  // namespace ally::autocomplete
