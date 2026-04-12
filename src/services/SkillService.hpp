#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "src/components/autocomplete/SkillTypes.hpp"

namespace ally::services {

class SkillService {
 public:
  explicit SkillService(std::string project_root);

  /// Synchronously scans <project_root>/.opencode/skills/ for subdirectories
  /// containing SKILL.md with YAML frontmatter. Returns results sorted
  /// ascending by dir_name. Caller is responsible for threading.
  void ListSkills(const std::function<void(std::vector<ally::autocomplete::SkillEntry>)>& on_done);

 private:
  std::filesystem::path project_root_;
};

}  // namespace ally::services
