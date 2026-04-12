#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ally::models {

struct WorkflowStage {
  std::string id;
  std::string name;
  std::string description;
  std::string starting_prompt;
  std::optional<std::string> product;
};

struct WorkflowDefinition {
  std::string id;
  std::string name;
  std::string description;
  std::vector<WorkflowStage> stages;
  std::string created_at;
};

struct WorkflowStageCreateInput {
  std::string name;
  std::string description;
  std::string starting_prompt;
  std::optional<std::string> product;
};

struct CreateWorkflowInput {
  std::string name;
  std::string description;
  std::vector<WorkflowStageCreateInput> stages;
};

struct WorkflowStageUpdateInput {
  std::optional<std::string> existing_id;
  std::string name;
  std::string description;
  std::string starting_prompt;
  std::optional<std::string> product;
};

struct UpdateWorkflowInput {
  std::string id;
  std::string name;
  std::string description;
  std::vector<WorkflowStageUpdateInput> stages;
};

}  // namespace ally::models
