#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "src/opencode/Client.hpp"
#include "src/opencode/Error.hpp"
#include "src/opencode/Types.hpp"

namespace ally::opencode {

struct OpenCodeProcess {
  pid_t pid{};
  int port{};
  std::string base_url;
};

enum class ServerStatus : std::uint8_t { Starting, Running, Stopped };

struct ServerCrashedStatus {
  std::string message;
  std::optional<OpenCodeErrorKind> error_kind;
};

using ServerStatusVariant = std::variant<ServerStatus, ServerCrashedStatus>;

struct OpenCodeState {
  std::optional<OpenCodeProcess> process;
  std::optional<OpenCodeClient> client;
  std::string base_url;
  ServerStatusVariant status{ServerStatus::Starting};
};

class EventQueue {
 public:
  static constexpr std::size_t kCapacity = 256;

  auto push(OpenCodeEvent event) -> bool {
    std::scoped_lock lock(mutex_);
    if (queue_.size() >= kCapacity) {
      return false;
    }
    queue_.push_back(std::move(event));
    return true;
  }

  auto drain() -> std::vector<OpenCodeEvent> {
    std::scoped_lock lock(mutex_);
    std::vector<OpenCodeEvent> events(std::make_move_iterator(queue_.begin()), std::make_move_iterator(queue_.end()));
    queue_.clear();
    return events;
  }

 private:
  std::mutex mutex_;
  std::deque<OpenCodeEvent> queue_;
};

}  // namespace ally::opencode
