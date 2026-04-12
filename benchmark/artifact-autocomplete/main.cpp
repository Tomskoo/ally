#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <vector>

#include "src/components/autocomplete/ArtifactAutocomplete.hpp"
#include "src/components/autocomplete/ArtifactTypes.hpp"
#include "src/models/Workflow.hpp"
#include "src/services/watcher/ArtifactWatcher.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

using namespace ftxui;

namespace {

// Create sample .ally/tasks/<tid>/threads/<thid>/stages/<stage>/artifact.md
// files for demonstration purposes.
void CreateSampleArtifacts(const std::filesystem::path& root, const std::string& task_id, const std::string& thread_id) {
  namespace fs = std::filesystem;

  std::vector<std::string> stages = {"intent", "design", "implementation"};

  for (const auto& stage : stages) {
    auto stage_dir = root / ".ally" / "tasks" / task_id / "threads" / thread_id / "stages" / stage;
    fs::create_directories(stage_dir);

    auto artifact_path = stage_dir / "artifact.md";
    if (!fs::exists(artifact_path)) {
      std::ofstream out(artifact_path);
      out << "# " << stage << " artifact\n\n";
      out << "Sample artifact content for the " << stage << " stage.\n";
    }
  }
}

auto BuildSampleStages() -> std::vector<ally::models::WorkflowStage> {
  return {
      {"intent", "Intent", "Capture user intent", "Describe your goal", "intent-artifact.md"},
      {"design", "Design", "System design", "Design the solution", "design-artifact.md"},
      {"implementation", "Implementation", "Write code", "Implement the design", std::nullopt},
  };
}

}  // namespace

int main() {
  auto screen = ScreenInteractive::Fullscreen();

  const std::filesystem::path project_root = std::getenv("BUILD_WORKSPACE_DIRECTORY")
                                                 ? std::getenv("BUILD_WORKSPACE_DIRECTORY")
                                                 : std::filesystem::temp_directory_path() / "ally-artifact-bench";

  const std::string task_id = "sample-task";
  const std::string thread_id = "main";

  // Create sample artifacts for the demo.
  CreateSampleArtifacts(project_root, task_id, thread_id);

  auto stages = BuildSampleStages();

  auto artifact_state = std::make_shared<ally::autocomplete::ArtifactAutocompleteState>();

  std::string text_content;
  int cursor_pos = 0;
  std::string last_selected;

  // Load artifacts at startup.
  ally::watcher::WatcherBroadcast<ally::watcher::ArtifactChangedEvent> artifact_broadcast;
  auto artifact_watcher = std::make_unique<ally::watcher::ArtifactWatcher>(project_root, artifact_broadcast);
  ally::autocomplete::SetupArtifactsListener(artifact_state, project_root.string(), task_id, thread_id, stages, artifact_broadcast,
                                             screen);

  // Callback: splice the selected artifact into the text buffer.
  auto on_insert = [&](const std::string& new_text, int trigger) {
    text_content = new_text;
    auto space_pos = new_text.find(' ', trigger);
    if (space_pos != std::string::npos) {
      last_selected = new_text.substr(trigger, space_pos - trigger);
      cursor_pos = static_cast<int>(space_pos + 1);
    } else {
      cursor_pos = static_cast<int>(new_text.size());
    }
  };

  // The autocomplete overlay component.
  auto autocomplete_component =
      ally::autocomplete::ArtifactAutocompleteComponent(artifact_state, screen, [](const std::string&) {});

  // Input component.
  auto input_option = InputOption();
  input_option.cursor_position = &cursor_pos;
  input_option.on_change = [&] {
    std::lock_guard lock(artifact_state->mutex);
    ally::autocomplete::CheckArtifactTrigger(*artifact_state, text_content, cursor_pos);
  };
  input_option.multiline = false;
  auto input = Input(&text_content, "Type $ to trigger artifact autocomplete...", input_option);

  // Renderer: input always visible; overlay appears below it.
  auto renderer = Renderer([&]() -> Element {
    auto title = text("Artifact Autocomplete Test") | bold;
    auto exit_hint = text("[Ctrl+C to exit]") | dim;
    auto header = hbox({title, filler(), exit_hint});

    auto input_row = hbox({
        text("> "),
        input->Render() | flex,
    });

    // Status bar.
    std::string overlay_status = artifact_state->is_open ? "open" : "closed";
    int cache_size = 0;
    if (artifact_state->artifacts_cache.has_value()) {
      cache_size = static_cast<int>(artifact_state->artifacts_cache->size());
    }
    auto status_line = hbox({
        text("Status: overlay=") | dim,
        text(overlay_status),
        text("  artifacts=") | dim,
        text(std::to_string(cache_size)),
        text("  query=\"") | dim,
        text(artifact_state->query),
        text("\"") | dim,
    });
    auto select_line = hbox({
        text("Last select: ") | dim,
        text(last_selected.empty() ? "(none)" : last_selected),
    });

    Elements layout;
    layout.push_back(header);
    layout.push_back(separator());
    layout.push_back(emptyElement() | size(HEIGHT, EQUAL, 1));
    layout.push_back(input_row);

    auto overlay_el = autocomplete_component->Render();
    layout.push_back(overlay_el);

    layout.push_back(filler());
    layout.push_back(separator());
    layout.push_back(status_line);
    layout.push_back(select_line);

    return vbox(std::move(layout)) | border;
  });

  // Route events: when overlay is open, autocomplete gets priority.
  auto main_component = CatchEvent(renderer, [&](Event event) -> bool {
    if (artifact_state->is_open) {
      {
        std::lock_guard lock(artifact_state->mutex);
        auto trigger = artifact_state->trigger_position;
        std::string new_text;
        if (ally::autocomplete::HandleArtifactKeydown(*artifact_state, text_content, new_text, event)) {
          if (!new_text.empty() && trigger.has_value()) {
            on_insert(new_text, *trigger);
          }
          return true;
        }
      }
      if (autocomplete_component->OnEvent(event)) {
        return true;
      }
    }
    return input->OnEvent(event);
  });

  screen.Loop(main_component);
  return 0;
}
