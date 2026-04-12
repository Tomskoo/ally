#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ally::autocomplete {

struct ArtifactEntry {
  std::string stage;          // stage slug (e.g., "intent", "design")
  std::string product;        // human-readable name from the workflow definition
  std::string relative_path;  // @-referenceable path
};

struct ArtifactAutocompleteState {
  bool is_open = false;
  std::string query;
  std::optional<int> trigger_position;
  int selected_index = 0;
  std::optional<std::vector<ArtifactEntry>> artifacts_cache;
  std::mutex mutex;

  // Used to signal the watcher listener thread to stop
  std::atomic<bool> listener_stop{false};
};

}  // namespace ally::autocomplete
