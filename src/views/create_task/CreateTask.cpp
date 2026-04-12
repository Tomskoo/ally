#include "CreateTask.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>

#include "src/style/form/FormStyle.hpp"

using namespace ftxui;
namespace form = ally::style::form;

namespace ally::views {

auto create_task(AppContext& ctx, Navigator& nav) -> Component {
  auto name = std::make_shared<std::string>();
  auto description = std::make_shared<std::string>();
  auto submitting = std::make_shared<bool>(false);
  auto attempted_submit = std::make_shared<bool>(false);

  auto name_input = Input(name.get(), "Enter task name", form::decorated_input_option());
  auto desc_input = Input(description.get(), "Enter description (optional)", form::decorated_input_option());

  auto submit_btn = Button(
      "Create Task",
      [&ctx, &nav, name, description, submitting, attempted_submit] -> void {
        *attempted_submit = true;
        if (*submitting) {
          return;
        }
        auto trimmed_name = form::trim(*name);
        if (trimmed_name.empty()) {
          return;
        }
        *submitting = true;

        models::CreateTaskInput input;
        input.name = trimmed_name;
        auto trimmed_desc = form::trim(*description);
        if (!trimmed_desc.empty()) {
          input.description = trimmed_desc;
        }

        auto result = ctx.task_provider.create_task(input);
        if (result.has_value()) {
          nav.go(BoardState{});
        } else {
          *submitting = false;
        }
      },
      form::primary_button_option());

  auto cancel_btn = Button("Cancel", [&nav] -> void { nav.go(BoardState{}); }, form::secondary_button_option());

  auto layout = Container::Vertical({
      name_input,
      desc_input,
      Container::Horizontal({submit_btn, cancel_btn}),
  });

  return Renderer(layout, [=] -> Element {
    std::string name_error;
    if (*attempted_submit && form::trim(*name).empty()) {
      name_error = "Name is required";
    }

    auto submit_element = submit_btn->Render();
    if (*submitting) {
      submit_element = text(" Creating... ") | dim | border;
    }

    return vbox({
        hbox({text("New Task") | bold, filler()}),
        separator(),
        vbox({
            form::make_field_row("Task Name *", name_input->Render(), name_error),
            text(""),
            form::make_field_row("Description", desc_input->Render()),
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
