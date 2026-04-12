#include "src/services/SkillService.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ally::services {

SkillService::SkillService(std::string project_root) : project_root_(std::move(project_root)) {}

namespace {

// Extract text between the first pair of "---" fences.
auto ExtractFrontmatter(const std::string& content) -> std::string {
  // Trim leading whitespace.
  auto start = content.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) { return {};
}

  // Must begin with "---".
  if (content.compare(start, 3, "---") != 0) { return {};
}

  // Find the closing "---" after the opening fence.
  auto body_start = content.find('\n', start);
  if (body_start == std::string::npos) { return {};
}
  ++body_start;

  auto end = content.find("\n---", body_start);
  if (end == std::string::npos) { return {};
}

  return content.substr(body_start, end - body_start);
}

auto LowercaseName(const std::string& str) -> std::string {
  std::string lower = str;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char chr) -> int { return std::tolower(chr); });
  return lower;
}

}  // namespace

void SkillService::ListSkills(const std::function<void(std::vector<ally::autocomplete::SkillEntry>)>& on_done) {
  namespace fs = std::filesystem;

  std::vector<ally::autocomplete::SkillEntry> entries;

  auto skills_dir = project_root_ / ".opencode" / "skills";
  std::error_code err;
  if (!fs::is_directory(skills_dir, err)) {
    on_done({});
    return;
  }

  for (const auto& dir_entry : fs::directory_iterator(skills_dir, err)) {
    if (!dir_entry.is_directory()) { continue;
}

    auto skill_md = dir_entry.path() / "SKILL.md";
    if (!fs::is_regular_file(skill_md, err)) { continue;
}

    // Read the file.
    std::ifstream file(skill_md);
    if (!file.is_open()) { continue;
}
    std::ostringstream oss;
    oss << file.rdbuf();
    auto content = oss.str();

    auto yaml_str = ExtractFrontmatter(content);
    if (yaml_str.empty()) { continue;
}

    std::string dir_name = dir_entry.path().filename().string();
    std::string name = dir_name;
    std::string description;

    try {
      auto node = YAML::Load(yaml_str);
      if (node["name"] && node["name"].IsScalar()) {
        auto parsed_name = node["name"].as<std::string>();
        if (!parsed_name.empty()) { name = parsed_name;
}
      }
      if (node["description"] && node["description"].IsScalar()) {
        description = node["description"].as<std::string>();
      }
    } catch (const YAML::Exception&) {
      // Malformed YAML — use dir_name fallback, skip description.
    }

    entries.push_back({std::move(name), std::move(dir_name), std::move(description)});
  }

  // Sort ascending by dir_name (case-insensitive).
  std::sort(entries.begin(), entries.end(),
            [](const auto& lhs, const auto& rhs) -> auto { return LowercaseName(lhs.dir_name) < LowercaseName(rhs.dir_name); });

  on_done(std::move(entries));
}

}  // namespace ally::services
