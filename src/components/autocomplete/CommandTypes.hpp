#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ally::autocomplete {

struct CommandEntry {
  std::string name;         // filename stem (e.g., "research") — used for display and insertion
  std::string description;  // from YAML frontmatter (may be empty)
};

struct CommandAutocompleteState {
  bool is_open = false;
  std::string query;
  std::optional<int> trigger_position;
  int selected_index = 0;
  std::optional<std::vector<CommandEntry>> commands_cache;
  std::mutex mutex;

  // Used to signal the watcher listener thread to stop
  std::atomic<bool> listener_stop{false};
};

}  // namespace ally::autocomplete
