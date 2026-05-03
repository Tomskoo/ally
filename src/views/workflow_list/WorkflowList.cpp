#include "WorkflowList.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/style/form/FormStyle.hpp"

using namespace ftxui;
namespace form = ally::style::form;

namespace ally::views {

namespace {

auto render_pipeline(const std::vector<models::WorkflowStage>& stages) -> Element {
  if (stages.empty()) {
    return text("(no stages)") | dim;
  }

  Elements elements;
  for (size_t idx = 0; idx < stages.size(); ++idx) {
    auto name = stages[idx].name.empty() ? "..." : stages[idx].name;
    elements.push_back(text(" " + name + " ") | border);

    if (idx + 1 < stages.size()) {
      elements.push_back(text(" -> ") | dim | vcenter);
    }
  }

  return hbox(std::move(elements));
}

}  // namespace

auto workflow_list(AppContext& ctx, Navigator& nav) -> Component {
  struct WorkflowListState {
    std::vector<models::WorkflowDefinition> workflows;
    std::optional<std::string> confirm_delete;
  };

  auto state = std::make_shared<WorkflowListState>();
  state->workflows = ctx.workflow_service.list_workflows();

  auto container = Container::Vertical({});

  auto rebuild = [&ctx, &nav, state, container] -> void {
    container->DetachAllChildren();

    auto new_btn = Button("+ New Workflow", [&nav] -> void { nav.go(NewWorkflowState{}); }, form::primary_button_option());
    container->Add(new_btn);

    for (size_t idx = 0; idx < state->workflows.size(); ++idx) {
      const auto& workflow = state->workflows[idx];
      auto workflow_id = workflow.id;

      auto edit_btn =
          Button("Edit", [&nav, workflow_id] -> void { nav.go(EditWorkflowState{workflow_id}); }, form::secondary_button_option());

      auto delete_btn =
          Button("Delete", [state, workflow_id] -> void { state->confirm_delete = workflow_id; }, form::secondary_button_option());

      auto confirm_btn = Button(
          "Confirm Delete",
          [&ctx, state, workflow_id] -> void {
            state->confirm_delete = std::nullopt;
            auto result = ctx.workflow_service.delete_workflow(workflow_id);
            if (models::is_ok(result)) {
              auto iter =
                  std::find_if(state->workflows.begin(), state->workflows.end(),
                               [&workflow_id](const models::WorkflowDefinition& def) -> bool { return def.id == workflow_id; });
              if (iter != state->workflows.end()) {
                state->workflows.erase(iter);
              }
            }
          },
          form::primary_button_option());

      auto cancel_delete_btn = Button("Cancel", [state] -> void { state->confirm_delete = std::nullopt; }, form::secondary_button_option());

      auto card_actions = Container::Horizontal({
          edit_btn,
          delete_btn,
          confirm_btn,
          cancel_delete_btn,
      });

      container->Add(card_actions);
    }
  };

  rebuild();

  return Renderer(container, [state, container] -> Element {
    if (state->workflows.empty()) {
      auto header = hbox({
          text("Workflows") | bold,
          filler(),
          container->ChildAt(0)->Render(),
      });
      return vbox({
          header,
          separator(),
          text("No workflows yet. Press 'n' to create a workflow.") | dim | center,
      });
    }

    Elements cards;
    auto header = hbox({
        text("Workflows") | bold,
        filler(),
        container->ChildAt(0)->Render(),
    });
    cards.push_back(header);
    cards.push_back(separator());

    for (size_t idx = 0; idx < state->workflows.size(); ++idx) {
      const auto& workflow = state->workflows[idx];
      auto stage_count = std::to_string(workflow.stages.size()) + " stages";

      auto card_actions = container->ChildAt(static_cast<int>(idx) + 1);

      auto name_row = hbox({
          text(workflow.name) | bold,
          filler(),
          text(stage_count) | dim,
      });

      auto desc_row = text(workflow.description) | dim;
      auto pipeline_row = render_pipeline(workflow.stages);

      Elements action_elements;
      action_elements.push_back(card_actions->ChildAt(0)->Render());
      action_elements.push_back(text(" "));
      action_elements.push_back(card_actions->ChildAt(1)->Render());

      bool is_confirming = state->confirm_delete.has_value() && *state->confirm_delete == workflow.id;
      if (is_confirming) {
        action_elements.push_back(text("  "));
        action_elements.push_back(text("Delete this workflow? ") | color(Color::Red));
        action_elements.push_back(card_actions->ChildAt(2)->Render());
        action_elements.push_back(text(" "));
        action_elements.push_back(card_actions->ChildAt(3)->Render());
      }

      auto action_row = hbox(std::move(action_elements));

      cards.push_back(vbox({
                          name_row,
                          desc_row,
                          pipeline_row,
                          action_row,
                      }) |
                      border);
    }

    return vbox(std::move(cards));
  });
}

}  // namespace ally::views
