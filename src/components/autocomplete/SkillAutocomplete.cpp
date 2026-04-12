#include "src/components/autocomplete/SkillAutocomplete.hpp"

#include <algorithm>
#include <chrono>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <thread>
#include <utility>

#include "src/components/autocomplete/FileAutocomplete.hpp"  // DefaultMatchStrategy

namespace ally::autocomplete {

// ---------------------------------------------------------------------------
// CheckSkillTrigger
// ---------------------------------------------------------------------------

void CheckSkillTrigger(SkillAutocompleteState& state, const std::string& text, int cursor_pos) {
  // Clamp cursor_pos.
  cursor_pos = std::max(cursor_pos, 0);
  cursor_pos = std::min(cursor_pos, static_cast<int>(text.size()));

  // Scan backwards for '/'.
  int slash_pos = -1;
  for (int idx = cursor_pos - 1; idx >= 0; --idx) {
    if (text[idx] == '/') {
      slash_pos = idx;
      break;
    }
  }

  if (slash_pos < 0) {
    if (state.is_open) {
      state.is_open = false;
      state.trigger_position = std::nullopt;
    }
    return;
  }

  // '/' must be at position 0 or preceded by whitespace.
  if (slash_pos > 0) {
    char prev = text[slash_pos - 1];
    if (prev != ' ' && prev != '\t' && prev != '\n') {
      if (state.is_open) {
        state.is_open = false;
        state.trigger_position = std::nullopt;
      }
      return;
    }
  }

  // Extract query between '/' + 1 and cursor.
  std::string query = text.substr(slash_pos + 1, cursor_pos - slash_pos - 1);

  // Close if query contains space, newline, or another '/'.
  if (query.find(' ') != std::string::npos || query.find('\n') != std::string::npos || query.find('/') != std::string::npos) {
    state.is_open = false;
    state.trigger_position = std::nullopt;
    return;
  }

  state.trigger_position = slash_pos;
  state.query = query;
  state.selected_index = 0;
  state.is_open = true;
}

// ---------------------------------------------------------------------------
// GetFilteredSkills
// ---------------------------------------------------------------------------

auto GetFilteredSkills(const std::vector<SkillEntry>& skills_cache, std::string_view query) -> std::vector<const SkillEntry*> {
  std::vector<const SkillEntry*> result;
  for (const auto& entry : skills_cache) {
    if (DefaultMatchStrategy(query, entry.dir_name) || DefaultMatchStrategy(query, entry.name)) {
      result.push_back(&entry);
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// SelectCurrentSkill
// ---------------------------------------------------------------------------

void SelectCurrentSkill(SkillAutocompleteState& state, const std::string& current_text, std::string& text_out) {
  if (!state.skills_cache.has_value() || !state.trigger_position.has_value()) {
    return;
  }

  auto filtered = GetFilteredSkills(*state.skills_cache, state.query);
  if (filtered.empty() || state.selected_index < 0 || state.selected_index >= static_cast<int>(filtered.size())) {
    return;
  }

  const auto* entry = filtered[state.selected_index];
  int trigger_pos = *state.trigger_position;
  int cursor_pos = trigger_pos + 1 + static_cast<int>(state.query.size());

  std::string replacement = "/" + entry->dir_name;
  text_out = current_text.substr(0, trigger_pos) + replacement + " " + current_text.substr(cursor_pos);

  state.is_open = false;
  state.trigger_position = std::nullopt;
}

// ---------------------------------------------------------------------------
// HandleSkillKeydown
// ---------------------------------------------------------------------------

auto HandleSkillKeydown(SkillAutocompleteState& state, const std::string& current_text, std::string& text_out,
                        const ftxui::Event& event) -> bool {
  if (!state.is_open) {
    return false;
  }

  if (!state.skills_cache.has_value()) {
    return false;
  }

  auto filtered = GetFilteredSkills(*state.skills_cache, state.query);
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
    SelectCurrentSkill(state, current_text, text_out);
    return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// SkillAutocompleteComponent
// ---------------------------------------------------------------------------

auto SkillAutocompleteComponent(const std::shared_ptr<SkillAutocompleteState>& state, ftxui::ScreenInteractive& screen,
                                std::function<void(const std::string& new_text)> on_insert) -> ftxui::Component {
  using namespace ftxui;

  auto component = Renderer([state]() -> Element {
    std::scoped_lock lock(state->mutex);

    if (!state->is_open || !state->skills_cache.has_value()) {
      return text("");
    }

    auto& cache = *state->skills_cache;
    auto filtered = GetFilteredSkills(cache, state->query);

    // Clamp selected_index.
    int item_count = static_cast<int>(filtered.size());
    if (item_count > 0) {
      state->selected_index = std::max(0, std::min(state->selected_index, item_count - 1));
    } else {
      state->selected_index = 0;
    }

    if (filtered.empty()) {
      auto empty_row = text("No skills found") | dim | center;
      return vbox({empty_row}) | vscroll_indicator | yframe | size(WIDTH, EQUAL, 60) | size(HEIGHT, LESS_THAN, 10) | border;
    }

    Elements rows;
    for (int idx = 0; idx < item_count; ++idx) {
      const auto* entry = filtered[idx];
      bool selected = (idx == state->selected_index);

      auto icon = text("* ");
      auto name_el = text(entry->name);

      Elements row_parts = {icon, name_el};
      if (!entry->description.empty()) {
        row_parts.push_back(text(" "));
        row_parts.push_back(text(entry->description) | dim | flex);
      }

      auto row = hbox(std::move(row_parts));
      if (selected) {
        row = row | inverted | focus;
      }
      rows.push_back(row);
    }

    return vbox(std::move(rows)) | vscroll_indicator | yframe | size(WIDTH, EQUAL, 60) | size(HEIGHT, LESS_THAN, 10) | border;
  });

  // Wrap with CatchEvent for keyboard and mouse handling.
  component = CatchEvent(component, [state, on_insert = std::move(on_insert)](Event event) -> bool {
    std::scoped_lock lock(state->mutex);

    if (!state->is_open || !state->skills_cache.has_value()) {
      return false;
    }

    // Mouse hover: update selected_index to hovered row.
    if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
      // Mouse events within the overlay update selection.
      // The y coordinate relative to the overlay start gives the row.
      int row = event.mouse().y;
      auto filtered = GetFilteredSkills(*state->skills_cache, state->query);
      if (row >= 0 && row < static_cast<int>(filtered.size())) {
        state->selected_index = row;
      }
      return false;  // Don't consume — let render update.
    }

    // Mouse click: select the clicked row.
    if (event.is_mouse() && event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Released) {
      int row = event.mouse().y;
      auto filtered = GetFilteredSkills(*state->skills_cache, state->query);
      if (row >= 0 && row < static_cast<int>(filtered.size())) {
        state->selected_index = row;
        // Build the text with the selection.
        // We need to provide a dummy current_text — the caller
        // will receive the new_text via on_insert.
        // However, we don't have access to the current text here.
        // So we signal via on_insert with an empty string; the
        // keyboard path handles this differently.
      }
      return false;
    }

    // Keyboard: need current_text for SelectCurrentSkill.
    // The CatchEvent handler doesn't have direct access to the text
    // buffer, so keyboard events are handled by the caller through
    // HandleSkillKeydown before reaching this component.
    return false;
  });

  return component;
}

// ---------------------------------------------------------------------------
// SetupSkillsListener
// ---------------------------------------------------------------------------

void SetupSkillsListener(const std::shared_ptr<SkillAutocompleteState>& state, ally::services::SkillService& service,
                         watcher::WatcherBroadcast<watcher::SkillsChangedEvent>& broadcast, ftxui::ScreenInteractive& screen) {
  std::thread([state, &service, &broadcast, &screen]() -> void {
    // Initial one-shot load
    service.ListSkills([state, &screen](std::vector<SkillEntry> skills) -> void {
      std::scoped_lock lock(state->mutex);
      state->skills_cache = std::move(skills);
      screen.PostEvent(ftxui::Event::Custom);
    });

    // Subscribe to broadcast for live updates
    auto queue = broadcast.subscribe();

    while (!state->listener_stop.load()) {
      auto events = queue->drain();
      if (!events.empty()) {
        // Any skills change triggers a full re-fetch
        service.ListSkills([state, &screen](std::vector<SkillEntry> skills) -> void {
          std::scoped_lock lock(state->mutex);
          state->skills_cache = std::move(skills);
          screen.PostEvent(ftxui::Event::Custom);
        });
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }).detach();
}

}  // namespace ally::autocomplete
