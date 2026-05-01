#pragma once

#include <string>
#include <vector>

namespace ally::components {

struct CommandBarState {
  bool is_active = false;
  std::string input_text;
  int cursor_pos = 0;

  // History (session-scoped, newest last)
  std::vector<std::string> history;
  int history_index = -1;   // -1 = editing new entry, 0+ = browsing
  std::string saved_input;  // text saved before browsing history

  // Autocomplete
  bool autocomplete_open = false;
  int autocomplete_selected = 0;

  void Activate() {
    is_active = true;
    input_text.clear();
    cursor_pos = 0;
    history_index = -1;
    saved_input.clear();
    autocomplete_open = false;
    autocomplete_selected = 0;
  }

  void Deactivate() {
    is_active = false;
    input_text.clear();
    cursor_pos = 0;
    history_index = -1;
    saved_input.clear();
    autocomplete_open = false;
    autocomplete_selected = 0;
  }

  void PushHistory(const std::string& cmd) {
    if (cmd.empty()) {
      return;
    }
    if (!history.empty() && history.back() == cmd) {
      return;
    }
    history.push_back(cmd);
  }

  void HistoryUp() {
    if (history.empty()) {
      return;
    }
    if (history_index == -1) {
      saved_input = input_text;
    }
    int max_index = static_cast<int>(history.size()) - 1;
    history_index = std::min(history_index + 1, max_index);
    input_text = history[history.size() - 1 - history_index];
    cursor_pos = static_cast<int>(input_text.size());
  }

  void HistoryDown() {
    if (history_index < 0) {
      return;
    }
    history_index--;
    if (history_index < 0) {
      input_text = saved_input;
    } else {
      input_text = history[history.size() - 1 - history_index];
    }
    cursor_pos = static_cast<int>(input_text.size());
  }
};

}  // namespace ally::components
