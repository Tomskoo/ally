#pragma once

#include <cstdint>
#include <string>

namespace ally::watcher {

struct ArtifactChangedEvent {
  enum class Kind : std::uint8_t { Created, Modified };
  Kind kind;
  std::string task_id;
  std::string thread_id;
  std::string stage;
};

struct SkillsChangedEvent {
  // No payload fields. Consumers treat any instance as a full cache invalidation.
};

}  // namespace ally::watcher
