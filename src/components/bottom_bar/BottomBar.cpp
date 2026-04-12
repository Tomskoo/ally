#include "BottomBar.hpp"

#include <algorithm>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "src/auth/Auth.hpp"
#include "src/components/health_monitor/HealthMonitor.hpp"

using namespace ftxui;

namespace ally::components {

namespace {

auto AggregateColor(AggregateHealth agg) -> Color {
  switch (agg) {
    case AggregateHealth::AllHealthy:
      return Color::Green;
    case AggregateHealth::SomeHealthy:
      return Color::Yellow;
    case AggregateHealth::NoneHealthy:
      return Color::Red;
    case AggregateHealth::Unknown:
      return Color::GrayDark;
  }
  return Color::GrayDark;
}

auto BoxContains(const Box& box, int col, int row) -> bool {
  return col >= box.x_min && col <= box.x_max && row >= box.y_min && row <= box.y_max;
}

struct BottomBarState {
  bool provider_expanded = false;

  // Provider menu state — kept in sync with ctx.providers each render
  std::vector<std::string> provider_entries;
  std::vector<std::string> provider_ids;
  int provider_selected = 0;
};

}  // namespace

auto bottom_bar(AppContext& ctx, Navigator& nav, ftxui::ScreenInteractive& screen) -> Component {
  auto state = std::make_shared<BottomBarState>();
  auto monitor = std::make_shared<HealthMonitor>(ctx, screen);
  auto health_state = monitor->GetState();
  auto health_component = monitor->GetComponent();

  // Boxes for click-away detection
  auto provider_btn_box = std::make_shared<Box>();
  auto health_btn_box = std::make_shared<Box>();
  auto provider_panel_box = std::make_shared<Box>();
  auto health_panel_box = std::make_shared<Box>();

  // --- Provider button with dynamic label via transform ---
  ButtonOption provider_btn_opt = ButtonOption::Ascii();
  provider_btn_opt.transform = [&ctx](const EntryState&) -> Element {
    std::shared_lock lock(ctx.provider_mutex);
    auto label = ctx.selected_provider.value_or("No provider");
    auto el = text(" " + label + " ");
    if (ctx.provider_locked) {
      el = el | dim;
    }
    return el;
  };
  auto provider_btn = Button(
      "",
      [state, health_state, &ctx] -> void {
        if (!ctx.provider_locked) {
          state->provider_expanded = !state->provider_expanded;
          std::scoped_lock lock(health_state->mutex);
          health_state->expanded = false;
        }
      },
      provider_btn_opt);

  // --- Health indicator button with dynamic dot color ---
  ButtonOption health_btn_opt = ButtonOption::Ascii();
  health_btn_opt.transform = [health_state](const EntryState&) -> Element {
    std::scoped_lock lock(health_state->mutex);
    auto dot_color = AggregateColor(health_state->aggregate);
    return text(" ● ") | color(dot_color);
  };
  auto health_btn = Button(
      "",
      [state, health_state] -> void {
        std::scoped_lock lock(health_state->mutex);
        health_state->expanded = !health_state->expanded;
        state->provider_expanded = false;
      },
      health_btn_opt);

  // --- Provider selection menu ---
  auto select_provider = [state, &ctx, &screen] -> void {
    int idx = state->provider_selected;
    if (idx < 0 || idx >= static_cast<int>(state->provider_ids.size())) {
      return;
    }
    auto chosen_id = state->provider_ids[idx];
    bool needs_auth = false;
    {
      std::unique_lock lock(ctx.provider_mutex);
      ctx.selected_provider = chosen_id;
      needs_auth =
          std::find(ctx.connected_providers.begin(), ctx.connected_providers.end(), chosen_id) == ctx.connected_providers.end();
    }
    state->provider_expanded = false;
    if (needs_auth) {
      std::thread([&ctx, chosen_id, &screen] -> void { auth::TryAuthenticateProvider(ctx, chosen_id, screen); }).detach();
    }
  };

  auto provider_menu = Menu({
      .entries = &state->provider_entries,
      .selected = &state->provider_selected,
      .on_change = select_provider,
      .on_enter = select_provider,
  });

  // All interactive elements in one container for event routing
  auto interactive = Container::Vertical({
      Container::Horizontal({provider_btn, health_btn}),
      provider_menu | Maybe(&state->provider_expanded),
      health_component,
  });

  auto component = Renderer(interactive, [=, &ctx]() -> Element {
    // Sync provider menu entries from ctx each frame
    {
      std::shared_lock lock(ctx.provider_mutex);
      state->provider_entries.clear();
      state->provider_ids.clear();
      for (const auto& prov : ctx.providers) {
        auto pid = prov.value("id", "");
        if (pid.empty()) { continue;
}

        bool is_selected = ctx.selected_provider.has_value() && *ctx.selected_provider == pid;
        bool is_connected =
            std::find(ctx.connected_providers.begin(), ctx.connected_providers.end(), pid) != ctx.connected_providers.end();

        std::string entry = pid;
        if (is_connected) { entry += " ✓";
}
        if (is_selected) { entry = "▸ " + entry;
}
        state->provider_entries.push_back(entry);
        state->provider_ids.push_back(pid);
      }
    }

    // Open provider panel when focused via Shift+P
    if (nav.focus_target() == ally::FocusTarget::ProviderBar && !state->provider_expanded) {
      state->provider_expanded = true;
    }

    // Bottom bar row: buttons rendered via their own Render()
    auto bar = hbox({
                   filler(),
                   provider_btn->Render() | reflect(*provider_btn_box),
                   text(" "),
                   health_btn->Render() | reflect(*health_btn_box),
               }) |
               size(HEIGHT, EQUAL, 1);

    // Provider panel
    if (state->provider_expanded) {
      Elements panel_rows;
      if (state->provider_entries.empty()) {
        panel_rows.push_back(text(" No providers available ") | dim);
      } else {
        panel_rows.push_back(provider_menu->Render());
      }

      auto panel = vbox(std::move(panel_rows)) | border | size(WIDTH, EQUAL, 35) | clear_under | reflect(*provider_panel_box);
      return vbox({
          filler(),
          hbox({filler(), panel}),
          bar,
      });
    }

    // Health panel
    {
      std::scoped_lock lock(health_state->mutex);
      if (health_state->expanded) {
        auto panel = health_component->Render() | clear_under | reflect(*health_panel_box);
        return vbox({
            filler(),
            hbox({filler(), panel}),
            bar,
        });
      }
    }

    return bar;
  });

  // Click-away dismissal
  component = CatchEvent(component, [=](Event event) -> bool {
    if (!event.is_mouse()) { return false;
}
    auto& mouse = event.mouse();

    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) {
      return false;
    }

    // Click-away for provider panel
    if (state->provider_expanded) {
      if (!BoxContains(*provider_btn_box, mouse.x, mouse.y) && !BoxContains(*provider_panel_box, mouse.x, mouse.y)) {
        state->provider_expanded = false;
        return true;
      }
    }

    // Click-away for health panel
    {
      std::scoped_lock lock(health_state->mutex);
      if (health_state->expanded) {
        if (!BoxContains(*health_btn_box, mouse.x, mouse.y) && !BoxContains(*health_panel_box, mouse.x, mouse.y)) {
          health_state->expanded = false;
          return true;
        }
      }
    }

    return false;
  });

  return component;
}

}  // namespace ally::components
