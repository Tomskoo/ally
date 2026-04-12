#include "ArtifactService.hpp"

#include <utility>

#include "src/commands/storage/Storage.hpp"

namespace ally::providers {

ArtifactService::ArtifactService(std::filesystem::path project_root) : project_root_(std::move(project_root)) {}

auto ArtifactService::list_stage_artifacts(const std::string& task_id, const std::string& thread_id) const
    -> std::vector<std::string> {
  return commands::storage::list_stage_artifact_slugs(project_root_, task_id, thread_id);
}

auto ArtifactService::get_artifact(const std::string& task_id, const std::string& thread_id, const std::string& stage) const
    -> std::optional<std::string> {
  return commands::storage::read_artifact_content(project_root_, task_id, thread_id, stage);
}

}  // namespace ally::providers
