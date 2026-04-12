#include "NavBar.hpp"

#include <algorithm>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/models/Workflow.hpp"

using namespace ftxui;

namespace ally::components {

namespace {

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// TODO: re-implement navigation dropdowns for task/thread switching

struct NavBarAction {
  std::function<void()> activate;
};

struct NavBarState {
  std::vector<models::WorkflowStage> workflow_stages;
  std::vector<std::string> artifact_stages;
  std::string cached_workflow_id;
  std::string last_nav_key;
  std::vector<NavBarAction> cursor_actions;
};

auto nav_key_str(const ally::NavState& nav) -> std::string {
  return std::visit(overloaded{
                        [](const ally::BoardState&) -> std::string { return std::string("board"); },
                        [](const ally::NewTaskState&) -> std::string { return std::string("new_task"); },
                        [](const ally::TaskDetailState& state) -> std::string { return "task:" + state.task_id; },
                        [](const ally::NewThreadState& state) -> std::string { return "new_thread:" + state.task_id; },
                        [](const ally::StageViewState& state) -> std::string { return "stage:" + state.task_id + ":" + state.thread_id + ":" + state.stage; },
                        [](const ally::WorkflowsState&) -> std::string { return std::string("workflows"); },
                        [](const ally::NewWorkflowState&) -> std::string { return std::string("new_workflow"); },
                        [](const ally::EditWorkflowState& state) -> std::string { return "edit_wf:" + state.workflow_id; },
                    },
                    nav);
}

}  // namespace

auto nav_bar(ally::AppContext& ctx, ally::Navigator& nav) -> Component {
  auto state = std::make_shared<NavBarState>();

  // Back button
  auto back_btn = Button("<", [&nav] -> void { nav.back(); }, ButtonOption::Ascii());

  // Workflows shortcut button (far right)
  auto wf_btn = Button("[wf]", [&nav] -> void { nav.go(ally::WorkflowsState{}); }, ButtonOption::Ascii());

  // Stable breadcrumb link buttons
  auto tasks_link_btn = Button("Tasks", [&nav] -> void { nav.go(ally::BoardState{}); }, ButtonOption::Ascii());
  auto workflows_link_btn = Button("Workflows", [&nav] -> void { nav.go(ally::WorkflowsState{}); }, ButtonOption::Ascii());

  // Task name link button — label is dynamic via transform, target updated
  // each render pass via task_link_id.
  auto task_link_id = std::make_shared<std::string>("");
  ButtonOption task_link_opt;
  task_link_opt.transform = [task_link_id, &ctx](const EntryState&) -> Element {
    std::string label = ctx.current_task.has_value() ? ctx.current_task->name : *task_link_id;
    return text(label) | color(Color::Blue);
  };
  auto task_link_btn = Button("", [&nav, task_link_id] -> void { nav.go(ally::TaskDetailState{*task_link_id}); }, task_link_opt);

  auto show_task_link = std::make_shared<bool>(false);

  // Stage chip container — children rebuilt when workflow_id changes
  auto stage_container = Container::Horizontal({});
  auto stage_btns = std::make_shared<std::vector<Component>>();

  // All interactive elements in one horizontal container for event routing
  auto interactive = Container::Horizontal({
      back_btn,
      tasks_link_btn,
      workflows_link_btn,
      Maybe(task_link_btn, show_task_link.get()),
      stage_container,
      wf_btn,
  });

  auto renderer_comp = Renderer(interactive, [=, &ctx, &nav]() -> Element {
    const std::size_t history_len = nav.history().size();
    const std::string cur_key = nav_key_str(nav.current());

    if (cur_key != state->last_nav_key) {
      state->last_nav_key = cur_key;
    }

    // Separator element
    auto sep = [] -> Element { return text(" > ") | color(Color::GrayDark); };

    // Cursor system: rebuild action list each render pass
    state->cursor_actions.clear();
    bool navbar_focused = nav.focus_target() == ally::FocusTarget::Navbar;
    auto cursor_pos = nav.navbar_cursor();

    // Register an element with the cursor system. Returns the element
    // with inverted styling if it's the cursor target.
    auto cursor_el = [&](Element el, std::function<void()> action) -> Element {
      size_t idx = state->cursor_actions.size();
      state->cursor_actions.push_back(NavBarAction{std::move(action)});
      if (navbar_focused && cursor_pos.has_value() && *cursor_pos == idx) {
        return el | inverted;
      }
      return el;
    };

    // Register back button as first cursor element
    auto back_cursor_el = cursor_el(back_btn->Render(), [&nav] -> void { nav.back(); });

    // Default: task link not active; overridden per-view below.
    *show_task_link = false;

    Element breadcrumbs = text("");

    std::visit(overloaded{
                   // ── Board ──────────────────────────────────────────
                   [&](const ally::BoardState&) -> void {
                     stage_container->DetachAllChildren();
                     stage_btns->clear();
                     breadcrumbs = text("Tasks") | bold;
                   },

                   // ── New Task ───────────────────────────────────────
                   [&](const ally::NewTaskState&) -> void {
                     stage_container->DetachAllChildren();
                     stage_btns->clear();
                     breadcrumbs = hbox({
                         cursor_el(tasks_link_btn->Render(), [&nav] -> void { nav.go(ally::BoardState{}); }),
                         sep(),
                         text("New Task") | bold,
                     });
                   },

                   // ── Workflows ──────────────────────────────────────
                   [&](const ally::WorkflowsState&) -> void {
                     stage_container->DetachAllChildren();
                     stage_btns->clear();
                     breadcrumbs = text("Workflows") | bold;
                   },

                   // ── New Workflow ───────────────────────────────────
                   [&](const ally::NewWorkflowState&) -> void {
                     stage_container->DetachAllChildren();
                     stage_btns->clear();
                     breadcrumbs = hbox({
                         cursor_el(workflows_link_btn->Render(), [&nav] -> void { nav.go(ally::WorkflowsState{}); }),
                         sep(),
                         text("New Workflow") | bold,
                     });
                   },

                   // ── Edit Workflow ──────────────────────────────────
                   [&](const ally::EditWorkflowState&) -> void {
                     stage_container->DetachAllChildren();
                     stage_btns->clear();
                     breadcrumbs = hbox({
                         cursor_el(workflows_link_btn->Render(), [&nav] -> void { nav.go(ally::WorkflowsState{}); }),
                         sep(),
                         text("Edit Workflow") | bold,
                     });
                   },

                   // ── Task Detail ────────────────────────────────────
                   [&](const ally::TaskDetailState& nav_state) -> void {
                     *task_link_id = nav_state.task_id;
                     stage_container->DetachAllChildren();
                     stage_btns->clear();

                     std::string task_name = ctx.current_task.has_value() ? ctx.current_task->name : nav_state.task_id;

                     breadcrumbs = hbox({
                         cursor_el(tasks_link_btn->Render(), [&nav] -> void { nav.go(ally::BoardState{}); }),
                         sep(),
                         text(task_name) | bold,
                     });
                   },

                   // ── New Thread ─────────────────────────────────────
                   [&](const ally::NewThreadState& nav_state) -> void {
                     *task_link_id = nav_state.task_id;
                     *show_task_link = true;
                     stage_container->DetachAllChildren();
                     stage_btns->clear();

                     breadcrumbs = hbox({
                         cursor_el(tasks_link_btn->Render(), [&nav] -> void { nav.go(ally::BoardState{}); }),
                         sep(),
                         cursor_el(task_link_btn->Render(), [&nav, id = nav_state.task_id] -> void { nav.go(ally::TaskDetailState{id}); }),
                         sep(),
                         text("New Thread") | bold,
                     });
                   },

                   // ── Stage View ─────────────────────────────────────
                   [&](const ally::StageViewState& nav_state) -> void {
                     *task_link_id = nav_state.task_id;
                     *show_task_link = true;

                     // Resolve thread display name and workflow_id
                     std::string thread_name = nav_state.thread_id;
                     std::string current_wf_id;
                     if (ctx.current_task.has_value()) {
                       for (const auto& thr : ctx.current_task->threads) {
                         if (thr.id == nav_state.thread_id) {
                           thread_name = thr.name.empty() ? thr.id : thr.name;
                           current_wf_id = thr.workflow_id;
                           break;
                         }
                       }
                     }

                     // Fetch workflow stages (cached by workflow_id)
                     if (current_wf_id != state->cached_workflow_id || stage_btns->empty()) {
                       state->workflow_stages.clear();
                       state->cached_workflow_id = current_wf_id;
                       if (!current_wf_id.empty()) {
                         auto workflow = ctx.workflow_service.get_workflow(current_wf_id);
                         if (workflow.has_value()) {
                           state->workflow_stages = workflow->stages;
                         }
                       }
                       // Rebuild stage chip buttons
                       stage_container->DetachAllChildren();
                       stage_btns->clear();
                       for (const auto& stage : state->workflow_stages) {
                         auto btn = Button(
                             stage.name,
                             [&nav, task_id = nav_state.task_id, thread_id = nav_state.thread_id, stage_id = stage.id] -> void {
                               nav.replace(ally::StageViewState{task_id, thread_id, stage_id});
                             },
                             ButtonOption::Ascii());
                         stage_container->Add(btn);
                         stage_btns->push_back(btn);
                       }
                     }

                     // Refresh artifact stages for dimming
                     state->artifact_stages = ctx.artifact_service.list_stage_artifacts(nav_state.task_id, nav_state.thread_id);

                     auto task_seg =
                         cursor_el(task_link_btn->Render(), [&nav, task_id = nav_state.task_id] -> void { nav.go(ally::TaskDetailState{task_id}); });

                     // Thread segment — plain text, no dropdown
                     // TODO: re-implement thread switcher dropdown
                     Element thread_seg = text(thread_name) | bold;

                     // Stage trail
                     Elements stage_els;
                     for (std::size_t i = 0; i < stage_btns->size(); ++i) {
                       bool active = (state->workflow_stages[i].id == nav_state.stage);
                       bool has_artifact = std::find(state->artifact_stages.begin(), state->artifact_stages.end(),
                                                     state->workflow_stages[i].id) != state->artifact_stages.end();
                       Element chip = (*stage_btns)[i]->Render();
                       if (active) {
                         chip = chip | bgcolor(Color::Blue) | color(Color::White);
                       } else if (!has_artifact) {
                         chip = chip | dim;
                       } else {
                         chip = chip | color(Color::GrayDark);
                       }
                       auto stage_id = state->workflow_stages[i].id;
                       chip = cursor_el(chip, [&nav, task_id = nav_state.task_id, thread_id = nav_state.thread_id, sid = stage_id] -> void {
                         nav.replace(ally::StageViewState{task_id, thread_id, sid});
                       });
                       stage_els.push_back(chip);
                     }

                     breadcrumbs = hbox({
                         cursor_el(tasks_link_btn->Render(), [&nav] -> void { nav.go(ally::BoardState{}); }),
                         sep(),
                         task_seg,
                         sep(),
                         thread_seg,
                         sep(),
                         hbox(std::move(stage_els)),
                     });
                   },
               },
               nav.current());

    // Wrap wf button with cursor
    auto wf_el = cursor_el(wf_btn->Render() | color(Color::GrayDark), [&nav] -> void { nav.go(ally::WorkflowsState{}); });

    // Report element count to Navigator
    nav.set_navbar_element_count(state->cursor_actions.size());

    // Back button — dim when no history
    Element back_el = back_cursor_el;
    if (history_len == 0) {
      back_el = back_el | dim;
    }

    return hbox({
        back_el,
        text(" "),
        breadcrumbs,
        filler(),
        wf_el,
    });
  });

  return CatchEvent(renderer_comp, [state, &nav](const Event& event) -> bool {
    if (event == Event::Return && nav.focus_target() == ally::FocusTarget::Navbar) {
      auto cursor = nav.navbar_cursor();
      if (cursor.has_value() && *cursor < state->cursor_actions.size()) {
        state->cursor_actions[*cursor].activate();
      }
      nav.set_focus(ally::FocusTarget::MainView);
      return true;
    }
    return false;
  });
}

}  // namespace ally::components
