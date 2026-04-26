#include "src/components/autocomplete/CommandAutocomplete.hpp"

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
// CheckCommandTrigger
// ---------------------------------------------------------------------------

void CheckCommandTrigger(CommandAutocompleteState& state, const std::string& text, int cursor_pos) {
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
// GetFilteredCommands
// ---------------------------------------------------------------------------

auto GetFilteredCommands(const std::vector<CommandEntry>& commands_cache, std::string_view query) -> std::vector<const CommandEntry*> {
  std::vector<const CommandEntry*> result;
  for (const auto& entry : commands_cache) {
    if (DefaultMatchStrategy(query, entry.name) || DefaultMatchStrategy(query, entry.description)) {
      result.push_back(&entry);
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// SelectCurrentCommand
// ---------------------------------------------------------------------------

void SelectCurrentCommand(CommandAutocompleteState& state, const std::string& current_text, std::string& text_out) {
  if (!state.commands_cache.has_value() || !state.trigger_position.has_value()) {
    return;
  }

  auto filtered = GetFilteredCommands(*state.commands_cache, state.query);
  if (filtered.empty() || state.selected_index < 0 || state.selected_index >= static_cast<int>(filtered.size())) {
    return;
  }

  const auto* entry = filtered[state.selected_index];
  int trigger_pos = *state.trigger_position;
  int cursor_pos = trigger_pos + 1 + static_cast<int>(state.query.size());

  std::string replacement = "/" + entry->name;
  text_out = current_text.substr(0, trigger_pos) + replacement + " " + current_text.substr(cursor_pos);

  state.is_open = false;
  state.trigger_position = std::nullopt;
}

// ---------------------------------------------------------------------------
// HandleCommandKeydown
// ---------------------------------------------------------------------------

auto HandleCommandKeydown(CommandAutocompleteState& state, const std::string& current_text, std::string& text_out,
                          const ftxui::Event& event, const ally::configuration::InputConfig& input_config) -> bool {
  if (!state.is_open) {
    return false;
  }

  if (!state.commands_cache.has_value()) {
    return false;
  }

  auto filtered = GetFilteredCommands(*state.commands_cache, state.query);
  int item_count = static_cast<int>(filtered.size());

  if (input_config.autocomplete.next.matches(event)) {
    if (item_count > 0) {
      state.selected_index = std::min(state.selected_index + 1, item_count - 1);
    }
    return true;
  }

  if (input_config.autocomplete.prev.matches(event)) {
    if (item_count > 0) {
      state.selected_index = std::max(state.selected_index - 1, 0);
    }
    return true;
  }

  if (input_config.autocomplete.dismiss.matches(event)) {
    state.is_open = false;
    state.trigger_position = std::nullopt;
    return true;
  }

  if (input_config.autocomplete.select.matches(event)) {
    SelectCurrentCommand(state, current_text, text_out);
    return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// CommandAutocompleteComponent
// ---------------------------------------------------------------------------

auto CommandAutocompleteComponent(const std::shared_ptr<CommandAutocompleteState>& state, ftxui::ScreenInteractive& screen,
                                  std::function<void(const std::string& new_text)> on_insert) -> ftxui::Component {
  using namespace ftxui;

  auto component = Renderer([state]() -> Element {
    std::scoped_lock lock(state->mutex);

    if (!state->is_open || !state->commands_cache.has_value()) {
      return emptyElement();
    }

    auto& cache = *state->commands_cache;
    auto filtered = GetFilteredCommands(cache, state->query);

    // Clamp selected_index.
    int item_count = static_cast<int>(filtered.size());
    if (item_count > 0) {
      state->selected_index = std::max(0, std::min(state->selected_index, item_count - 1));
    } else {
      state->selected_index = 0;
    }

    if (filtered.empty()) {
      auto empty_row = text("No commands found") | dim | center;
      return vbox({empty_row}) | vscroll_indicator | yframe | xflex | size(HEIGHT, LESS_THAN, 10) | border;
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

    return vbox(std::move(rows)) | vscroll_indicator | yframe | xflex | size(HEIGHT, LESS_THAN, 10) | border;
  });

  // Wrap with CatchEvent for keyboard and mouse handling.
  component = CatchEvent(component, [state, on_insert = std::move(on_insert)](Event event) -> bool {
    std::scoped_lock lock(state->mutex);

    if (!state->is_open || !state->commands_cache.has_value()) {
      return false;
    }

    // Mouse hover: update selected_index to hovered row.
    if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
      int row = event.mouse().y;
      auto filtered = GetFilteredCommands(*state->commands_cache, state->query);
      if (row >= 0 && row < static_cast<int>(filtered.size())) {
        state->selected_index = row;
      }
      return false;  // Don't consume — let render update.
    }

    // Mouse click: select the clicked row.
    if (event.is_mouse() && event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Released) {
      int row = event.mouse().y;
      auto filtered = GetFilteredCommands(*state->commands_cache, state->query);
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
// SetupCommandsListener
// ---------------------------------------------------------------------------

void SetupCommandsListener(const std::shared_ptr<CommandAutocompleteState>& state, ally::services::CommandService& service,
                           watcher::WatcherBroadcast<watcher::CommandsChangedEvent>& broadcast, ftxui::ScreenInteractive& screen) {
  std::thread([state, &service, &broadcast, &screen]() -> void {
    // Initial one-shot load
    service.ListCommands([state, &screen](std::vector<CommandEntry> commands) -> void {
      std::scoped_lock lock(state->mutex);
      state->commands_cache = std::move(commands);
      screen.PostEvent(ftxui::Event::Custom);
    });

    // Subscribe to broadcast for live updates
    auto queue = broadcast.subscribe();

    while (!state->listener_stop.load()) {
      auto events = queue->drain();
      if (!events.empty()) {
        // Any commands change triggers a full re-fetch
        service.ListCommands([state, &screen](std::vector<CommandEntry> commands) -> void {
          std::scoped_lock lock(state->mutex);
          state->commands_cache = std::move(commands);
          screen.PostEvent(ftxui::Event::Custom);
        });
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }).detach();
}

}  // namespace ally::autocomplete
