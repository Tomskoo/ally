#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ally::providers {

class ArtifactService {
 public:
  explicit ArtifactService(std::filesystem::path project_root);

  [[nodiscard]] auto list_stage_artifacts(const std::string& task_id, const std::string& thread_id) const
      -> std::vector<std::string>;

  [[nodiscard]] auto get_artifact(const std::string& task_id, const std::string& thread_id, const std::string& stage) const
      -> std::optional<std::string>;

 private:
  std::filesystem::path project_root_;
};

}  // namespace ally::providers
