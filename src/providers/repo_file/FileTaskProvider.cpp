#include "FileTaskProvider.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_set>

#include "src/commands/storage/Storage.hpp"

namespace ally::providers {

FileTaskProvider::FileTaskProvider(std::filesystem::path project_root) : project_root_(std::move(project_root)) {}

auto FileTaskProvider::list_tasks() const -> std::vector<models::Task> {
  if (!commands::storage::is_initialized(project_root_)) {
    return {};
  }

  auto ids = commands::storage::list_task_ids(project_root_);
  std::vector<models::Task> tasks;
  tasks.reserve(ids.size());

  for (const auto& task_id : ids) {
    auto task = commands::storage::read_task(project_root_, task_id);
    if (task.has_value() && !task->archived) {
      tasks.push_back(std::move(*task));
    }
  }

  std::sort(tasks.begin(), tasks.end(), [](const models::Task& lhs, const models::Task& rhs) -> bool {
    try {
      return std::stoull(lhs.last_activity) > std::stoull(rhs.last_activity);
    } catch (...) {
      return lhs.last_activity > rhs.last_activity;
    }
  });

  return tasks;
}

auto FileTaskProvider::create_task(const models::CreateTaskInput& input) -> std::optional<models::Task> {
  try {
    auto slug = commands::storage::slugify(input.name);
    if (slug.empty()) {
      return std::nullopt;
    }

    // Deduplicate slug
    auto existing_ids = commands::storage::list_task_ids(project_root_);
    auto id_set = std::unordered_set<std::string>(existing_ids.begin(), existing_ids.end());
    auto final_slug = slug;
    int suffix = 1;
    while (id_set.count(final_slug) > 0) {
      final_slug = slug + "-" + std::to_string(suffix);
      ++suffix;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto timestamp = std::to_string(now);

    models::Task task;
    task.id = final_slug;
    task.name = input.name;
    task.stage = "";
    task.created_at = timestamp;
    task.last_activity = timestamp;
    task.description = input.description;
    task.archived = false;

    if (!commands::storage::is_initialized(project_root_)) {
      commands::storage::init_workspace(project_root_);
    }

    if (!commands::storage::write_task(project_root_, task)) {
      return std::nullopt;
    }

    return task;
  } catch (...) {
    return std::nullopt;
  }
}

auto FileTaskProvider::get_task(const std::string& task_id) const -> std::optional<models::Task> {
  return commands::storage::read_task(project_root_, task_id);
}

auto FileTaskProvider::update_task(const models::Task& task) -> bool { return commands::storage::write_task(project_root_, task); }

auto FileTaskProvider::create_thread(const std::string& task_id, const models::CreateThreadInput& input)
    -> std::optional<models::Thread> {
  try {
    auto slug = commands::storage::slugify(input.name);
    if (slug.empty()) {
      return std::nullopt;
    }

    // Deduplicate slug against existing thread directories
    auto threads_dir = project_root_ / ".ally" / "tasks" / task_id / "threads";
    std::unordered_set<std::string> existing_ids;
    if (std::filesystem::is_directory(threads_dir)) {
      for (const auto& entry : std::filesystem::directory_iterator(threads_dir)) {
        if (entry.is_directory()) {
          existing_ids.insert(entry.path().filename().string());
        }
      }
    }

    auto final_slug = slug;
    int suffix = 1;
    while (existing_ids.count(final_slug) > 0) {
      final_slug = slug + "-" + std::to_string(suffix);
      ++suffix;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto timestamp = std::to_string(now);

    models::Thread thread;
    thread.id = final_slug;
    thread.name = input.name;
    thread.status = models::ThreadStatus::Idle;
    thread.current_stage = input.first_stage;
    thread.workflow_id = input.workflow_id.value_or("");
    thread.created_at = timestamp;
    thread.last_activity = timestamp;
    thread.archived = false;

    if (!commands::storage::write_thread(project_root_, task_id, thread)) {
      return std::nullopt;
    }

    return thread;
  } catch (...) {
    return std::nullopt;
  }
}

auto FileTaskProvider::archive_thread(const std::string& task_id, const std::string& thread_id) -> bool {
  return commands::storage::archive_thread(project_root_, task_id, thread_id);
}

}  // namespace ally::providers
