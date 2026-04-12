#pragma once

#include <atomic>
#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "src/app/AppContext.hpp"

namespace ally::components {

enum class AggregateHealth : uint8_t {
  AllHealthy,
  SomeHealthy,
  NoneHealthy,
  Unknown,
};

enum class ServiceStatusKind : uint8_t {
  Starting,
  Running,
  Crashed,
  Stopped,
  Unknown,
};

struct ServiceStatus {
  ServiceStatusKind kind = ServiceStatusKind::Unknown;
  std::string crash_message;
};

struct HealthMonitorState {
  std::mutex mutex;
  ServiceStatus status;
  std::optional<std::string> version;
  std::optional<std::string> error_msg;
  std::optional<bool> provider_auth_ok;
  AggregateHealth aggregate = AggregateHealth::Unknown;
  bool expanded = false;
};

auto ComputeAggregate(const ServiceStatus& status, bool healthy, std::optional<bool> provider_auth_ok) -> AggregateHealth;

class HealthMonitor {
 public:
  HealthMonitor(AppContext& ctx, ftxui::ScreenInteractive& screen);
  ~HealthMonitor();

  HealthMonitor(const HealthMonitor&) = delete;
  auto operator=(const HealthMonitor&) -> HealthMonitor& = delete;

  auto GetComponent() -> ftxui::Component;
  auto GetState() -> std::shared_ptr<HealthMonitorState>;

 private:
  void PollLoop();

  AppContext& ctx_;
  ftxui::ScreenInteractive& screen_;
  std::shared_ptr<HealthMonitorState> state_;
  std::atomic<bool> stop_{false};
  std::thread poll_thread_;
};

}  // namespace ally::components
