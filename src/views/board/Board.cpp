#include "Board.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <vector>

#include "src/style/colour/StageColours.hpp"
#include "src/utils/time_format.hpp"

using namespace ftxui;

namespace ally::views {

namespace {

constexpr int kStageWidth = 14;
constexpr int kDateWidth = 14;
constexpr int kActivityWidth = 14;

}  // namespace

auto task_board(AppContext& ctx, Navigator& nav) -> Component {
  auto tasks = std::make_shared<std::vector<models::Task>>(ctx.task_provider.list_tasks());

  auto entries = std::make_shared<std::vector<std::string>>();
  entries->reserve(tasks->size());
  for (const auto& task : *tasks) {
    entries->push_back(task.name);
  }

  auto selected = std::make_shared<int>(0);

  MenuEntryOption entry_option;
  entry_option.transform = [tasks](const EntryState& state) -> Element {
    if (state.index < 0 || state.index >= static_cast<int>(tasks->size())) {
      return text("");
    }
    const auto& task = (*tasks)[state.index];
    auto row = hbox({
        text(task.name) | flex,
        text("  "),
        ally::style::colour::render_stage_badge(task.stage) | size(WIDTH, EQUAL, kStageWidth),
        text("  "),
        text(ally::utils::format_date(task.created_at)) | size(WIDTH, EQUAL, kDateWidth),
        text("  "),
        text(ally::utils::format_relative(task.last_activity)) | size(WIDTH, EQUAL, kActivityWidth),
    });
    if (state.focused) {
      row = row | inverted;
    }
    return row;
  };

  auto menu = Menu({
      .entries = entries.get(),
      .selected = selected.get(),
      .entries_option = entry_option,
      .on_change = [] -> void {},
      .on_enter =
          [tasks, selected, &nav] -> void {
            if (*selected >= 0 && *selected < static_cast<int>(tasks->size())) {
              nav.go(TaskDetailState{(*tasks)[*selected].id});
            }
          },
  });

  auto new_task_btn = Button("+ New Task", [&nav] -> void { nav.go(NewTaskState{}); }, ButtonOption::Ascii());

  auto layout = Container::Vertical({
      new_task_btn,
      menu,
  });

  return Renderer(layout, [&ctx, new_task_btn, menu, tasks, entries, selected] -> Element {
    auto header = hbox({
        text("Tasks") | bold,
        filler(),
        new_task_btn->Render(),
    });

    auto table_header = hbox({
        text("Task Name") | bold | flex,
        text("  "),
        text("Stage") | size(WIDTH, EQUAL, kStageWidth) | bold,
        text("  "),
        text("Created On") | size(WIDTH, EQUAL, kDateWidth) | bold,
        text("  "),
        text("Last Activity") | size(WIDTH, EQUAL, kActivityWidth) | bold,
    });

    if (tasks->empty()) {
      Elements empty_content;
      if (ctx.is_fresh_workspace) {
        empty_content.push_back(text("Welcome to ally!") | bold | center);
        empty_content.push_back(text(""));
        empty_content.push_back(text("Your workspace has been initialized at .ally/") | dim | center);
        empty_content.push_back(text("Press 'n' to create a task, or '?' for help") | dim | center);
      } else {
        empty_content.push_back(text("No tasks yet.") | dim | center);
        empty_content.push_back(text(""));
        empty_content.push_back(text("Press 'n' to create your first task") | dim | center);
      }
      return vbox({
          header,
          separator(),
          table_header,
          separator(),
          vbox(std::move(empty_content)),
      });
    }

    return vbox({
        header,
        separator(),
        table_header,
        separator(),
        menu->Render() | flex,
    });
  });
}

}  // namespace ally::views
