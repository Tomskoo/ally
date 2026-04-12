#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ally::models {

enum class ThreadStatus : std::uint8_t { Running, Idle, Completed };

struct Thread {
  std::string id;
  std::string name;
  std::string current_stage;
  std::string workflow_id;
  std::string created_at;
  std::string last_activity;
  ThreadStatus status = ThreadStatus::Running;
  bool archived = false;
  std::unordered_map<std::string, std::string> stage_sessions;  // stage → session_id
};

struct Task {
  std::string id;
  std::string name;
  std::string stage;  // computed at read time, not stored on disk
  std::string created_at;
  std::string last_activity;
  std::optional<std::string> description;
  std::vector<Thread> threads;
  bool archived = false;
};

struct CreateTaskInput {
  std::string name;
  std::optional<std::string> description;
};

struct CreateThreadInput {
  std::string name;
  std::optional<std::string> workflow_id;
  std::string first_stage;
};

}  // namespace ally::models
