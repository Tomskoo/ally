#include "WorkflowService.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "src/commands/storage/Storage.hpp"
#include "yaml-cpp/yaml.h"

namespace fs = std::filesystem;

namespace ally::providers {

namespace {

auto now_timestamp() -> std::string {
  auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  return std::to_string(now);
}

auto generate_stage_ids(const std::vector<models::WorkflowStageCreateInput>& inputs) -> std::vector<std::string> {
  std::vector<std::string> ids;
  ids.reserve(inputs.size());
  std::unordered_map<std::string, int> seen;

  for (const auto& input : inputs) {
    auto slug = commands::storage::slugify(input.name);
    if (seen.count(slug) > 0) {
      ++seen[slug];
      slug += "-" + std::to_string(seen[slug]);
    } else {
      seen[slug] = 0;
    }
    ids.push_back(slug);
  }
  return ids;
}

auto resolve_stage_ids(const std::vector<models::WorkflowStage>& existing,
                       const std::vector<models::WorkflowStageUpdateInput>& inputs) -> std::vector<std::string> {
  std::unordered_set<std::string> existing_ids;
  for (const auto& stage : existing) {
    existing_ids.insert(stage.id);
  }

  std::unordered_set<std::string> preserved;
  for (const auto& input : inputs) {
    if (input.existing_id.has_value() && existing_ids.count(*input.existing_id) > 0) {
      preserved.insert(*input.existing_id);
    }
  }

  std::vector<std::string> ids;
  ids.reserve(inputs.size());
  std::unordered_map<std::string, int> seen;

  for (const auto& claimed : preserved) {
    seen[claimed] = 0;
  }

  for (const auto& input : inputs) {
    if (input.existing_id.has_value() && preserved.count(*input.existing_id) > 0) {
      ids.push_back(*input.existing_id);
    } else {
      auto slug = commands::storage::slugify(input.name);
      if (seen.count(slug) > 0) {
        ++seen[slug];
        slug += "-" + std::to_string(seen[slug]);
      }
      seen[slug] = 0;
      ids.push_back(slug);
    }
  }
  return ids;
}

auto apply_product_defaults(std::vector<models::WorkflowStage>& stages) -> void {
  if (stages.empty()) {
    return;
  }
  stages.back().product = std::nullopt;
  for (size_t idx = 0; idx + 1 < stages.size(); ++idx) {
    if (!stages[idx].product.has_value() || stages[idx].product->empty()) {
      stages[idx].product = stages[idx].id + "-artifact.md";
    }
  }
}

}  // namespace

WorkflowService::WorkflowService(std::filesystem::path project_root) : project_root_(std::move(project_root)) { load_workflows(); }

auto WorkflowService::workflows_dir() const -> fs::path { return project_root_ / ".ally" / "workflows"; }

void WorkflowService::load_workflows() {
  workflows_.clear();
  auto dir = workflows_dir();
  if (!fs::is_directory(dir)) {
    return;
  }

  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_directory()) {
      continue;
    }
    auto yaml_path = entry.path() / "workflow.yaml";
    if (!fs::exists(yaml_path)) {
      continue;
    }
    try {
      auto node = YAML::LoadFile(yaml_path.string());
      models::WorkflowDefinition def;
      def.id = entry.path().filename().string();
      def.name = node["name"].as<std::string>("");
      def.description = node["description"].as<std::string>("");
      def.created_at = node["created_at"].as<std::string>("");

      if (node["stages"] && node["stages"].IsSequence()) {
        for (const auto& stage_node : node["stages"]) {
          models::WorkflowStage stage;
          stage.id = stage_node["id"].as<std::string>("");
          stage.name = stage_node["name"].as<std::string>("");
          stage.description = stage_node["description"].as<std::string>("");
          stage.starting_prompt = stage_node["starting_prompt"].as<std::string>("");
          if (stage_node["product"] && !stage_node["product"].IsNull()) {
            stage.product = stage_node["product"].as<std::string>();
          }
          def.stages.push_back(std::move(stage));
        }
      }
      workflows_.push_back(std::move(def));
    } catch (const std::exception& /*unused*/) {
      // Skip workflows that fail to parse
    }
  }

  std::sort(workflows_.begin(), workflows_.end(),
            [](const models::WorkflowDefinition& lhs, const models::WorkflowDefinition& rhs) -> bool {
              try {
                return std::stoull(lhs.created_at) > std::stoull(rhs.created_at);
              } catch (const std::exception& /*unused*/) {
                return lhs.created_at > rhs.created_at;
              }
            });
}

auto WorkflowService::write_workflow_yaml(const models::WorkflowDefinition& def) -> bool {
  try {
    auto dir = workflows_dir() / def.id;
    fs::create_directories(dir);

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << def.name;
    out << YAML::Key << "description" << YAML::Value << def.description;
    out << YAML::Key << "created_at" << YAML::Value << def.created_at;
    out << YAML::Key << "stages" << YAML::Value << YAML::BeginSeq;
    for (const auto& stage : def.stages) {
      out << YAML::BeginMap;
      out << YAML::Key << "id" << YAML::Value << stage.id;
      out << YAML::Key << "name" << YAML::Value << stage.name;
      out << YAML::Key << "description" << YAML::Value << stage.description;
      out << YAML::Key << "starting_prompt" << YAML::Value << stage.starting_prompt;
      if (stage.product.has_value()) {
        out << YAML::Key << "product" << YAML::Value << *stage.product;
      }
      out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;

    auto yaml_path = dir / "workflow.yaml";
    std::ofstream file(yaml_path);
    if (!file.is_open()) {
      return false;
    }
    file << out.c_str() << "\n";
    return file.good();
  } catch (const std::exception& /*unused*/) {
    return false;
  }
}

auto WorkflowService::list_workflows() const -> std::vector<models::WorkflowDefinition> { return workflows_; }

auto WorkflowService::get_workflow(const std::string& workflow_id) const -> std::optional<models::WorkflowDefinition> {
  auto iter = std::find_if(workflows_.begin(), workflows_.end(),
                           [&workflow_id](const models::WorkflowDefinition& def) -> bool { return def.id == workflow_id; });
  if (iter != workflows_.end()) {
    return *iter;
  }
  return std::nullopt;
}

auto WorkflowService::create_workflow(const models::CreateWorkflowInput& input) -> models::Result<models::WorkflowDefinition> {
  if (input.stages.empty()) {
    return std::string("At least one stage is required");
  }

  auto ids = generate_stage_ids(input.stages);

  auto slug = commands::storage::slugify(input.name);
  if (slug.empty()) {
    return std::string("Workflow name produces an empty slug");
  }

  // Deduplicate slug against existing workflow directories
  std::unordered_set<std::string> existing_ids;
  for (const auto& workflow : workflows_) {
    existing_ids.insert(workflow.id);
  }
  auto final_slug = slug;
  int suffix = 1;
  while (existing_ids.count(final_slug) > 0) {
    final_slug = slug + "-" + std::to_string(suffix);
    ++suffix;
  }

  models::WorkflowDefinition def;
  def.id = final_slug;
  def.name = input.name;
  def.description = input.description;
  def.created_at = now_timestamp();

  for (size_t idx = 0; idx < input.stages.size(); ++idx) {
    models::WorkflowStage stage;
    stage.id = ids[idx];
    stage.name = input.stages[idx].name;
    stage.description = input.stages[idx].description;
    stage.starting_prompt = input.stages[idx].starting_prompt;
    stage.product = input.stages[idx].product;
    def.stages.push_back(std::move(stage));
  }

  apply_product_defaults(def.stages);

  if (!write_workflow_yaml(def)) {
    return std::string("Failed to write workflow to disk");
  }

  workflows_.insert(workflows_.begin(), def);
  return def;
}

auto WorkflowService::update_workflow(const models::UpdateWorkflowInput& input) -> models::Result<models::WorkflowDefinition> {
  if (input.stages.empty()) {
    return std::string("At least one stage is required");
  }

  auto iter = std::find_if(workflows_.begin(), workflows_.end(),
                           [&input](const models::WorkflowDefinition& def) -> bool { return def.id == input.id; });
  if (iter == workflows_.end()) {
    return std::string("Workflow " + input.id + " not found");
  }

  auto ids = resolve_stage_ids(iter->stages, input.stages);

  models::WorkflowDefinition def;
  def.id = input.id;
  def.name = input.name;
  def.description = input.description;
  def.created_at = iter->created_at;

  for (size_t idx = 0; idx < input.stages.size(); ++idx) {
    models::WorkflowStage stage;
    stage.id = ids[idx];
    stage.name = input.stages[idx].name;
    stage.description = input.stages[idx].description;
    stage.starting_prompt = input.stages[idx].starting_prompt;
    stage.product = input.stages[idx].product;
    def.stages.push_back(std::move(stage));
  }

  apply_product_defaults(def.stages);

  if (!write_workflow_yaml(def)) {
    return std::string("Failed to write workflow to disk");
  }

  *iter = def;
  return def;
}

auto WorkflowService::delete_workflow(const std::string& workflow_id) -> models::Result<std::monostate> {
  auto iter = std::find_if(workflows_.begin(), workflows_.end(),
                           [&workflow_id](const models::WorkflowDefinition& def) -> bool { return def.id == workflow_id; });
  if (iter == workflows_.end()) {
    return std::string("Workflow " + workflow_id + " not found");
  }

  try {
    auto dir = workflows_dir() / workflow_id;
    if (fs::is_directory(dir)) {
      fs::remove_all(dir);
    }
  } catch (const std::exception& /*unused*/) {
    return std::string("Failed to delete workflow directory");
  }

  workflows_.erase(iter);
  return std::monostate{};
}

}  // namespace ally::providers
