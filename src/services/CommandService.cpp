#include "src/services/CommandService.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ally::services {

CommandService::CommandService(std::string project_root) : project_root_(std::move(project_root)) {}

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

void CommandService::ListCommands(const std::function<void(std::vector<ally::autocomplete::CommandEntry>)>& on_done) {
  namespace fs = std::filesystem;

  std::vector<ally::autocomplete::CommandEntry> entries;

  auto commands_dir = project_root_ / ".opencode" / "commands";
  std::error_code err;
  if (!fs::is_directory(commands_dir, err)) {
    on_done({});
    return;
  }

  for (const auto& dir_entry : fs::directory_iterator(commands_dir, err)) {
    if (!dir_entry.is_regular_file()) { continue;
}

    const auto& path = dir_entry.path();
    if (path.extension() != ".md") { continue;
}

    // Read the file.
    std::ifstream file(path);
    if (!file.is_open()) { continue;
}
    std::ostringstream oss;
    oss << file.rdbuf();
    auto content = oss.str();

    std::string name = path.stem().string();
    std::string description;

    auto yaml_str = ExtractFrontmatter(content);
    if (!yaml_str.empty()) {
      try {
        auto node = YAML::Load(yaml_str);
        if (node["description"] && node["description"].IsScalar()) {
          description = node["description"].as<std::string>();
        }
      } catch (const YAML::Exception&) {
        // Malformed YAML — skip description.
      }
    }

    entries.push_back({std::move(name), std::move(description)});
  }

  // Sort ascending by name (case-insensitive).
  std::sort(entries.begin(), entries.end(),
            [](const auto& lhs, const auto& rhs) -> auto { return LowercaseName(lhs.name) < LowercaseName(rhs.name); });

  on_done(std::move(entries));
}

}  // namespace ally::services
