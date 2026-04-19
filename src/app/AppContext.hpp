#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/models/Task.hpp"
#include "src/opencode/State.hpp"
#include "src/providers/artifact/ArtifactService.hpp"
#include "src/providers/repo_file/FileTaskProvider.hpp"
#include "src/providers/workflow/WorkflowService.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

namespace ally {

struct AppContext {
  std::filesystem::path project_root;
  providers::FileTaskProvider& task_provider;
  providers::WorkflowService& workflow_service;
  providers::ArtifactService& artifact_service;

  std::optional<models::Task> current_task;
  std::vector<std::pair<std::string, std::string>> recent_tasks;

  opencode::OpenCodeState opencode_state;
  std::shared_mutex opencode_mutex;
  opencode::EventQueue event_queue;

  // Provider state (protected by provider_mutex)
  std::optional<std::string> selected_provider;
  std::vector<nlohmann::json> providers;
  std::vector<std::string> connected_providers;
  bool provider_locked = false;
  std::shared_mutex provider_mutex;

  // Rendering config
  std::vector<std::filesystem::path> query_dirs;

  // Watcher broadcast channels
  watcher::WatcherBroadcast<watcher::ArtifactChangedEvent> artifact_broadcast;
  watcher::WatcherBroadcast<watcher::CommandsChangedEvent> commands_broadcast;

  static constexpr size_t kMaxRecentTasks = 5;

  void set_current_task(models::Task task) {
    recent_tasks.erase(std::remove_if(recent_tasks.begin(), recent_tasks.end(),
                                      [&task](const auto& entry) -> bool { return entry.first == task.id; }),
                       recent_tasks.end());
    recent_tasks.insert(recent_tasks.begin(), {task.id, task.name});
    if (recent_tasks.size() > kMaxRecentTasks) {
      recent_tasks.resize(kMaxRecentTasks);
    }
    current_task = std::move(task);
  }
};

}  // namespace ally
