#include <algorithm>
#include <exception>
#include <filesystem>
#include <ftxui/component/component.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "AppContext.hpp"
#include "NavState.hpp"
#include "Navigator.hpp"
#include "src/commands/storage/Storage.hpp"
#include "src/components/bottom_bar/BottomBar.hpp"
#include "src/components/nav_bar/NavBar.hpp"
#include "src/components/splash/Splash.hpp"
#include "src/components/scrollable/ScrollableNode.hpp"
#include "src/configuration/Configuration.hpp"
#include "src/configuration/InputConfig.hpp"
#include "src/opencode/Error.hpp"
#include "src/opencode/Lifecycle.hpp"
#include "src/opencode/Service.hpp"
#include "src/opencode/Sse.hpp"
#include "src/providers/artifact/ArtifactService.hpp"
#include "src/services/watcher/ArtifactWatcher.hpp"
#include "src/services/watcher/CommandsWatcher.hpp"
#include "src/views/board/Board.hpp"
#include "src/views/create_task/CreateTask.hpp"
#include "src/views/create_thread/CreateThread.hpp"
#include "src/views/stage_view/StageView.hpp"
#include "src/views/task_detail/TaskDetail.hpp"
#include "src/views/quick_chat/QuickChat.hpp"
#include "src/views/workflow_form/WorkflowForm.hpp"
#include "src/views/workflow_list/WorkflowList.hpp"

using namespace ftxui;

namespace {

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

constexpr int kScrollLines = 3;

auto has_flag(int argc, char** argv, const char* flag) -> bool {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == flag) return true;
  }
  return false;
}
auto parent_state(const ally::NavState& current) -> std::optional<ally::NavState> {
  return std::visit(
      overloaded{
          [](const ally::BoardState&) -> std::optional<ally::NavState> { return std::nullopt; },
          [](const ally::NewTaskState&) -> std::optional<ally::NavState> { return ally::BoardState{}; },
          [](const ally::TaskDetailState&) -> std::optional<ally::NavState> { return ally::BoardState{}; },
          [](const ally::StageViewState& state) -> std::optional<ally::NavState> { return ally::TaskDetailState{state.task_id}; },
          [](const ally::NewThreadState& state) -> std::optional<ally::NavState> { return ally::TaskDetailState{state.task_id}; },
          [](const ally::WorkflowsState&) -> std::optional<ally::NavState> { return ally::BoardState{}; },
          [](const ally::NewWorkflowState&) -> std::optional<ally::NavState> { return ally::WorkflowsState{}; },
          [](const ally::EditWorkflowState&) -> std::optional<ally::NavState> { return ally::WorkflowsState{}; },
          [](const ally::QuickChatState&) -> std::optional<ally::NavState> { return ally::BoardState{}; },
      },
      current);
}

auto parse_project_root(int argc, char** argv) -> std::filesystem::path {
  std::filesystem::path project_root;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--project" && i + 1 < argc) {
      project_root = argv[++i];
      break;
    }
  }
  if (project_root.empty()) {
    project_root = ally::commands::storage::find_project_root();
  }
  // Resolve symlinks so that efsw real paths match the root used by
  // ParseArtifactPath.  Under `bazel run` the execroot contains symlinks
  // (e.g. .ally -> /real/path/.ally), so canonicalise via the .ally dir.
  auto ally_dir = project_root / ".ally";
  if (std::filesystem::is_symlink(ally_dir)) {
    project_root = std::filesystem::canonical(ally_dir).parent_path();
  }
  return project_root;
}

auto init_opencode_server(ally::AppContext& ctx, const ally::configuration::OpenCodeConfig& cfg) -> void {
  if (cfg.server_url.has_value()) {
    const auto& url = *cfg.server_url;
    auto health = ally::opencode::lifecycle::WaitForHealth(url);
    std::unique_lock lock(ctx.opencode_mutex);
    if (ally::opencode::is_ok(health)) {
      ctx.opencode_state.base_url = url;
      ctx.opencode_state.client = ally::opencode::OpenCodeClient::Create(url);
      ctx.opencode_state.status = ally::opencode::ServerStatus::Running;
    } else {
      ctx.opencode_state.status = ally::opencode::ServerCrashedStatus{ally::opencode::get_error(health).message};
    }
    return;
  }
  auto spawn_result = ally::opencode::lifecycle::Spawn({.working_dir = ctx.project_root});
  std::unique_lock lock(ctx.opencode_mutex);
  if (ally::opencode::is_ok(spawn_result)) {
    const auto& proc = ally::opencode::get_value(spawn_result);
    ctx.opencode_state.process = proc;
    ctx.opencode_state.base_url = proc.base_url;
    ctx.opencode_state.client = ally::opencode::OpenCodeClient::Create(proc.base_url);
    ctx.opencode_state.status = ally::opencode::ServerStatus::Running;
  } else {
    ctx.opencode_state.status = ally::opencode::ServerCrashedStatus{ally::opencode::get_error(spawn_result).message};
  }
}

auto init_providers(ally::AppContext& ctx) -> void {
  auto result = ally::opencode::ListProviders(ctx.opencode_state, ctx.opencode_mutex);
  if (!ally::opencode::is_ok(result)) {
    return;
  }
  const auto& provider_list = ally::opencode::get_value(result);
  std::unique_lock lock(ctx.provider_mutex);
  ctx.providers = provider_list.providers;
  ctx.connected_providers = provider_list.connected;
  // Keep the configured default if it's among the connected providers;
  // otherwise fall back to the first connected or first available.
  if (!ctx.selected_provider.has_value()) {
    return;
  }
  auto& default_id = *ctx.selected_provider;
  bool default_connected =
      std::find(provider_list.connected.begin(), provider_list.connected.end(), default_id) != provider_list.connected.end();
  if (default_connected) {
    return;
  }
  if (!provider_list.connected.empty()) {
    ctx.selected_provider = provider_list.connected.front();
  } else if (!provider_list.providers.empty()) {
    ctx.selected_provider = provider_list.providers.front().value("id", "");
  }
}

struct AppComponents {
  Component active_proxy;
  Component nav_bar;
  Component bottom_bar;
};

auto handle_mouse_wheel(Event event, ally::Navigator& nav, const Component& active_proxy) -> std::optional<bool> {
  if (!event.is_mouse()) {
    return std::nullopt;
  }
  const auto& mouse = event.mouse();
  if (mouse.button != Mouse::WheelUp && mouse.button != Mouse::WheelDown) {
    return std::nullopt;
  }
  if (active_proxy->OnEvent(event)) {
    return true;
  }
  if (mouse.button == Mouse::WheelUp) {
    nav.scroll_y = std::max(0, nav.scroll_y - kScrollLines);
  } else {
    nav.scroll_y += kScrollLines;
  }
  return true;
}

auto handle_escape(const Event& event, ally::Navigator& nav, const Component& active_proxy,
                   const ally::configuration::InputConfig& input_config) -> std::optional<bool> {
  if (!input_config.navigation.escape.matches(event)) {
    return std::nullopt;
  }
  auto target = nav.focus_target();
  if (target == ally::FocusTarget::Navbar || target == ally::FocusTarget::ProviderBar) {
    nav.set_focus(ally::FocusTarget::MainView);
    active_proxy->TakeFocus();
    return true;
  }
  // Let active component try (overlay dismissal)
  if (active_proxy->OnEvent(event)) {
    return true;
  }
  auto parent = parent_state(nav.current());
  if (parent.has_value()) {
    nav.go(*parent);
    return true;
  }
  return false;
}

auto handle_navbar_cycle(const Event& event, ally::Navigator& nav, const AppComponents& components,
                        const ally::configuration::InputConfig& input_config) -> std::optional<bool> {
  bool is_right = input_config.navigation.cycle_right.matches(event);
  bool is_left  = input_config.navigation.cycle_left.matches(event);
  if (!is_right && !is_left) {
    return std::nullopt;
  }
  if (std::holds_alternative<ally::StageViewState>(nav.current())) {
    return components.active_proxy->OnEvent(event);
  }
  auto count = nav.navbar_element_count();
  if (count == 0) {
    return false;
  }
  nav.set_focus(ally::FocusTarget::Navbar);
  components.nav_bar->TakeFocus();
  auto cursor = nav.navbar_cursor();
  if (is_right) {
    auto idx = cursor.has_value() ? (*cursor + 1) % count : size_t{0};
    nav.set_navbar_cursor(idx);
  } else {
    auto idx = cursor.has_value() ? (*cursor + count - 1) % count : count - 1;
    nav.set_navbar_cursor(idx);
  }
  return true;
}

auto handle_arrow_focus(const Event& event, ally::Navigator& nav, const Component& active_proxy) -> void {
  if (event == Event::ArrowUp || event == Event::ArrowDown || event == Event::ArrowLeft || event == Event::ArrowRight) {
    nav.set_focus(ally::FocusTarget::MainView);
    active_proxy->TakeFocus();
  }
}

auto handle_enter_on_navbar(const Event& event, ally::Navigator& nav, const AppComponents& components) -> std::optional<bool> {
  if (event != Event::Return || nav.focus_target() != ally::FocusTarget::Navbar) {
    return std::nullopt;
  }
  components.nav_bar->OnEvent(event);
  nav.set_focus(ally::FocusTarget::MainView);
  components.active_proxy->TakeFocus();
  return true;
}

auto handle_app_event(const Event& event, ally::Navigator& nav, const AppComponents& components,
                      const ally::configuration::InputConfig& input_config) -> bool {
  // 1. Mouse wheel
  if (auto handled = handle_mouse_wheel(event, nav, components.active_proxy); handled.has_value()) {
    return *handled;
  }
  // 2a. Escape — focus reset / overlay dismiss / parent nav
  if (auto handled = handle_escape(event, nav, components.active_proxy, input_config); handled.has_value()) {
    return *handled;
  }
  // 2b. [ and ] — stage nav in StageView, navbar cursor elsewhere
  if (auto handled = handle_navbar_cycle(event, nav, components, input_config); handled.has_value()) {
    return *handled;
  }
  // 2c. Arrow keys — focus MainView, fall through
  handle_arrow_focus(event, nav, components.active_proxy);
  // 2d. Enter when navbar is focused — activate element
  if (auto handled = handle_enter_on_navbar(event, nav, components); handled.has_value()) {
    return *handled;
  }
  // 3. Remaining: active → bottom bar → navbar
  if (components.active_proxy->OnEvent(event)) {
    return true;
  }
  // 4. Meta+P — focus provider bar
  if (input_config.navigation.focus_provider.matches(event)) {
    nav.set_focus(ally::FocusTarget::ProviderBar);
    components.bottom_bar->TakeFocus();
    return true;
  }
  // 5. Meta+W — open/reset Quick Chat
  if (input_config.navigation.toggle_quick_chat.matches(event)) {
    if (std::holds_alternative<ally::QuickChatState>(nav.current())) {
      nav.replace(ally::QuickChatState{});
    } else {
      nav.go(ally::QuickChatState{});
    }
    return true;
  }
  if (components.bottom_bar->OnEvent(event)) {
    return true;
  }
  if (components.nav_bar->OnEvent(event)) {
    return true;
  }
  // Always consume — prevent Container::Vertical's
  // default event handling from interfering.
  return true;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
  try {
    auto screen = ScreenInteractive::Fullscreen();

    bool show_splash = !has_flag(argc, argv, "--skip-splash");

    auto project_root = parse_project_root(argc, argv);

    auto provider = ally::providers::FileTaskProvider(project_root);
    auto workflow_service = ally::providers::WorkflowService(project_root);
    auto artifact_service = ally::providers::ArtifactService(project_root);

    auto opencode_config = ally::configuration::LoadOpenCodeConfig(project_root);
    auto rendering_config = ally::configuration::LoadRenderingConfig(project_root);
    auto input_config = ally::configuration::DefaultInputConfig();

    ally::AppContext ctx{
        .project_root = project_root,
        .input_config = input_config,
        .task_provider = provider,
        .workflow_service = workflow_service,
        .artifact_service = artifact_service,
        .selected_provider = opencode_config.default_provider.value_or("opencode"),
        .provider_locked = opencode_config.lock_provider,
        .query_dirs = std::move(rendering_config.query_dirs),
      .theme_name = std::move(rendering_config.theme),
    };

    init_opencode_server(ctx, opencode_config);
    init_providers(ctx);

    // Start SSE event subscription
    std::unique_ptr<ally::opencode::sse::SseSubscription> sse_sub;
    {
      std::shared_lock lock(ctx.opencode_mutex);
      if (!ctx.opencode_state.base_url.empty()) {
        sse_sub = ally::opencode::sse::SubscribeEvents(
            ctx.opencode_state.base_url, [&ctx, &screen](ally::opencode::OpenCodeEvent evt) -> void {
              ctx.event_queue.push(std::move(evt));
              screen.PostEvent(Event::Custom);
            });
      }
    }

    // Start filesystem watchers
    auto artifact_watcher = std::make_unique<ally::watcher::ArtifactWatcher>(project_root, ctx.artifact_broadcast);
    auto commands_watcher = std::make_unique<ally::watcher::CommandsWatcher>(project_root, ctx.commands_broadcast);

    auto active_component = std::make_shared<Component>();

    // Proxy container for the active view — lets FTXUI's focus tree
    // manage Focused() correctly across navbar / content / bottom bar.
    auto active_proxy = Container::Vertical({});

    std::function<void()> rebuild_component;
    ally::Navigator nav([&rebuild_component]() -> void { rebuild_component(); });

    rebuild_component = [&]() -> void {
      std::visit(
          overloaded{
              [&](const ally::BoardState&) -> void { *active_component = ally::views::task_board(ctx, nav) | border; },
              [&](const ally::NewTaskState&) -> void { *active_component = ally::views::create_task(ctx, nav) | border; },
              [&](const ally::TaskDetailState& state) -> void {
                *active_component = ally::views::task_detail(ctx, nav, state.task_id) | border;
              },
              [&](const ally::StageViewState& state) -> void {
                *active_component = ally::views::stage_view(ctx, nav, screen, state.task_id, state.thread_id, state.stage) | border;
              },
              [&](const ally::NewThreadState& state) -> void {
                *active_component = ally::views::create_thread(ctx, nav, state.task_id) | border;
              },
              [&](const ally::WorkflowsState&) -> void { *active_component = ally::views::workflow_list(ctx, nav) | border; },
              [&](const ally::NewWorkflowState&) -> void {
                *active_component = ally::views::workflow_form(ctx, nav, std::nullopt) | border;
              },
              [&](const ally::EditWorkflowState& state) -> void {
                *active_component = ally::views::workflow_form(ctx, nav, state.workflow_id) | border;
              },
              [&](const ally::QuickChatState&) -> void {
                *active_component = ally::views::quick_chat(ctx, nav, screen) | border;
              },
          },
          nav.current());
      active_proxy->DetachAllChildren();
      active_proxy->Add(*active_component);
    };

    rebuild_component();

    auto nav_bar_component = ally::components::nav_bar(ctx, nav);
    auto bottom_bar_component = ally::components::bottom_bar(ctx, nav, screen);

    // Focus tree — Container::Vertical manages which child is focused.
    // TakeFocus() on a child makes the others report Focused() == false,
    // so e.g. the Board's Menu stops rendering its selection as inverted.
    auto focus_tree = Container::Vertical({
        nav_bar_component,
        active_proxy,
        bottom_bar_component,
    });
    active_proxy->TakeFocus();

    // Custom rendering via dbox — ignores Container::Vertical's default layout.
    auto renderer = Renderer(focus_tree, [&]() -> Element {
      auto nav_element = nav_bar_component->Render();
      auto content = active_proxy->ChildCount() > 0 ? active_proxy->ChildAt(0)->Render() : text("");
      auto bottom_el = bottom_bar_component->Render();

      return dbox({
          vbox({
              // Reserve space for the navbar header (1 line)
              emptyElement() | size(HEIGHT, EQUAL, 1),
              ally::components::make_scrollable(content | vscroll_indicator, &nav.scroll_y) | flex,
              // Reserve space for the bottom bar (1 line)
              emptyElement() | size(HEIGHT, EQUAL, 1),
          }),
          vbox({
              // Navbar header and open dropdown panels render on top via dbox
              nav_element,
              filler(),
          }),
          vbox({
              // Bottom bar and its panels float at the bottom via dbox
              filler(),
              bottom_el,
          }),
      });
    });

    AppComponents components{
        .active_proxy = active_proxy,
        .nav_bar = nav_bar_component,
        .bottom_bar = bottom_bar_component,
    };
    auto main_component = CatchEvent(renderer, [&](const Event& event) -> bool { return handle_app_event(event, nav, components, ctx.input_config); });

    if (show_splash) {
      auto splash_done = std::make_shared<bool>(false);
      auto splash = ally::components::splash_screen(screen, [&]() { *splash_done = true; });

      auto root = Renderer(main_component, [&]() -> Element {
        if (!*splash_done) {
          return splash->Render();
        }
        return main_component->Render();
      }) | CatchEvent([&](const Event& event) -> bool {
        if (!*splash_done) {
          return splash->OnEvent(event);
        }
        return main_component->OnEvent(event);
      });

      screen.Loop(root);
    } else {
      screen.Loop(main_component);
    }

    // Stop SSE subscription before shutting down the server
    sse_sub.reset();

    // Shutdown the opencode server if we spawned one
    {
      std::unique_lock lock(ctx.opencode_mutex);
      if (ctx.opencode_state.process) {
        ally::opencode::lifecycle::Shutdown(*ctx.opencode_state.process);
      }
    }

    return 0;
  } catch (const std::exception&) {
    return 1;
  }
}
