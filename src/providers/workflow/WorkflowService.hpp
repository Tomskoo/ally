#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "src/models/Result.hpp"
#include "src/models/Workflow.hpp"

namespace ally::providers {

class WorkflowService {
 public:
  explicit WorkflowService(std::filesystem::path project_root);

  [[nodiscard]] auto list_workflows() const -> std::vector<models::WorkflowDefinition>;
  [[nodiscard]] auto get_workflow(const std::string& workflow_id) const -> std::optional<models::WorkflowDefinition>;
  auto create_workflow(const models::CreateWorkflowInput& input) -> models::Result<models::WorkflowDefinition>;
  auto update_workflow(const models::UpdateWorkflowInput& input) -> models::Result<models::WorkflowDefinition>;
  auto delete_workflow(const std::string& workflow_id) -> models::Result<std::monostate>;

 private:
  std::filesystem::path project_root_;
  std::vector<models::WorkflowDefinition> workflows_;

  void load_workflows();
  auto write_workflow_yaml(const models::WorkflowDefinition& def) -> bool;
  [[nodiscard]] auto workflows_dir() const -> std::filesystem::path;
};

}  // namespace ally::providers
