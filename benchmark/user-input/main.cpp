#include <chrono>
#include <filesystem>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>

#include "src/storage/Storage.hpp"
#include "src/components/autocomplete/FileAutocomplete.hpp"
#include "src/components/autocomplete/Types.hpp"

using namespace ftxui;

int main() {
  auto screen = ScreenInteractive::Fullscreen();

  // Point at the ally project root for a richer file tree.
  const std::filesystem::path project_root =
      std::getenv("BUILD_WORKSPACE_DIRECTORY") ? std::getenv("BUILD_WORKSPACE_DIRECTORY") : ".";

  auto ac_state = std::make_shared<ally::autocomplete::AutocompleteState>();

  std::string text_content;
  int cursor_pos = 0;
  std::string last_selected;
  double last_filter_ms = 0.0;
  int last_item_count = 0;

  // Callback: insert the @path into text_content.
  // NOTE: This is called from within the CatchEvent handler which already
  // holds ac_state->mutex, so we must NOT lock it again here.
  auto on_insert = [&](const std::string& insertion) {
    if (ac_state->trigger_position.has_value()) {
      int trigger = *ac_state->trigger_position;
      // Replace everything from the '@' to the end of the current
      // input. The query field may have been cleared by directory
      // drill-ins, so we can't rely on it for the splice endpoint.
      // Instead, scan forward from trigger to find the end of the
      // autocomplete token (no spaces after '@').
      int end_pos = trigger + 1;
      while (end_pos < static_cast<int>(text_content.size()) && text_content[end_pos] != ' ' && text_content[end_pos] != '\n') {
        ++end_pos;
      }
      std::string before = text_content.substr(0, trigger);
      std::string after = text_content.substr(end_pos);
      text_content = before + insertion + " " + after;
      // Place cursor right after the inserted text + space.
      cursor_pos = static_cast<int>(before.size() + insertion.size() + 1);
      last_selected = insertion;
    }
  };

  // The autocomplete overlay component.
  auto autocomplete_component = ally::autocomplete::FileAutocompleteComponent(ac_state, project_root, screen, on_insert);

  // Input component.
  auto input_option = InputOption();
  input_option.cursor_position = &cursor_pos;
  input_option.on_change = [&] {
    auto on_open = [&ac_state, &project_root, &screen]() {
      auto state_copy = ac_state;
      auto root_copy = project_root;
      std::thread([state_copy, root_copy, &screen]() {
        ally::storage::list_directory_tree(root_copy, 6,
                                                     [state_copy, &screen](std::vector<ally::autocomplete::DirTreeNode> nodes) {
                                                       std::lock_guard lock(state_copy->mutex);
                                                       state_copy->tree_cache = std::move(nodes);
                                                       screen.PostEvent(Event::Custom);
                                                     });
      }).detach();
    };

    ally::autocomplete::CheckAutocompleteTrigger(*ac_state, text_content, cursor_pos, on_open);

    // Measure filter performance.
    if (ac_state->is_open && ac_state->tree_cache.has_value()) {
      auto start = std::chrono::high_resolution_clock::now();
      std::lock_guard lock(ac_state->mutex);
      auto& tree = *ac_state->tree_cache;
      if (ac_state->current_path.empty() && !ac_state->query.empty()) {
        std::function<void(std::vector<ally::autocomplete::DirTreeNode>&)> count_matches;
        count_matches = [&](std::vector<ally::autocomplete::DirTreeNode>& nodes) {
          for (auto& node : nodes) {
            if (ally::autocomplete::DefaultMatchStrategy(ac_state->query, node.relative_path)) {
              ++last_item_count;
            }
            if (node.is_dir && !node.children.empty()) {
              count_matches(node.children);
            }
          }
        };
        last_item_count = 0;
        count_matches(tree);
      } else {
        last_item_count = 0;
      }
      auto end = std::chrono::high_resolution_clock::now();
      last_filter_ms = std::chrono::duration<double, std::milli>(end - start).count();
    }
  };
  input_option.multiline = false;
  auto input = Input(&text_content, "Type @ to trigger autocomplete...", input_option);

  // Renderer: input is always visible; overlay appears below it.
  auto renderer = Renderer([&]() -> Element {
    auto title = text("File Autocomplete Test") | bold;
    auto exit_hint = text("[Ctrl+C to exit]") | dim;
    auto header = hbox({title, filler(), exit_hint});

    auto input_row = hbox({
        text("> "),
        input->Render() | flex,
    });

    // Status bar.
    std::string overlay_status = ac_state->is_open ? "open" : "closed";
    auto status_line = hbox({
        text("Status: overlay=") | dim,
        text(overlay_status),
        text("  items=") | dim,
        text(std::to_string(last_item_count)),
        text("  query=\"") | dim,
        text(ac_state->query),
        text("\"") | dim,
    });
    auto perf_line = hbox({
        text("Filter time: ") | dim,
        text(std::to_string(last_filter_ms).substr(0, std::to_string(last_filter_ms).find('.') + 3) + "ms"),
        text("  Last select: ") | dim,
        text(last_selected.empty() ? "(none)" : last_selected),
    });

    // Build the main layout: input always visible, overlay below it.
    Elements layout;
    layout.push_back(header);
    layout.push_back(separator());
    layout.push_back(emptyElement() | size(HEIGHT, EQUAL, 1));
    layout.push_back(input_row);

    // Show overlay below the input when open.
    auto overlay_el = autocomplete_component->Render();
    layout.push_back(overlay_el);

    layout.push_back(filler());
    layout.push_back(separator());
    layout.push_back(status_line);
    layout.push_back(perf_line);

    return vbox(std::move(layout)) | border;
  });

  // Route events: when overlay is open, autocomplete handles arrow
  // keys / Enter / Escape before the input component sees them.
  auto main_component = CatchEvent(renderer, [&](Event event) -> bool {
    if (ac_state->is_open) {
      if (autocomplete_component->OnEvent(event)) {
        return true;
      }
    }
    // Forward remaining events to the input.
    return input->OnEvent(event);
  });

  screen.Loop(main_component);
  return 0;
}
