#include "TaskDetail.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>

#include "src/models/Task.hpp"
#include "src/style/colour/StageColours.hpp"
#include "src/utils/time_format.hpp"

namespace ally::views {

using namespace ftxui;

namespace {

constexpr int kColName = 28;
constexpr int kColStatus = 14;
constexpr int kColStage = 14;
constexpr int kColActivity = 16;
constexpr int kColAction = 5;
constexpr int kLabelWidth = 16;
constexpr int kOverlayWidth = 52;

auto status_label(models::ThreadStatus s) -> std::string {
  switch (s) {
    case models::ThreadStatus::Running:
      return "Running";
    case models::ThreadStatus::Idle:
      return "Idle";
    case models::ThreadStatus::Completed:
      return "Completed";
  }
  return "";
}

auto status_color(models::ThreadStatus s) -> Color {
  switch (s) {
    case models::ThreadStatus::Running:
      return Color::Blue;
    case models::ThreadStatus::Idle:
      return Color::Magenta;
    case models::ThreadStatus::Completed:
      return Color::Green;
  }
  return Color::Default;
}

struct ViewState {
  std::optional<models::Task> task = std::nullopt;
  bool confirm_archive = false;
  std::optional<std::string> confirm_archive_thread = std::nullopt;
};

}  // namespace

auto task_detail(AppContext& ctx, Navigator& nav, const std::string& task_id) -> Component {
  auto state = std::make_shared<ViewState>();
  state->task = ctx.task_provider.get_task(task_id);

  if (state->task.has_value()) {
    ctx.set_current_task(*state->task);
  }

  auto thread_container = Container::Vertical({});

  // Task archive buttons
  auto archive_task_btn = Button("Archive Task", [state] -> void { state->confirm_archive = true; }, ButtonOption::Ascii());

  auto new_thread_btn = Button("+ New Thread", [&nav, task_id] -> void { nav.go(NewThreadState{task_id}); }, ButtonOption::Ascii());

  auto task_cancel_btn = Button("Cancel", [state] -> void { state->confirm_archive = false; }, ButtonOption::Ascii());

  auto task_confirm_btn = Button(
      "Archive",
      [state, &ctx, &nav] -> void {
        if (state->task.has_value()) {
          auto modified = *state->task;
          modified.archived = true;
          ctx.task_provider.update_task(modified);
        }
        nav.go(BoardState{});
      },
      ButtonOption::Ascii());

  // Thread archive buttons
  auto thread_cancel_btn = Button("Cancel", [state] -> void { state->confirm_archive_thread = std::nullopt; }, ButtonOption::Ascii());

  auto rebuild_fn = std::make_shared<std::function<void()>>();

  auto thread_confirm_btn = Button(
      "Archive",
      [state, &ctx, rebuild_fn, task_id] -> void {
        if (state->confirm_archive_thread.has_value()) {
          auto thread_id = *state->confirm_archive_thread;
          if (ctx.task_provider.archive_thread(task_id, thread_id)) {
            state->task = ctx.task_provider.get_task(task_id);
            if (*rebuild_fn) {
              (*rebuild_fn)();
            }
          }
        }
        state->confirm_archive_thread = std::nullopt;
      },
      ButtonOption::Ascii());

  *rebuild_fn = [state, thread_container, &nav, task_id] -> void {
    thread_container->DetachAllChildren();
    if (!state->task.has_value()) {
      return;
    }
    for (const auto& thread : state->task->threads) {
      if (thread.archived) {
        continue;
      }
      auto tid = thread.id;
      auto t_stage = thread.current_stage;
      auto t_name = thread.name;
      auto t_status = thread.status;
      auto t_activity = thread.last_activity;

      ButtonOption row_opt;
      row_opt.transform = [t_name, t_status, t_stage, t_activity](const EntryState& e) -> Element {
        auto stage_color = ally::style::colour::stage_fg_color(t_stage);
        Element stage_el = stage_color.has_value() ? (text(t_stage) | color(*stage_color) | size(WIDTH, EQUAL, kColStage))
                                                   : (text(t_stage) | size(WIDTH, EQUAL, kColStage));

        auto row = hbox({
            text(t_name) | size(WIDTH, EQUAL, kColName),
            text(status_label(t_status)) | color(status_color(t_status)) | size(WIDTH, EQUAL, kColStatus),
            stage_el,
            text(ally::utils::format_relative(t_activity)) | size(WIDTH, EQUAL, kColActivity),
        });
        if (e.focused) {
          return row | inverted;
        }
        return row;
      };

      auto row_btn = Button("", [&nav, task_id, tid, t_stage] -> void { nav.go(StageViewState{task_id, tid, t_stage}); }, row_opt);

      auto arch_btn = Button("[x]", [state, tid] -> void { state->confirm_archive_thread = tid; }, ButtonOption::Ascii());

      thread_container->Add(Container::Horizontal({row_btn, arch_btn}));
    }
  };

  (*rebuild_fn)();

  auto active_overlay = std::make_shared<int>(0);

  auto task_overlay_btns = Container::Horizontal({task_cancel_btn, task_confirm_btn});
  auto thread_overlay_btns = Container::Horizontal({thread_cancel_btn, thread_confirm_btn});
  auto main_controls = Container::Vertical({
      Container::Horizontal({archive_task_btn, new_thread_btn}),
      thread_container,
  });

  auto top = Container::Tab({main_controls, task_overlay_btns, thread_overlay_btns}, active_overlay.get());

  auto view = Renderer(top, [=] -> Element {
    // Sync overlay index from state
    if (state->confirm_archive) {
      *active_overlay = 1;
    } else if (state->confirm_archive_thread.has_value()) {
      *active_overlay = 2;
    } else {
      *active_overlay = 0;
    }

    if (*active_overlay == 1) {
      return vbox({
                 text("Archive this task?") | bold,
                 separator(),
                 text("The task will be hidden from the board but kept on disk."),
                 separator(),
                 hbox({
                     task_cancel_btn->Render(),
                     text(" "),
                     task_confirm_btn->Render() | color(Color::Red),
                 }),
             }) |
             border | size(WIDTH, LESS_THAN, kOverlayWidth) | center;
    }

    if (*active_overlay == 2) {
      return vbox({
                 text("Archive this thread?") | bold,
                 separator(),
                 text("The thread will be hidden from this view but kept on disk."),
                 separator(),
                 hbox({
                     thread_cancel_btn->Render(),
                     text(" "),
                     thread_confirm_btn->Render() | color(Color::Red),
                 }),
             }) |
             border | size(WIDTH, LESS_THAN, kOverlayWidth) | center;
    }

    if (!state->task.has_value()) {
      return vbox({text("Loading...")}) | flex;
    }

    const auto& task = *state->task;

    // Summary card
    Elements meta_rows;
    if (task.description.has_value()) {
      meta_rows.push_back(hbox({
          text("Description") | size(WIDTH, EQUAL, kLabelWidth) | bold,
          text(*task.description) | flex,
      }));
    }
    meta_rows.push_back(hbox({
        text("Created") | size(WIDTH, EQUAL, kLabelWidth) | bold,
        text(ally::utils::format_date(task.created_at)),
    }));
    meta_rows.push_back(hbox({
        text("Last Activity") | size(WIDTH, EQUAL, kLabelWidth) | bold,
        text(ally::utils::format_relative(task.last_activity)),
    }));
    auto summary_card = vbox(std::move(meta_rows)) | border;

    // Thread table
    auto table_header = hbox({
        text("Name") | size(WIDTH, EQUAL, kColName) | bold,
        text("Status") | size(WIDTH, EQUAL, kColStatus) | bold,
        text("Stage") | size(WIDTH, EQUAL, kColStage) | bold,
        text("Last Activity") | size(WIDTH, EQUAL, kColActivity) | bold,
        text("") | size(WIDTH, EQUAL, kColAction),
    });

    return vbox({
        text(task.name) | bold,
        separator(),
        summary_card,
        separator(),
        hbox({
            archive_task_btn->Render(),
            text("  "),
            new_thread_btn->Render(),
        }),
        separator(),
        hbox({
            text("Threads") | bold,
        }),
        separator(),
        table_header,
        separator(),
        thread_container->Render(),
    });
  });

  return CatchEvent(view, [state](const Event& event) -> bool {
    if (event == Event::Escape) {
      if (state->confirm_archive) {
        state->confirm_archive = false;
        return true;
      }
      if (state->confirm_archive_thread.has_value()) {
        state->confirm_archive_thread = std::nullopt;
        return true;
      }
    }
    return false;
  });
}

}  // namespace ally::views
