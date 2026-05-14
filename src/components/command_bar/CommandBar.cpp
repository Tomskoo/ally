#include "src/components/command_bar/CommandBar.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace ally::components {

namespace {

/// Extract the first word from input (the command name portion) for autocomplete matching.
auto FirstWord(const std::string& text) -> std::string {
  auto space = text.find(' ');
  if (space == std::string::npos) {
    return text;
  }
  return text.substr(0, space);
}

/// Check if input already contains a space (command name is finalized).
auto HasSpace(const std::string& text) -> bool {
  return text.find(' ') != std::string::npos;
}

/// Extract the argument portion after the command name.
auto ArgAfterSpace(const std::string& text) -> std::string {
  auto space = text.find(' ');
  if (space == std::string::npos) return "";
  auto start = space + 1;
  if (start >= text.size()) return "";
  return text.substr(start);
}

/// Case-insensitive subsequence match.
auto FuzzyMatch(std::string_view query, std::string_view target) -> bool {
  if (query.empty()) return true;
  size_t qi = 0;
  for (char c : target) {
    if (std::tolower(static_cast<unsigned char>(c)) == std::tolower(static_cast<unsigned char>(query[qi]))) {
      ++qi;
      if (qi == query.size()) return true;
    }
  }
  return false;
}

/// Returns fuzzy-filtered argument candidates if in argument mode, empty vector otherwise.
auto GetArgCandidates(const std::string& input_text, const CommandRegistry& registry) -> std::vector<std::pair<std::string, std::string>> {
  if (!HasSpace(input_text)) return {};
  auto* reg = registry.FindExact(FirstWord(input_text));
  if (!reg || !reg->arg_provider) return {};
  auto candidates = reg->arg_provider();
  auto query = ArgAfterSpace(input_text);
  std::vector<std::pair<std::string, std::string>> filtered;
  for (const auto& c : candidates) {
    if (FuzzyMatch(query, c.first)) {
      filtered.push_back(c);
    }
  }
  return filtered;
}

void RefreshAutocomplete(CommandBarState& state, const CommandRegistry& registry) {
  if (HasSpace(state.input_text)) {
    auto filtered = GetArgCandidates(state.input_text, registry);
    state.autocomplete_open = !filtered.empty();
    state.autocomplete_selected = std::clamp(state.autocomplete_selected, 0, std::max(0, static_cast<int>(filtered.size()) - 1));
    return;
  }
  auto query = FirstWord(state.input_text);
  auto matches = registry.Match(query);
  state.autocomplete_open = !matches.empty() && !state.input_text.empty();
  state.autocomplete_selected = std::clamp(state.autocomplete_selected, 0, std::max(0, static_cast<int>(matches.size()) - 1));
}

}  // namespace

auto RenderCommandBar(const CommandBarState& state) -> Element {
  // Build the text with cursor indicator.
  std::string before = state.input_text.substr(0, state.cursor_pos);
  std::string after = (state.cursor_pos < static_cast<int>(state.input_text.size()))
                          ? state.input_text.substr(state.cursor_pos)
                          : "";

  Element cursor_char;
  if (state.cursor_pos < static_cast<int>(state.input_text.size())) {
    cursor_char = text(std::string(1, state.input_text[state.cursor_pos])) | inverted;
    after = state.input_text.substr(state.cursor_pos + 1);
  } else {
    cursor_char = text(" ") | inverted;
  }

  return hbox({
      text(":") | bold,
      text(before),
      cursor_char,
      text(after),
  });
}

auto RenderCommandAutocomplete(const CommandBarState& state, const CommandRegistry& registry) -> Element {
  if (!state.autocomplete_open) {
    return emptyElement();
  }

  // Argument autocomplete mode.
  if (HasSpace(state.input_text)) {
    auto filtered = GetArgCandidates(state.input_text, registry);
    if (filtered.empty()) return emptyElement();

    int max_items = 8;
    int item_count = static_cast<int>(filtered.size());
    int display_count = std::min(item_count, max_items);

    Elements rows;
    for (int i = 0; i < display_count; ++i) {
      bool selected = (i == state.autocomplete_selected);
      Elements row_parts;
      row_parts.push_back(text(" " + filtered[i].first + " "));
      if (!filtered[i].second.empty()) {
        row_parts.push_back(text(filtered[i].second) | dim);
      }
      auto row = hbox(std::move(row_parts));
      if (selected) {
        row = row | inverted;
      }
      rows.push_back(row);
    }

    if (item_count > max_items) {
      rows.push_back(text(" +" + std::to_string(item_count - max_items) + " more...") | dim);
    }

    return vbox(std::move(rows)) | border;
  }

  // Command name autocomplete mode.
  auto query = FirstWord(state.input_text);
  auto matches = registry.Match(query);

  if (matches.empty()) {
    return emptyElement();
  }

  int max_items = 8;
  int item_count = static_cast<int>(matches.size());
  int display_count = std::min(item_count, max_items);

  Elements rows;
  for (int i = 0; i < display_count; ++i) {
    const auto* entry = matches[i];
    bool selected = (i == state.autocomplete_selected);

    Elements row_parts;
    row_parts.push_back(text(" :" + entry->name + " "));

    if (!entry->description.empty()) {
      row_parts.push_back(text(entry->description) | dim);
    }

    auto row = hbox(std::move(row_parts));
    if (selected) {
      row = row | inverted;
    }
    rows.push_back(row);
  }

  if (item_count > max_items) {
    rows.push_back(text(" +" + std::to_string(item_count - max_items) + " more...") | dim);
  }

  return vbox(std::move(rows)) | border;
}

auto HandleCommandBarEvent(CommandBarState& state, CommandRegistry& registry, const Event& event) -> bool {
  if (!state.is_active) {
    return false;
  }

  // Escape — cancel
  if (event == Event::Escape) {
    state.Deactivate();
    return true;
  }

  // Enter — execute (accept autocomplete selection first if open)
  if (event == Event::Return) {
    if (state.autocomplete_open) {
      if (HasSpace(state.input_text)) {
        auto filtered = GetArgCandidates(state.input_text, registry);
        if (!filtered.empty() && state.autocomplete_selected < static_cast<int>(filtered.size())) {
          state.input_text = FirstWord(state.input_text) + " " + filtered[state.autocomplete_selected].first;
        }
      } else {
        auto matches = registry.Match(FirstWord(state.input_text));
        if (!matches.empty() && state.autocomplete_selected < static_cast<int>(matches.size())) {
          state.input_text = matches[state.autocomplete_selected]->name;
        }
      }
    }
    auto cmd = state.input_text;
    state.PushHistory(cmd);
    state.Deactivate();
    registry.Execute(cmd);
    return true;
  }

  // Backspace
  if (event == Event::Backspace) {
    if (state.cursor_pos > 0) {
      state.input_text.erase(state.cursor_pos - 1, 1);
      state.cursor_pos--;
      state.history_index = -1;
      RefreshAutocomplete(state, registry);
    } else {
      // Empty input + backspace → deactivate (like vim)
      state.Deactivate();
    }
    return true;
  }

  // Tab — accept autocomplete
  if (event == Event::Tab) {
    if (state.autocomplete_open) {
      if (HasSpace(state.input_text)) {
        // Argument mode: fill selected candidate.
        auto filtered = GetArgCandidates(state.input_text, registry);
        if (!filtered.empty() && state.autocomplete_selected < static_cast<int>(filtered.size())) {
          state.input_text = FirstWord(state.input_text) + " " + filtered[state.autocomplete_selected].first;
          state.cursor_pos = static_cast<int>(state.input_text.size());
          state.autocomplete_open = false;
        }
      } else {
        // Command mode: fill command name + space.
        auto query = FirstWord(state.input_text);
        auto matches = registry.Match(query);
        if (!matches.empty() && state.autocomplete_selected < static_cast<int>(matches.size())) {
          state.input_text = matches[state.autocomplete_selected]->name + " ";
          state.cursor_pos = static_cast<int>(state.input_text.size());
          state.autocomplete_open = false;
          RefreshAutocomplete(state, registry);
        }
      }
    }
    return true;
  }

  // Up arrow — history or autocomplete
  if (event == Event::ArrowUp) {
    if (state.autocomplete_open) {
      state.autocomplete_selected = std::max(0, state.autocomplete_selected - 1);
    } else {
      state.HistoryUp();
    }
    return true;
  }

  // Down arrow — history or autocomplete
  if (event == Event::ArrowDown) {
    if (state.autocomplete_open) {
      int count;
      if (HasSpace(state.input_text)) {
        count = static_cast<int>(GetArgCandidates(state.input_text, registry).size());
      } else {
        count = static_cast<int>(registry.Match(FirstWord(state.input_text)).size());
      }
      int max_idx = std::max(0, count - 1);
      state.autocomplete_selected = std::min(state.autocomplete_selected + 1, max_idx);
    } else {
      state.HistoryDown();
    }
    return true;
  }

  // Left/Right arrow — cursor movement
  if (event == Event::ArrowLeft) {
    state.cursor_pos = std::max(0, state.cursor_pos - 1);
    return true;
  }
  if (event == Event::ArrowRight) {
    state.cursor_pos = std::min(static_cast<int>(state.input_text.size()), state.cursor_pos + 1);
    return true;
  }

  // Home / End
  if (event == Event::Home) {
    state.cursor_pos = 0;
    return true;
  }
  if (event == Event::End) {
    state.cursor_pos = static_cast<int>(state.input_text.size());
    return true;
  }

  // Delete
  if (event == Event::Delete) {
    if (state.cursor_pos < static_cast<int>(state.input_text.size())) {
      state.input_text.erase(state.cursor_pos, 1);
      RefreshAutocomplete(state, registry);
    }
    return true;
  }

  // Printable character
  if (event.is_character()) {
    auto ch = event.character();
    state.input_text.insert(state.cursor_pos, ch);
    state.cursor_pos += static_cast<int>(ch.size());
    state.history_index = -1;
    RefreshAutocomplete(state, registry);
    return true;
  }

  // Consume all other events to prevent leaking
  return true;
}

}  // namespace ally::components
