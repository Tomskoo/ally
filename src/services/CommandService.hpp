#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "src/components/autocomplete/CommandTypes.hpp"

namespace ally::services {

class CommandService {
 public:
  explicit CommandService(std::string project_root);

  /// Synchronously scans <project_root>/.opencode/commands/ for *.md files
  /// with optional YAML frontmatter. Returns results sorted ascending by name.
  /// Caller is responsible for threading.
  void ListCommands(const std::function<void(std::vector<ally::autocomplete::CommandEntry>)>& on_done);

 private:
  std::filesystem::path project_root_;
};

}  // namespace ally::services
