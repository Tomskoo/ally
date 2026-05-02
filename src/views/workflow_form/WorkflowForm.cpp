#include "WorkflowForm.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/storage/Storage.hpp"
#include "src/style/form/FormStyle.hpp"

using namespace ftxui;
namespace form = ally::style::form;

namespace ally::views {

namespace {

auto slugify_frontend(const std::string& name) -> std::string { return storage::slugify(name); }

auto default_product(const std::string& name) -> std::string {
  auto slug = slugify_frontend(name);
  if (slug.empty()) {
    return "";
  }
  return slug + "-artifact.md";
}

struct StageFormData {
  std::string form_id;
  std::optional<std::string> existing_id;
  std::string name;
  std::string description;
  std::string starting_prompt;
  std::string product;
  bool product_manually_set = false;

  static auto new_blank() -> StageFormData {
    static int counter = 0;
    return StageFormData{
        .form_id = "stage-" + std::to_string(++counter),
        .existing_id = std::nullopt,
        .name = "",
        .description = "",
        .starting_prompt = "",
        .product = "",
        .product_manually_set = false,
    };
  }

  static auto from_stage(const models::WorkflowStage& stage) -> StageFormData {
    static int counter = 0;
    auto data = StageFormData{
        .form_id = "stage-" + std::to_string(++counter),
        .existing_id = stage.id,
        .name = stage.name,
        .description = stage.description,
        .starting_prompt = stage.starting_prompt,
        .product = stage.product.value_or(""),
        .product_manually_set = stage.product.has_value() && !stage.product->empty(),
    };
    return data;
  }
};

struct StageComponents {
  Component name_input;
  Component desc_input;
  Component product_input;
  Component prompt_input;
  Component remove_btn;
};

struct WorkflowFormState {
  std::string workflow_name;
  std::string workflow_desc;
  std::vector<StageFormData> stages;
  std::vector<StageComponents> stage_components;
  bool submitting = false;
  bool loading = false;
};

auto render_pipeline_preview(const std::vector<StageFormData>& stages) -> Element {
  if (stages.empty()) {
    return text("(no stages)") | dim;
  }

  Elements elements;
  for (size_t idx = 0; idx < stages.size(); ++idx) {
    auto name = stages[idx].name.empty() ? std::string("\xe2\x80\xa6") : stages[idx].name;
    elements.push_back(text(" " + name + " ") | border);

    if (idx + 1 < stages.size()) {
      auto product_label = stages[idx].product;
      if (product_label.empty()) {
        elements.push_back(text(" -> ") | dim | vcenter);
      } else {
        elements.push_back(vbox({
                               text(product_label) | dim | center,
                               text(" -> ") | dim,
                           }) |
                           vcenter);
      }
    }
  }

  return hbox(std::move(elements));
}

// Wraps a component so that Tab/ShiftTab are not consumed by the child
// (e.g. a multiline Input that would otherwise insert a tab character).
// Returning false lets the parent Container handle focus navigation.
class TabNavigable : public ComponentBase {
 public:
  explicit TabNavigable(Component child) { Add(std::move(child)); }
  auto OnEvent(Event event) -> bool override {
    if (event == Event::Tab || event == Event::TabReverse) {
      return false;
    }
    if (ChildCount() > 0) {
      return ChildAt(0)->OnEvent(event);
    }
    return false;
  }
};

auto tab_navigable(Component child) -> Component { return Make<TabNavigable>(std::move(child)); }

auto decorated_multiline_option() -> InputOption {
  InputOption opt;
  opt.multiline = true;
  opt.transform = [](const InputState& state) -> Element {
    auto elem = state.element;
    if (state.focused) {
      elem = elem | border | color(Color::Cyan);
    } else {
      elem = elem | border | dim;
    }
    return elem;
  };
  return opt;
}

}  // namespace

auto workflow_form(AppContext& ctx, Navigator& nav, std::optional<std::string> workflow_id) -> Component {
  auto state = std::make_shared<WorkflowFormState>();

  if (workflow_id.has_value()) {
    auto existing = ctx.workflow_service.get_workflow(*workflow_id);
    if (existing.has_value()) {
      state->workflow_name = existing->name;
      state->workflow_desc = existing->description;
      for (const auto& stage : existing->stages) {
        state->stages.push_back(StageFormData::from_stage(stage));
      }
    }
  }

  if (state->stages.empty()) {
    state->stages.push_back(StageFormData::new_blank());
  }

  // Single flat container controls the tab order:
  //   workflow name -> workflow desc -> [stage name -> stage desc ->
  //   starting prompt ...per stage] -> add stage -> save
  // Stage product, input (read-only), and remove are rendered but NOT
  // in the focus tree — they are only reachable via mouse click.
  auto tab_container = Container::Vertical({});

  auto name_input = Input(&state->workflow_name, "Workflow name", form::decorated_input_option());
  auto desc_input = Input(&state->workflow_desc, "Description", form::decorated_input_option());

  auto rebuild_stages = std::make_shared<std::function<void()>>();

  auto add_stage_btn = Button(
      "+ Add Stage",
      [state, rebuild_stages] -> void {
        state->stages.push_back(StageFormData::new_blank());
        (*rebuild_stages)();
      },
      form::secondary_button_option());

  auto submit_btn = Button(
      "Save",
      [&ctx, &nav, state, workflow_id] -> void {
        if (state->submitting) {
          return;
        }
        state->submitting = true;

        if (workflow_id.has_value()) {
          models::UpdateWorkflowInput input;
          input.id = *workflow_id;
          input.name = state->workflow_name;
          input.description = state->workflow_desc;
          for (size_t idx = 0; idx < state->stages.size(); ++idx) {
            auto& stage = state->stages[idx];
            models::WorkflowStageUpdateInput stage_input;
            stage_input.existing_id = stage.existing_id;
            stage_input.name = stage.name;
            stage_input.description = stage.description;
            stage_input.starting_prompt = stage.starting_prompt;
            if (idx + 1 < state->stages.size() && !stage.product.empty()) {
              stage_input.product = stage.product;
            }
            input.stages.push_back(std::move(stage_input));
          }
          auto result = ctx.workflow_service.update_workflow(input);
          if (models::is_ok(result)) {
            nav.go(WorkflowsState{});
          } else {
            state->submitting = false;
          }
        } else {
          models::CreateWorkflowInput input;
          input.name = state->workflow_name;
          input.description = state->workflow_desc;
          for (size_t idx = 0; idx < state->stages.size(); ++idx) {
            auto& stage = state->stages[idx];
            models::WorkflowStageCreateInput stage_input;
            stage_input.name = stage.name;
            stage_input.description = stage.description;
            stage_input.starting_prompt = stage.starting_prompt;
            if (idx + 1 < state->stages.size() && !stage.product.empty()) {
              stage_input.product = stage.product;
            }
            input.stages.push_back(std::move(stage_input));
          }
          auto result = ctx.workflow_service.create_workflow(input);
          if (models::is_ok(result)) {
            nav.go(WorkflowsState{});
          } else {
            state->submitting = false;
          }
        }
      },
      form::primary_button_option());

  auto cancel_btn = Button("Cancel", [&nav] -> void { nav.go(WorkflowsState{}); }, form::secondary_button_option());

  *rebuild_stages = [state, tab_container, name_input, desc_input, add_stage_btn, submit_btn, cancel_btn, rebuild_stages] -> void {
    tab_container->DetachAllChildren();
    state->stage_components.clear();

    // Fixed tab stops at the top
    tab_container->Add(name_input);
    tab_container->Add(desc_input);

    // Per-stage tab stops
    for (size_t idx = 0; idx < state->stages.size(); ++idx) {
      auto& stage = state->stages[idx];

      if (!stage.product_manually_set) {
        stage.product = default_product(stage.name);
      }

      auto stage_name_input = Input(&stage.name, "Stage name", form::decorated_input_option());
      auto stage_desc_input = Input(&stage.description, "Stage description", form::decorated_input_option());

      InputOption product_option = form::decorated_input_option();
      product_option.on_change = [state, idx] -> void { state->stages[idx].product_manually_set = true; };
      auto product_input = Input(&stage.product, "Product filename", product_option);

      auto prompt_input = tab_navigable(Input(&stage.starting_prompt, "Starting prompt", decorated_multiline_option()));

      auto remove_btn = Button(
          "x",
          [state, idx, rebuild_stages] -> void {
            if (state->stages.size() > 1) {
              state->stages.erase(state->stages.begin() + static_cast<ptrdiff_t>(idx));
              (*rebuild_stages)();
            }
          },
          form::secondary_button_option());

      state->stage_components.push_back(StageComponents{
          .name_input = stage_name_input,
          .desc_input = stage_desc_input,
          .product_input = product_input,
          .prompt_input = prompt_input,
          .remove_btn = remove_btn,
      });

      tab_container->Add(remove_btn);
      tab_container->Add(stage_name_input);
      tab_container->Add(stage_desc_input);
      tab_container->Add(prompt_input);
    }

    // Fixed tab stops at the bottom
    tab_container->Add(add_stage_btn);
    tab_container->Add(submit_btn);
    tab_container->Add(cancel_btn);
  };

  (*rebuild_stages)();

  return Renderer(tab_container, [state, tab_container, name_input, desc_input, add_stage_btn, submit_btn, cancel_btn] -> Element {
    auto is_edit = state->stages.front().existing_id.has_value();
    auto title = text(is_edit ? "Edit Workflow" : "New Workflow") | bold;

    Elements form_elements;
    form_elements.push_back(hbox({title, filler()}));
    form_elements.push_back(separator());

    // Workflow metadata card
    form_elements.push_back(vbox({
                                form::make_field_row("Name", name_input->Render()),
                                text(""),
                                form::make_field_row("Description", desc_input->Render()),
                            }) |
                            border);

    form_elements.push_back(separator());

    form_elements.push_back(text("Pipeline Preview") | bold);
    form_elements.push_back(render_pipeline_preview(state->stages));
    form_elements.push_back(separator());

    form_elements.push_back(hbox({
        text("Stages") | bold,
        filler(),
        add_stage_btn->Render(),
    }));
    form_elements.push_back(separator());

    for (size_t idx = 0; idx < state->stages.size(); ++idx) {
      auto& stage = state->stages[idx];
      auto& comps = state->stage_components[idx];

      if (!stage.product_manually_set) {
        stage.product = default_product(stage.name);
      }

      Elements card_elements;

      auto stage_header = hbox({
          text("Stage " + std::to_string(idx + 1)) | bold,
          filler(),
          comps.remove_btn->Render(),
      });
      card_elements.push_back(stage_header);
      card_elements.push_back(separator());

      card_elements.push_back(form::make_field_row("Name", comps.name_input->Render()));

      if (idx > 0) {
        auto prev_product = state->stages[idx - 1].product;
        auto input_label = prev_product.empty() ? "(none)" : prev_product;
        card_elements.push_back(form::make_field_row("Input", text(input_label) | dim));
      }

      card_elements.push_back(form::make_field_row("Description", comps.desc_input->Render()));

      if (idx + 1 < state->stages.size()) {
        card_elements.push_back(form::make_field_row("Product", comps.product_input->Render()));
      }

      card_elements.push_back(form::make_field_row("Starting Prompt", comps.prompt_input->Render()));

      form_elements.push_back(vbox(std::move(card_elements)) | border);
    }

    form_elements.push_back(separator());

    auto submit_element = submit_btn->Render();
    if (state->submitting) {
      submit_element = text(" Saving... ") | dim | border;
    }

    form_elements.push_back(hbox({
        submit_element,
        text("  "),
        cancel_btn->Render(),
    }));

    return vbox(std::move(form_elements));
  });
}

}  // namespace ally::views
