#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "src/opencode/Types.hpp"

namespace ally::opencode::sse {

struct SseSubscription {
  std::atomic<bool> stop_flag{false};
  std::thread thread;

  void Stop();
  ~SseSubscription();

  SseSubscription() = default;
  SseSubscription(const SseSubscription&) = delete;
  auto operator=(const SseSubscription&) -> SseSubscription& = delete;
  SseSubscription(SseSubscription&&) = delete;
  auto operator=(SseSubscription&&) -> SseSubscription& = delete;
};

auto SubscribeEvents(const std::string& base_url, std::function<void(OpenCodeEvent)> callback) -> std::unique_ptr<SseSubscription>;

}  // namespace ally::opencode::sse
