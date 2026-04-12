#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ally::autocomplete {

struct DirTreeNode {
  std::string name;
  std::string relative_path;
  bool is_dir = false;
  std::vector<DirTreeNode> children;
};

struct AutocompleteState {
  bool is_open = false;
  std::string query;
  std::optional<int> trigger_position;
  int selected_index = 0;
  std::vector<std::string> current_path;
  std::optional<std::vector<DirTreeNode>> tree_cache;
  std::mutex mutex;
};

}  // namespace ally::autocomplete
