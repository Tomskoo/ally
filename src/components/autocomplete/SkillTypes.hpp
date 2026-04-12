#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ally::autocomplete {

struct SkillEntry {
  std::string name;         // human-readable display name (from YAML frontmatter)
  std::string dir_name;     // directory name under .opencode/skills/
  std::string description;  // optional one-line description (may be empty)
};

struct SkillAutocompleteState {
  bool is_open = false;
  std::string query;
  std::optional<int> trigger_position;
  int selected_index = 0;
  std::optional<std::vector<SkillEntry>> skills_cache;
  std::mutex mutex;

  // Used to signal the watcher listener thread to stop
  std::atomic<bool> listener_stop{false};
};

}  // namespace ally::autocomplete
