#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>

#include "src/components/autocomplete/SkillAutocomplete.hpp"
#include "src/components/autocomplete/SkillTypes.hpp"
#include "src/services/SkillService.hpp"
#include "src/services/watcher/SkillsWatcher.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

using namespace ftxui;

namespace {

// Create sample .opencode/skills/ directories with SKILL.md files for
// demonstration purposes.
void CreateSampleSkills(const std::filesystem::path& root) {
  namespace fs = std::filesystem;

  struct SampleSkill {
    std::string dir_name;
    std::string name;
    std::string description;
  };

  std::vector<SampleSkill> samples = {
      {"code-review", "Code Review", "Review code for bugs and best practices"},
      {"explain", "Explain Code", "Explain what a piece of code does"},
      {"refactor", "Refactor", "Refactor code for clarity and performance"},
      {"test-gen", "Generate Tests", "Generate unit tests for a function"},
      {"document", "Document", "Add documentation to code"},
  };

  auto skills_dir = root / ".opencode" / "skills";
  fs::create_directories(skills_dir);

  for (const auto& skill : samples) {
    auto skill_dir = skills_dir / skill.dir_name;
    fs::create_directories(skill_dir);

    auto skill_md = skill_dir / "SKILL.md";
    if (!fs::exists(skill_md)) {
      std::ofstream out(skill_md);
      out << "---\n";
      out << "name: " << skill.name << "\n";
      if (!skill.description.empty()) {
        out << "description: " << skill.description << "\n";
      }
      out << "---\n\n";
      out << "# " << skill.name << "\n\n";
      out << "Skill instructions go here.\n";
    }
  }
}

}  // namespace

int main() {
  auto screen = ScreenInteractive::Fullscreen();

  const std::filesystem::path project_root = std::getenv("BUILD_WORKSPACE_DIRECTORY")
                                                 ? std::getenv("BUILD_WORKSPACE_DIRECTORY")
                                                 : std::filesystem::temp_directory_path() / "ally-skill-bench";

  // Create sample skills for the demo.
  CreateSampleSkills(project_root);

  auto skill_state = std::make_shared<ally::autocomplete::SkillAutocompleteState>();

  ally::services::SkillService service(project_root.string());

  std::string text_content;
  int cursor_pos = 0;
  std::string last_selected;

  // Load skills at startup.
  ally::watcher::WatcherBroadcast<ally::watcher::SkillsChangedEvent> skills_broadcast;
  auto skills_watcher = std::make_unique<ally::watcher::SkillsWatcher>(project_root, skills_broadcast);
  ally::autocomplete::SetupSkillsListener(skill_state, service, skills_broadcast, screen);

  // Callback: splice the selected skill into the text buffer.
  // NOTE: This is called from within the CatchEvent handler which already
  // holds skill_state->mutex, so we must NOT lock it again here.
  auto on_insert = [&](const std::string& new_text, int trigger) {
    text_content = new_text;
    // Find the trailing space after the inserted "/skill_name " and
    // place the cursor right after it.
    auto space_pos = new_text.find(' ', trigger);
    if (space_pos != std::string::npos) {
      last_selected = new_text.substr(trigger, space_pos - trigger);
      cursor_pos = static_cast<int>(space_pos + 1);
    } else {
      cursor_pos = static_cast<int>(new_text.size());
    }
  };

  // The autocomplete overlay component.
  // The component's on_insert is not used for keyboard selection (that
  // goes through HandleSkillKeydown in the CatchEvent handler below).
  auto autocomplete_component = ally::autocomplete::SkillAutocompleteComponent(skill_state, screen, [](const std::string&) {});

  // Input component.
  auto input_option = InputOption();
  input_option.cursor_position = &cursor_pos;
  input_option.on_change = [&] {
    std::lock_guard lock(skill_state->mutex);
    ally::autocomplete::CheckSkillTrigger(*skill_state, text_content, cursor_pos);
  };
  input_option.multiline = false;
  auto input = Input(&text_content, "Type / to trigger skill autocomplete...", input_option);

  // Renderer: input always visible; overlay appears below it.
  auto renderer = Renderer([&]() -> Element {
    auto title = text("Skill Autocomplete Test") | bold;
    auto exit_hint = text("[Ctrl+C to exit]") | dim;
    auto header = hbox({title, filler(), exit_hint});

    auto input_row = hbox({
        text("> "),
        input->Render() | flex,
    });

    // Status bar.
    std::string overlay_status = skill_state->is_open ? "open" : "closed";
    int cache_size = 0;
    if (skill_state->skills_cache.has_value()) {
      cache_size = static_cast<int>(skill_state->skills_cache->size());
    }
    auto status_line = hbox({
        text("Status: overlay=") | dim,
        text(overlay_status),
        text("  skills=") | dim,
        text(std::to_string(cache_size)),
        text("  query=\"") | dim,
        text(skill_state->query),
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
  // For keyboard events that need the text buffer (Enter/Tab), we handle
  // them here before delegating to the component.
  auto main_component = CatchEvent(renderer, [&](Event event) -> bool {
    if (skill_state->is_open) {
      // Handle keyboard events that need access to the text buffer.
      {
        std::lock_guard lock(skill_state->mutex);
        // Save trigger_position before HandleSkillKeydown clears it.
        auto trigger = skill_state->trigger_position;
        std::string new_text;
        if (ally::autocomplete::HandleSkillKeydown(*skill_state, text_content, new_text, event)) {
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
