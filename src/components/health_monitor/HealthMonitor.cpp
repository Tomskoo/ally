#include "HealthMonitor.hpp"

#include <algorithm>
#include <chrono>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <shared_mutex>
#include <variant>

#include "src/opencode/Error.hpp"
#include "src/opencode/Service.hpp"
#include "src/opencode/State.hpp"

using namespace ftxui;

namespace ally::components {

namespace {

auto ParseServerStatus(const opencode::ServerStatusVariant& variant) -> ServiceStatus {
  return std::visit(
      [](auto&& val) -> ServiceStatus {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, opencode::ServerStatus>) {
          switch (val) {
            case opencode::ServerStatus::Running:
              return {ServiceStatusKind::Running, {}};
            case opencode::ServerStatus::Starting:
              return {ServiceStatusKind::Starting, {}};
            case opencode::ServerStatus::Stopped:
              return {ServiceStatusKind::Stopped, {}};
          }
          return {ServiceStatusKind::Unknown, {}};
        } else if constexpr (std::is_same_v<T, opencode::ServerCrashedStatus>) {
          return {ServiceStatusKind::Crashed, val.message};
        } else {
          return {ServiceStatusKind::Unknown, {}};
        }
      },
      variant);
}

auto StatusKindText(ServiceStatusKind kind) -> std::string {
  switch (kind) {
    case ServiceStatusKind::Running:
      return "running";
    case ServiceStatusKind::Starting:
      return "starting";
    case ServiceStatusKind::Crashed:
      return "crashed";
    case ServiceStatusKind::Stopped:
      return "stopped";
    case ServiceStatusKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

auto StatusKindColor(ServiceStatusKind kind) -> Color {
  switch (kind) {
    case ServiceStatusKind::Running:
      return Color::Green;
    case ServiceStatusKind::Starting:
      return Color::Yellow;
    case ServiceStatusKind::Crashed:
      return Color::Red;
    case ServiceStatusKind::Stopped:
    case ServiceStatusKind::Unknown:
      return Color::GrayDark;
  }
  return Color::GrayDark;
}

auto AuthBadge(std::optional<bool> auth_ok) -> Element {
  if (!auth_ok.has_value()) {
    return text("-") | color(Color::GrayDark);
  }
  if (*auth_ok) {
    return text("v") | color(Color::Green);
  }
  return text("x") | color(Color::Red);
}

}  // namespace

auto ComputeAggregate(const ServiceStatus& status, bool healthy, std::optional<bool> provider_auth_ok) -> AggregateHealth {
  if (status.kind == ServiceStatusKind::Running && healthy) {
    if (provider_auth_ok.has_value() && !*provider_auth_ok) {
      return AggregateHealth::SomeHealthy;
    }
    return AggregateHealth::AllHealthy;
  }
  if (status.kind == ServiceStatusKind::Starting) {
    return AggregateHealth::SomeHealthy;
  }
  return AggregateHealth::NoneHealthy;
}

HealthMonitor::HealthMonitor(AppContext& ctx, ftxui::ScreenInteractive& screen)
    : ctx_(ctx), screen_(screen), state_(std::make_shared<HealthMonitorState>()) {
  poll_thread_ = std::thread([this] -> void { PollLoop(); });
}

HealthMonitor::~HealthMonitor() {
  stop_.store(true);
  if (poll_thread_.joinable()) {
    poll_thread_.join();
  }
}

void HealthMonitor::PollLoop() {
  while (!stop_.load()) {
    // Step 1: Fetch server status
    auto status_variant = opencode::GetStatus(ctx_.opencode_state, ctx_.opencode_mutex);
    auto status = ParseServerStatus(status_variant);

    // Step 2: Fetch health details
    bool healthy = false;
    std::optional<std::string> version;
    auto health_result = opencode::Health(ctx_.opencode_state, ctx_.opencode_mutex);
    if (opencode::is_ok(health_result)) {
      const auto& resp = opencode::get_value(health_result);
      healthy = resp.healthy;
      version = resp.version;
    }

    // Step 3: Extract crash message
    std::optional<std::string> error_msg;
    if (status.kind == ServiceStatusKind::Crashed && !status.crash_message.empty()) {
      error_msg = status.crash_message;
    }

    // Step 5: Check provider auth
    std::optional<bool> provider_auth_ok;
    {
      std::shared_lock lock(ctx_.provider_mutex);
      if (ctx_.selected_provider.has_value()) {
        auto& connected = ctx_.connected_providers;
        bool found = std::find(connected.begin(), connected.end(), *ctx_.selected_provider) != connected.end();
        provider_auth_ok = found;
      }
    }

    // Step 6: Compute aggregate
    auto aggregate = ComputeAggregate(status, healthy, provider_auth_ok);

    // Step 7: Update local state
    {
      std::scoped_lock lock(state_->mutex);
      state_->status = status;
      state_->version = version;
      state_->error_msg = error_msg;
      state_->provider_auth_ok = provider_auth_ok;
      state_->aggregate = aggregate;
    }

    screen_.PostEvent(Event::Custom);

    // Step 8: Sleep 20s in 1s chunks, checking stop flag
    for (int i = 0; i < 20 && !stop_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

auto HealthMonitor::GetState() -> std::shared_ptr<HealthMonitorState> { return state_; }

auto HealthMonitor::GetComponent() -> ftxui::Component {
  auto state = state_;

  return Renderer([state] -> Element {
    std::scoped_lock lock(state->mutex);

    if (!state->expanded) {
      return emptyElement();
    }

    auto status_text = StatusKindText(state->status.kind);
    auto status_color = StatusKindColor(state->status.kind);

    Elements rows;
    rows.push_back(text("SERVICE HEALTH") | bold);
    rows.push_back(separator());
    rows.push_back(hbox({
        text("opencode") | bold,
        filler(),
        text(status_text) | color(status_color),
    }));

    if (state->version.has_value()) {
      rows.push_back(text("Version: " + *state->version) | dim);
    }

    rows.push_back(hbox({
        text("Provider Auth") | bold,
        filler(),
        AuthBadge(state->provider_auth_ok),
    }));

    if (state->error_msg.has_value()) {
      rows.push_back(text(*state->error_msg) | color(Color::Red));
    }

    return vbox(std::move(rows)) | border | size(WIDTH, EQUAL, 40);
  });
}

}  // namespace ally::components
