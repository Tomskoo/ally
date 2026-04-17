#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>

#include "src/components/autocomplete/CommandAutocomplete.hpp"
#include "src/components/autocomplete/CommandTypes.hpp"
#include "src/services/CommandService.hpp"
#include "src/services/watcher/CommandsWatcher.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

using namespace ftxui;

namespace {

// Create sample .opencode/commands/ files for demonstration purposes.
void CreateSampleCommands(const std::filesystem::path& root) {
  namespace fs = std::filesystem;

  struct SampleCommand {
    std::string filename;
    std::string description;
  };

  std::vector<SampleCommand> samples = {
      {"code-review", "Review code for bugs and best practices"},
      {"explain", "Explain what a piece of code does"},
      {"refactor", "Refactor code for clarity and performance"},
      {"test-gen", "Generate unit tests for a function"},
      {"document", "Add documentation to code"},
  };

  auto commands_dir = root / ".opencode" / "commands";
  fs::create_directories(commands_dir);

  for (const auto& cmd : samples) {
    auto cmd_md = commands_dir / (cmd.filename + ".md");
    if (!fs::exists(cmd_md)) {
      std::ofstream out(cmd_md);
      out << "---\n";
      if (!cmd.description.empty()) {
        out << "description: " << cmd.description << "\n";
      }
      out << "---\n\n";
      out << "# " << cmd.filename << "\n\n";
      out << "Command instructions go here.\n";
    }
  }
}

}  // namespace

int main() {
  auto screen = ScreenInteractive::Fullscreen();

  const std::filesystem::path project_root = std::getenv("BUILD_WORKSPACE_DIRECTORY")
                                                 ? std::getenv("BUILD_WORKSPACE_DIRECTORY")
                                                 : std::filesystem::temp_directory_path() / "ally-command-bench";

  // Create sample commands for the demo.
  CreateSampleCommands(project_root);

  auto command_state = std::make_shared<ally::autocomplete::CommandAutocompleteState>();

  ally::services::CommandService service(project_root.string());

  std::string text_content;
  int cursor_pos = 0;
  std::string last_selected;

  // Load commands at startup.
  ally::watcher::WatcherBroadcast<ally::watcher::CommandsChangedEvent> commands_broadcast;
  auto commands_watcher = std::make_unique<ally::watcher::CommandsWatcher>(project_root, commands_broadcast);
  ally::autocomplete::SetupCommandsListener(command_state, service, commands_broadcast, screen);

  // Callback: splice the selected command into the text buffer.
  // NOTE: This is called from within the CatchEvent handler which already
  // holds command_state->mutex, so we must NOT lock it again here.
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
  auto autocomplete_component = ally::autocomplete::CommandAutocompleteComponent(command_state, screen, [](const std::string&) {});

  // Input component.
  auto input_option = InputOption();
  input_option.cursor_position = &cursor_pos;
  input_option.on_change = [&] {
    std::lock_guard lock(command_state->mutex);
    ally::autocomplete::CheckCommandTrigger(*command_state, text_content, cursor_pos);
  };
  input_option.multiline = false;
  auto input = Input(&text_content, "Type / to trigger command autocomplete...", input_option);

  // Renderer: input always visible; overlay appears below it.
  auto renderer = Renderer([&]() -> Element {
    auto title = text("Command Autocomplete Test") | bold;
    auto exit_hint = text("[Ctrl+C to exit]") | dim;
    auto header = hbox({title, filler(), exit_hint});

    auto input_row = hbox({
        text("> "),
        input->Render() | flex,
    });

    // Status bar.
    std::string overlay_status = command_state->is_open ? "open" : "closed";
    int cache_size = 0;
    if (command_state->commands_cache.has_value()) {
      cache_size = static_cast<int>(command_state->commands_cache->size());
    }
    auto status_line = hbox({
        text("Status: overlay=") | dim,
        text(overlay_status),
        text("  commands=") | dim,
        text(std::to_string(cache_size)),
        text("  query=\"") | dim,
        text(command_state->query),
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
    if (command_state->is_open) {
      // Handle keyboard events that need access to the text buffer.
      {
        std::lock_guard lock(command_state->mutex);
        auto trigger = command_state->trigger_position;
        std::string new_text;
        if (ally::autocomplete::HandleCommandKeydown(*command_state, text_content, new_text, event)) {
          if (!new_text.empty() && trigger.has_value()) {
            on_insert(new_text, *trigger);
          }
          return true;
        }
      }
      // Delegate remaining events to the autocomplete component.
      if (autocomplete_component->OnEvent(event)) {
        return true;
      }
    }
    return input->OnEvent(event);
  });

  screen.Loop(main_component);
  return 0;
}
