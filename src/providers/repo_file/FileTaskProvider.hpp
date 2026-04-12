#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "src/models/Task.hpp"

namespace ally::providers {

class FileTaskProvider {
 public:
  explicit FileTaskProvider(std::filesystem::path project_root);

  [[nodiscard]] auto list_tasks() const -> std::vector<models::Task>;
  auto create_task(const models::CreateTaskInput& input) -> std::optional<models::Task>;
  [[nodiscard]] auto get_task(const std::string& task_id) const -> std::optional<models::Task>;
  auto update_task(const models::Task& task) -> bool;
  auto create_thread(const std::string& task_id, const models::CreateThreadInput& input) -> std::optional<models::Thread>;
  auto archive_thread(const std::string& task_id, const std::string& thread_id) -> bool;

 private:
  std::filesystem::path project_root_;
};

}  // namespace ally::providers
