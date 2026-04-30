#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "src/components/autocomplete/ArtifactTypes.hpp"
#include "src/components/autocomplete/Types.hpp"
#include "src/models/Task.hpp"
#include "src/models/Workflow.hpp"

namespace ally::commands::storage {

struct DirEntry {
  std::string name;
  std::string relative_path;
  bool is_dir = false;
};

/// Recursively walk the project root up to max_depth levels.
/// Applies default directory filter. Synchronous — caller threads it.
void list_directory_tree(const std::filesystem::path& project_root, int max_depth,
                         const std::function<void(std::vector<ally::autocomplete::DirTreeNode>)>& on_done);

/// Return the immediate children of the directory at relative_path.
/// Applies default directory filter. Synchronous.
void list_directory_entries(const std::filesystem::path& project_root, const std::string& relative_path,
                            const std::function<void(std::vector<DirEntry>)>& on_done);

auto find_project_root() -> std::filesystem::path;

auto is_initialized(const std::filesystem::path& project_root) -> bool;

auto list_task_ids(const std::filesystem::path& project_root) -> std::vector<std::string>;

auto read_task(const std::filesystem::path& project_root, const std::string& task_id) -> std::optional<models::Task>;

auto init_workspace(const std::filesystem::path& project_root) -> void;

auto slugify(const std::string& name) -> std::string;

auto write_task(const std::filesystem::path& project_root, const models::Task& task) -> bool;

auto write_thread(const std::filesystem::path& project_root, const std::string& task_id, const models::Thread& thread) -> bool;

auto archive_thread(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id) -> bool;

/// Return stage slugs that have an artifact.md file, sorted alphabetically.
auto list_stage_artifact_slugs(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id)
    -> std::vector<std::string>;

/// Read the content of a stage's artifact.md. Returns nullopt if file missing.
auto read_artifact_content(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id,
                           const std::string& stage) -> std::optional<std::string>;

/// Scan stages directories for existing artifact.md files.
/// Returns an ArtifactEntry per stage that has an artifact, sorted by stage slug.
auto ListArtifactCompletions(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id,
                             const std::vector<models::WorkflowStage>& stages) -> std::vector<ally::autocomplete::ArtifactEntry>;

/// Look up a persisted OpenCode session ID for the given (task, thread, stage).
auto GetStageSessionId(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id,
                       const std::string& stage) -> std::optional<std::string>;

/// Persist an OpenCode session ID for the given (task, thread, stage).
auto SaveStageSessionId(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id,
                        const std::string& stage, const std::string& session_id) -> bool;

/// Return all persisted session IDs for a stage (ordered, active session last).
auto GetStageSessionIds(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id,
                        const std::string& stage) -> std::vector<std::string>;

/// Append a new session ID to the stage's session list (becomes the active session).
auto AppendStageSessionId(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id,
                          const std::string& stage, const std::string& session_id) -> bool;

/// Move a session ID to the end of the stage's session list (making it active).
auto SetActiveStageSession(const std::filesystem::path& project_root, const std::string& task_id, const std::string& thread_id,
                           const std::string& stage, const std::string& session_id) -> bool;

/// Look up the persisted model ID for a provider.
auto GetModelForProvider(const std::filesystem::path& project_root, const std::string& provider_id) -> std::optional<std::string>;

/// Persist a model selection for a provider.
auto SetModelForProvider(const std::filesystem::path& project_root, const std::string& provider_id,
                         const std::string& model_id) -> void;

}  // namespace ally::commands::storage
