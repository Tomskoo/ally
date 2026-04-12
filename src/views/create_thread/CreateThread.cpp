#include "CreateThread.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <vector>

#include "src/models/Task.hpp"
#include "src/models/Workflow.hpp"
#include "src/style/form/FormStyle.hpp"

using namespace ftxui;
namespace form = ally::style::form;

namespace ally::views {

auto create_thread(AppContext& ctx, Navigator& nav, const std::string& task_id) -> Component {
  struct CreateThreadState {
    std::string name;
    std::vector<models::WorkflowDefinition> workflows;
    std::vector<std::string> dropdown_names;
    int dropdown_index = 0;
    bool submitting = false;
    bool attempted_submit = false;
  };

  auto state = std::make_shared<CreateThreadState>();
  state->workflows = ctx.workflow_service.list_workflows();
  for (const auto& wfl : state->workflows) {
    state->dropdown_names.push_back(wfl.name);
  }

  auto name_input = Input(&state->name, "Enter thread name", form::decorated_input_option());
  auto workflow_dropdown = Dropdown(&state->dropdown_names, &state->dropdown_index);

  auto submit_btn = Button(
      "Create Thread",
      [&ctx, &nav, task_id, state] -> void {
        state->attempted_submit = true;
        if (state->submitting) {
          return;
        }
        auto trimmed_name = form::trim(state->name);
        if (trimmed_name.empty()) {
          return;
        }
        if (state->dropdown_names.empty()) {
          return;
        }
        state->submitting = true;

        std::string first_stage;
        const auto& selected_wf = state->workflows[state->dropdown_index];
        if (!selected_wf.stages.empty()) {
          first_stage = selected_wf.stages[0].id;
        }

        models::CreateThreadInput input;
        input.name = trimmed_name;
        input.workflow_id = selected_wf.id;
        input.first_stage = first_stage;

        auto result = ctx.task_provider.create_thread(task_id, input);
        if (result.has_value()) {
          auto refreshed = ctx.task_provider.get_task(task_id);
          if (refreshed.has_value()) {
            ctx.set_current_task(*refreshed);
          }
          nav.go(StageViewState{task_id, result->id, result->current_stage});
        } else {
          state->submitting = false;
        }
      },
      form::primary_button_option());

  auto cancel_btn = Button("Cancel", [&nav, task_id] -> void { nav.go(TaskDetailState{task_id}); }, form::secondary_button_option());

  auto layout = Container::Vertical({
      name_input,
      workflow_dropdown,
      Container::Horizontal({submit_btn, cancel_btn}),
  });

  return Renderer(layout, [=] -> Element {
    std::string name_error;
    if (state->attempted_submit && form::trim(state->name).empty()) {
      name_error = "Name is required";
    }

    std::string workflow_error;
    if (state->attempted_submit && state->dropdown_names.empty()) {
      workflow_error = "No workflows available";
    }

    auto submit_element = submit_btn->Render();
    if (state->submitting) {
      submit_element = text(" Creating... ") | dim | border;
    }

    return vbox({
        hbox({text("New Thread") | bold, filler()}),
        separator(),
        vbox({
            form::make_field_row("Thread Name *", name_input->Render(), name_error),
            text(""),
            form::make_field_row("Workflow *", workflow_dropdown->Render(), workflow_error),
        }) | border,
        separator(),
        hbox({
            submit_element,
            text("  "),
            cancel_btn->Render(),
        }),
    });
  });
}

}  // namespace ally::views
