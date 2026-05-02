#include "Storage.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "src/configuration/Configuration.hpp"
#include "yaml-cpp/yaml.h"

namespace fs = std::filesystem;

namespace ally::storage {

auto find_project_root() -> fs::path {
  auto current = fs::current_path();
  while (true) {
    if (fs::is_directory(current / ".ally")) {
      return current;
    }
    auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return fs::current_path();
}

auto is_initialized(const fs::path& project_root) -> bool { return fs::is_directory(project_root / ".ally"); }

auto list_task_ids(const fs::path& project_root) -> std::vector<std::string> {
  auto tasks_dir = project_root / ".ally" / "tasks";
  if (!fs::is_directory(tasks_dir)) {
    return {};
  }

  std::vector<std::string> ids;
  for (const auto& entry : fs::directory_iterator(tasks_dir)) {
    if (!entry.is_directory()) {
      continue;
    }
    if (fs::exists(entry.path() / "task.yaml")) {
      ids.push_back(entry.path().filename().string());
    }
  }
  return ids;
}

static auto read_threads(const fs::path& task_dir) -> std::vector<models::Thread> {
  auto threads_dir = task_dir / "threads";
  if (!fs::is_directory(threads_dir)) {
    return {};
  }

  std::vector<models::Thread> threads;
  for (const auto& entry : fs::directory_iterator(threads_dir)) {
    if (!entry.is_directory()) {
      continue;
    }
    auto thread_yaml_path = entry.path() / "thread.yaml";
    if (!fs::exists(thread_yaml_path)) {
      continue;
    }
    try {
      auto node = YAML::LoadFile(thread_yaml_path.string());
      models::Thread thread;
      thread.id = entry.path().filename().string();
      thread.name = node["name"].as<std::string>("");
      thread.current_stage = node["current_stage"].as<std::string>("");
      thread.workflow_id = node["workflow_id"].as<std::string>("");
      thread.created_at = node["created_at"].as<std::string>("");
      thread.last_activity = node["last_activity"].as<std::string>("");
      thread.archived = node["archived"].as<bool>(false);
      auto status_str = node["status"].as<std::string>("running");
      if (status_str == "idle") {
        thread.status = models::ThreadStatus::Idle;
      } else if (status_str == "completed") {
        thread.status = models::ThreadStatus::Completed;
      } else {
        thread.status = models::ThreadStatus::Running;
      }
      if (node["stage_sessions"] && node["stage_sessions"].IsMap()) {
        for (auto it = node["stage_sessions"].begin(); it != node["stage_sessions"].end(); ++it) {
          auto key = it->first.as<std::string>();
          if (it->second.IsSequence()) {
            for (const auto& item : it->second) {
              thread.stage_sessions[key].push_back(item.as<std::string>());
            }
          } else {
            // Old format: single string → wrap in vector.
            thread.stage_sessions[key].push_back(it->second.as<std::string>());
          }
        }
      }
      threads.push_back(std::move(thread));
    } catch (...) {
      // Skip threads that fail to parse
    }
  }
  return threads;
}

static auto compute_stage(const std::vector<models::Thread>& threads) -> std::string {
  std::string best_stage;
  unsigned long long best_activity = 0;

  for (const auto& thread : threads) {
    if (thread.archived) {
      continue;
    }
    try {
      auto activity = std::stoull(thread.last_activity);
      if (activity > best_activity) {
        best_activity = activity;
        best_stage = thread.current_stage;
      }
    } catch (...) {
      // Skip threads with unparseable timestamps
    }
  }
  return best_stage;
}

auto read_task(const fs::path& project_root, const std::string& task_id) -> std::optional<models::Task> {
  auto task_dir = project_root / ".ally" / "tasks" / task_id;
  auto task_yaml_path = task_dir / "task.yaml";

  if (!fs::exists(task_yaml_path)) {
    return std::nullopt;
  }

  try {
    auto node = YAML::LoadFile(task_yaml_path.string());
    models::Task task;
    task.id = task_id;
    task.name = node["name"].as<std::string>("");
    task.created_at = node["created_at"].as<std::string>("");
    task.last_activity = node["last_activity"].as<std::string>("");
    task.archived = node["archived"].as<bool>(false);

    if (node["description"] && !node["description"].IsNull()) {
      task.description = node["description"].as<std::string>();
    }

    task.threads = read_threads(task_dir);
    task.stage = compute_stage(task.threads);

    return task;
  } catch (...) {
    return std::nullopt;
  }
}

auto init_workspace(const fs::path& project_root) -> void {
  auto tasks_dir = project_root / ".ally" / "tasks";
  fs::create_directories(tasks_dir);
  auto workflows_dir = project_root / ".ally" / "workflows";
  fs::create_directories(workflows_dir);
}

auto slugify(const std::string& name) -> std::string {
  std::string result;
  result.reserve(name.size());
  bool prev_hyphen = true;  // suppress leading hyphen
  for (char chr : name) {
    if (std::isalnum(static_cast<unsigned char>(chr)) != 0) {
      result += static_cast<char>(std::tolower(static_cast<unsigned char>(chr)));
      prev_hyphen = false;
    } else if (!prev_hyphen) {
      result += '-';
      prev_hyphen = true;
    }
  }
  // trim trailing hyphen
  if (!result.empty() && result.back() == '-') {
    result.pop_back();
  }
  return result;
}

auto write_task(const fs::path& project_root, const models::Task& task) -> bool {
  try {
    auto task_dir = project_root / ".ally" / "tasks" / task.id;
    fs::create_directories(task_dir);

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << task.name;
    out << YAML::Key << "created_at" << YAML::Value << task.created_at;
    out << YAML::Key << "last_activity" << YAML::Value << task.last_activity;
    out << YAML::Key << "archived" << YAML::Value << task.archived;
    if (task.description.has_value()) {
      out << YAML::Key << "description" << YAML::Value << *task.description;
    }
    out << YAML::EndMap;

    auto task_yaml_path = task_dir / "task.yaml";
    std::ofstream file(task_yaml_path);
    if (!file.is_open()) {
      return false;
    }
    file << out.c_str() << "\n";
    return file.good();
  } catch (...) {
    return false;
  }
}

auto write_thread(const fs::path& project_root, const std::string& task_id, const models::Thread& thread) -> bool {
  try {
    auto thread_dir = project_root / ".ally" / "tasks" / task_id / "threads" / thread.id;
    fs::create_directories(thread_dir / "stages");

    std::string status_str;
    switch (thread.status) {
      case models::ThreadStatus::Idle:
        status_str = "idle";
        break;
      case models::ThreadStatus::Completed:
        status_str = "completed";
        break;
      default:
        status_str = "running";
        break;
    }

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << thread.name;
    out << YAML::Key << "current_stage" << YAML::Value << thread.current_stage;
    out << YAML::Key << "workflow_id" << YAML::Value << thread.workflow_id;
    out << YAML::Key << "created_at" << YAML::Value << thread.created_at;
    out << YAML::Key << "last_activity" << YAML::Value << thread.last_activity;
    out << YAML::Key << "status" << YAML::Value << status_str;
    out << YAML::Key << "archived" << YAML::Value << thread.archived;
    if (!thread.stage_sessions.empty()) {
      out << YAML::Key << "stage_sessions" << YAML::Value << YAML::BeginMap;
      for (const auto& [stage, sids] : thread.stage_sessions) {
        out << YAML::Key << stage << YAML::Value << YAML::BeginSeq;
        for (const auto& sid : sids) {
          out << sid;
        }
        out << YAML::EndSeq;
      }
      out << YAML::EndMap;
    }
    out << YAML::EndMap;

    auto thread_yaml_path = thread_dir / "thread.yaml";
    std::ofstream file(thread_yaml_path);
    if (!file.is_open()) {
      return false;
    }
    file << out.c_str() << "\n";
    return file.good();
  } catch (...) {
    return false;
  }
}

auto archive_thread(const fs::path& project_root, const std::string& task_id, const std::string& thread_id) -> bool {
  auto thread_yaml_path = project_root / ".ally" / "tasks" / task_id / "threads" / thread_id / "thread.yaml";
  if (!fs::exists(thread_yaml_path)) {
    return false;
  }
  try {
    auto node = YAML::LoadFile(thread_yaml_path.string());
    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    node["archived"] = true;
    node["last_activity"] = std::to_string(now);

    YAML::Emitter out;
    out << node;

    std::ofstream file(thread_yaml_path);
    if (!file.is_open()) {
      return false;
    }
    file << out.c_str() << "\n";
    return file.good();
  } catch (...) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Filesystem helpers (for file autocomplete)
// ---------------------------------------------------------------------------

namespace {

auto dir_filter(const std::string& name, bool is_dir) -> bool {
  if (is_dir) {
    if (name == ".git" || name == ".cache" || name == "external" || name == "node_modules" || name == "target") {
      return false;
    }
    if (name.size() >= 6 && name.compare(0, 6, "bazel-") == 0) {
      return false;
    }
    return true;
  }
  return name != ".DS_Store";
}

auto lowercase_name(const std::string& name) -> std::string {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char chr) -> int { return std::tolower(chr); });
  return lower;
}

void sort_tree_nodes(std::vector<ally::autocomplete::DirTreeNode>& nodes) {
  std::sort(nodes.begin(), nodes.end(), [](const ally::autocomplete::DirTreeNode& lhs, const ally::autocomplete::DirTreeNode& rhs) -> bool {
    if (lhs.is_dir != rhs.is_dir) {
      return lhs.is_dir > rhs.is_dir;
    }
    return lowercase_name(lhs.name) < lowercase_name(rhs.name);
  });
}

auto build_node(const fs::path& abs_path, const std::string& relative_path, int depth, int max_depth)
    -> ally::autocomplete::DirTreeNode {
  ally::autocomplete::DirTreeNode node;
  node.name = abs_path.filename().string();
  node.relative_path = relative_path;
  node.is_dir = fs::is_directory(abs_path);

  if (node.is_dir && depth < max_depth) {
    for (const auto& entry : fs::directory_iterator(abs_path)) {
      auto child_name = entry.path().filename().string();
      bool child_is_dir = entry.is_directory();
      if (!dir_filter(child_name, child_is_dir)) {
        continue;
      }
      auto child_rel = relative_path.empty() ? child_name : relative_path + "/" + child_name;
      node.children.push_back(build_node(entry.path(), child_rel, depth + 1, max_depth));
    }
    sort_tree_nodes(node.children);
  }

  return node;
}

}  // namespace

void list_directory_tree(const fs::path& project_root, int max_depth,
                         const std::function<void(std::vector<ally::autocomplete::DirTreeNode>)>& on_done) {
  std::vector<ally::autocomplete::DirTreeNode> roots;

  for (const auto& entry : fs::directory_iterator(project_root)) {
    auto name = entry.path().filename().string();
    bool is_dir = entry.is_directory();
    if (!dir_filter(name, is_dir)) {
      continue;
    }
    roots.push_back(build_node(entry.path(), name, 1, max_depth));
  }

  sort_tree_nodes(roots);
  on_done(std::move(roots));
}

void list_directory_entries(const fs::path& project_root, const std::string& relative_path,
                            const std::function<void(std::vector<DirEntry>)>& on_done) {
  if (relative_path.find("..") != std::string::npos) {
    on_done({});
    return;
  }

  fs::path dir_path = project_root / relative_path;

  if (!fs::is_directory(dir_path)) {
    on_done({});
    return;
  }

  std::vector<DirEntry> entries;
  for (const auto& entry : fs::directory_iterator(dir_path)) {
    auto name = entry.path().filename().string();
    bool is_dir = entry.is_directory();
    if (!dir_filter(name, is_dir)) {
      continue;
    }
    auto child_rel = relative_path.empty() ? name : relative_path + "/" + name;
    entries.push_back(DirEntry{name, child_rel, is_dir});
  }

  std::sort(entries.begin(), entries.end(), [](const DirEntry& lhs, const DirEntry& rhs) -> bool {
    if (lhs.is_dir != rhs.is_dir) {
      return lhs.is_dir > rhs.is_dir;
    }
    auto lower = [](const std::string& str) -> std::string {
      std::string out = str;
      std::transform(out.begin(), out.end(), out.begin(), [](unsigned char chr) -> int { return std::tolower(chr); });
      return out;
    };
    return lower(lhs.name) < lower(rhs.name);
  });

  on_done(std::move(entries));
}

// ---------------------------------------------------------------------------
// Artifact storage
// ---------------------------------------------------------------------------

auto list_stage_artifact_slugs(const fs::path& project_root, const std::string& task_id, const std::string& thread_id)
    -> std::vector<std::string> {
  auto stages_dir = project_root / ".ally" / "tasks" / task_id / "threads" / thread_id / "stages";
  if (!fs::is_directory(stages_dir)) {
    return {};
  }

  std::vector<std::string> slugs;
  for (const auto& entry : fs::directory_iterator(stages_dir)) {
    if (!entry.is_directory()) {
      continue;
    }
    if (fs::exists(entry.path() / "artifact.md")) {
      slugs.push_back(entry.path().filename().string());
    }
  }
  std::sort(slugs.begin(), slugs.end());
  return slugs;
}

auto read_artifact_content(const fs::path& project_root, const std::string& task_id, const std::string& thread_id,
                           const std::string& stage) -> std::optional<std::string> {
  auto artifact_path = project_root / ".ally" / "tasks" / task_id / "threads" / thread_id / "stages" / stage / "artifact.md";
  if (!fs::exists(artifact_path)) {
    return std::nullopt;
  }
  std::ifstream file(artifact_path);
  if (!file.is_open()) {
    return std::nullopt;
  }
  std::ostringstream oss;
  oss << file.rdbuf();
  return oss.str();
}

// ---------------------------------------------------------------------------
// Artifact completions
// ---------------------------------------------------------------------------

auto ListArtifactCompletions(const fs::path& project_root, const std::string& task_id, const std::string& thread_id,
                             const std::vector<models::WorkflowStage>& stages) -> std::vector<ally::autocomplete::ArtifactEntry> {
  auto stages_dir = project_root / ".ally" / "tasks" / task_id / "threads" / thread_id / "stages";

  if (!fs::is_directory(stages_dir)) {
    return {};
  }

  // Build stage-id -> human-readable name map from workflow stages.
  std::unordered_map<std::string, std::string> stage_name;
  for (const auto& stg : stages) {
    stage_name[stg.id] = stg.name;
  }

  std::vector<ally::autocomplete::ArtifactEntry> result;
  for (const auto& entry : fs::directory_iterator(stages_dir)) {
    if (!entry.is_directory()) {
      continue;
    }
    auto stage_slug = entry.path().filename().string();
    auto artifact_path = entry.path() / "artifact.md";
    if (!fs::exists(artifact_path)) {
      continue;
    }

    ally::autocomplete::ArtifactEntry artifact;
    artifact.stage = stage_slug;
    artifact.product = stage_name.count(stage_slug) != 0 ? stage_name[stage_slug] : stage_slug;
    artifact.relative_path = ".ally/tasks/" + task_id + "/threads/" + thread_id + "/stages/" + stage_slug + "/artifact.md";
    result.push_back(std::move(artifact));
  }

  std::sort(result.begin(), result.end(),
            [](const ally::autocomplete::ArtifactEntry& lhs, const ally::autocomplete::ArtifactEntry& rhs) -> bool {
              return lhs.stage < rhs.stage;
            });

  result.insert(result.begin(), ally::autocomplete::ArtifactEntry{"task", "Task Directory", ".ally/tasks/" + task_id + "/"});
  result.insert(result.begin(),
                ally::autocomplete::ArtifactEntry{"thread", "Thread Directory",
                                                  ".ally/tasks/" + task_id + "/threads/" + thread_id + "/"});

  return result;
}

// ---------------------------------------------------------------------------
// Session persistence
// ---------------------------------------------------------------------------

auto GetStageSessionId(const fs::path& project_root, const std::string& task_id, const std::string& thread_id,
                       const std::string& stage) -> std::optional<std::string> {
  auto thread_yaml = project_root / ".ally" / "tasks" / task_id / "threads" / thread_id / "thread.yaml";
  if (!fs::exists(thread_yaml)) {
    return std::nullopt;
  }
  try {
    auto node = YAML::LoadFile(thread_yaml.string());
    if (node["stage_sessions"] && node["stage_sessions"].IsMap() && node["stage_sessions"][stage]) {
      const auto& val = node["stage_sessions"][stage];
      if (val.IsSequence()) {
        if (val.size() == 0) { return std::nullopt; }
        return val[val.size() - 1].as<std::string>();  // active = last
      }
      return val.as<std::string>();  // old scalar format
    }
  } catch (const std::exception& /*unused*/) {
    // YAML parse failure — treat as missing session
  }
  return std::nullopt;
}

auto SaveStageSessionId(const fs::path& project_root, const std::string& task_id, const std::string& thread_id,
                        const std::string& stage, const std::string& session_id) -> bool {
  return AppendStageSessionId(project_root, task_id, thread_id, stage, session_id);
}

auto GetStageSessionIds(const fs::path& project_root, const std::string& task_id, const std::string& thread_id,
                        const std::string& stage) -> std::vector<std::string> {
  auto thread_yaml = project_root / ".ally" / "tasks" / task_id / "threads" / thread_id / "thread.yaml";
  if (!fs::exists(thread_yaml)) {
    return {};
  }
  try {
    auto node = YAML::LoadFile(thread_yaml.string());
    if (node["stage_sessions"] && node["stage_sessions"].IsMap() && node["stage_sessions"][stage]) {
      const auto& val = node["stage_sessions"][stage];
      std::vector<std::string> result;
      if (val.IsSequence()) {
        for (const auto& item : val) {
          result.push_back(item.as<std::string>());
        }
      } else {
        // Old scalar format.
        result.push_back(val.as<std::string>());
      }
      return result;
    }
  } catch (...) {
  }
  return {};
}

auto AppendStageSessionId(const fs::path& project_root, const std::string& task_id, const std::string& thread_id,
                          const std::string& stage, const std::string& session_id) -> bool {
  auto thread_yaml = project_root / ".ally" / "tasks" / task_id / "threads" / thread_id / "thread.yaml";
  if (!fs::exists(thread_yaml)) {
    return false;
  }
  try {
    auto node = YAML::LoadFile(thread_yaml.string());

    // Migrate old scalar to sequence if needed, then append.
    YAML::Node sessions;
    if (node["stage_sessions"] && node["stage_sessions"][stage]) {
      const auto& existing = node["stage_sessions"][stage];
      if (existing.IsSequence()) {
        sessions = existing;
      } else {
        // Old scalar → seed sequence with the existing value.
        sessions.push_back(existing.as<std::string>());
      }
    }
    sessions.push_back(session_id);
    node["stage_sessions"][stage] = sessions;

    YAML::Emitter out;
    out << node;

    std::ofstream file(thread_yaml);
    if (!file.is_open()) {
      return false;
    }
    file << out.c_str() << "\n";
    return file.good();
  } catch (...) {
    return false;
  }
}

auto SetActiveStageSession(const fs::path& project_root, const std::string& task_id, const std::string& thread_id,
                           const std::string& stage, const std::string& session_id) -> bool {
  auto thread_yaml = project_root / ".ally" / "tasks" / task_id / "threads" / thread_id / "thread.yaml";
  if (!fs::exists(thread_yaml)) {
    return false;
  }
  try {
    auto node = YAML::LoadFile(thread_yaml.string());
    if (!node["stage_sessions"] || !node["stage_sessions"][stage]) {
      return false;
    }

    const auto& existing = node["stage_sessions"][stage];
    std::vector<std::string> ids;
    if (existing.IsSequence()) {
      for (const auto& item : existing) {
        ids.push_back(item.as<std::string>());
      }
    } else {
      ids.push_back(existing.as<std::string>());
    }

    // Move session_id to the end (making it active).
    auto it = std::find(ids.begin(), ids.end(), session_id);
    if (it == ids.end()) {
      return false;
    }
    std::rotate(it, std::next(it), ids.end());

    YAML::Node sessions;
    for (const auto& sid : ids) {
      sessions.push_back(sid);
    }
    node["stage_sessions"][stage] = sessions;

    YAML::Emitter out;
    out << node;

    std::ofstream file(thread_yaml);
    if (!file.is_open()) {
      return false;
    }
    file << out.c_str() << "\n";
    return file.good();
  } catch (...) {
    return false;
  }
}

auto GetModelForProvider(const fs::path& project_root, const std::string& provider_id) -> std::optional<std::string> {
  auto config = configuration::LoadOpenCodeConfig(project_root);
  auto it = config.model_per_provider.find(provider_id);
  if (it != config.model_per_provider.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto SetModelForProvider(const fs::path& project_root, const std::string& provider_id, const std::string& model_id) -> void {
  configuration::SaveModelForProvider(project_root, provider_id, model_id);
}

}  // namespace ally::storage
