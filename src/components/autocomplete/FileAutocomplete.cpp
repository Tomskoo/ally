#include "src/components/autocomplete/FileAutocomplete.hpp"

#include <algorithm>
#include <cctype>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <sstream>
#include <thread>

#include "src/commands/storage/Storage.hpp"

namespace ally::autocomplete {

// ---------------------------------------------------------------------------
// DefaultMatchStrategy
// ---------------------------------------------------------------------------

auto DefaultMatchStrategy(std::string_view query, std::string_view target) -> bool {
  if (query.empty()) {
    return true;
  }
  size_t query_idx = 0;
  for (char chr : target) {
    if (std::tolower(static_cast<unsigned char>(chr)) == std::tolower(static_cast<unsigned char>(query[query_idx]))) {
      ++query_idx;
      if (query_idx == query.size()) {
        return true;
      }
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// CheckAutocompleteTrigger
// ---------------------------------------------------------------------------

void CheckAutocompleteTrigger(AutocompleteState& state, const std::string& text, int cursor_pos,
                              const std::function<void()>& on_open_callback) {
  // Clamp cursor_pos to text length.
  cursor_pos = std::max(cursor_pos, 0);
  cursor_pos = std::min(cursor_pos, static_cast<int>(text.size()));

  // Scan backwards for '@'.
  int at_pos = -1;
  for (int idx = cursor_pos - 1; idx >= 0; --idx) {
    if (text[idx] == '@') {
      at_pos = idx;
      break;
    }
    // Stop scanning at whitespace — the '@' must be reachable without
    // crossing a space/newline boundary in the query portion.
  }

  if (at_pos < 0) {
    // No '@' found.
    if (state.is_open) {
      state.is_open = false;
      state.trigger_position = std::nullopt;
    }
    return;
  }

  // Validate: '@' must be at position 0 or preceded by whitespace.
  if (at_pos > 0) {
    char prev = text[at_pos - 1];
    if (prev != ' ' && prev != '\t' && prev != '\n') {
      if (state.is_open) {
        state.is_open = false;
        state.trigger_position = std::nullopt;
      }
      return;
    }
  }

  // Extract query between '@' + 1 and cursor.
  std::string query = text.substr(at_pos + 1, cursor_pos - at_pos - 1);

  // If query contains space or newline, close.
  if (query.find(' ') != std::string::npos || query.find('\n') != std::string::npos) {
    state.is_open = false;
    state.trigger_position = std::nullopt;
    return;
  }

  bool was_open = state.is_open;

  state.trigger_position = at_pos;
  state.query = query;
  state.selected_index = 0;
  state.is_open = true;

  if (!was_open) {
    state.current_path.clear();
    if (on_open_callback) {
      on_open_callback();
    }
  }
}

// ---------------------------------------------------------------------------
// Tree traversal helpers
// ---------------------------------------------------------------------------

namespace {

void FlattenTreeImpl(std::vector<DirTreeNode>& nodes, std::string_view query, std::vector<DirTreeNode*>& out) {
  for (auto& node : nodes) {
    // Match against relative_path so queries like "chat.md" find
    // deeply nested files (e.g. "docs/rust/implemented/chat.md").
    if (DefaultMatchStrategy(query, node.relative_path)) {
      out.push_back(&node);
    }
    if (node.is_dir && !node.children.empty()) {
      FlattenTreeImpl(node.children, query, out);
    }
  }
}

auto FlattenTree(std::vector<DirTreeNode>& tree, std::string_view query) -> std::vector<DirTreeNode*> {
  std::vector<DirTreeNode*> result;
  FlattenTreeImpl(tree, query, result);
  return result;
}

auto LowercaseName(const std::string& name) -> std::string {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char chr) -> int { return std::tolower(chr); });
  return lower;
}

auto GetChildrenAtPath(std::vector<DirTreeNode>& tree, const std::vector<std::string>& path_segments, std::string_view query)
    -> std::vector<DirTreeNode*> {
  // Navigate to the target node by following path segments.
  std::vector<DirTreeNode>* current = &tree;
  for (const auto& segment : path_segments) {
    bool found = false;
    for (auto& node : *current) {
      if (node.name == segment && node.is_dir) {
        current = &node.children;
        found = true;
        break;
      }
    }
    if (!found) {
      return {};
    }
  }

  // Filter children by query and sort dirs-first, then alphabetically.
  std::vector<DirTreeNode*> result;
  for (auto& node : *current) {
    if (DefaultMatchStrategy(query, node.name)) {
      result.push_back(&node);
    }
  }

  std::sort(result.begin(), result.end(), [](const DirTreeNode* lhs, const DirTreeNode* rhs) -> bool {
    if (lhs->is_dir != rhs->is_dir) {
      return static_cast<int>(lhs->is_dir) > static_cast<int>(rhs->is_dir);
    }
    return LowercaseName(lhs->name) < LowercaseName(rhs->name);
  });

  return result;
}

void InsertEntriesAtPath(std::vector<DirTreeNode>& tree, const std::vector<std::string>& path_segments,
                         const std::vector<ally::commands::storage::DirEntry>& entries) {
  std::vector<DirTreeNode>* current = &tree;
  for (const auto& segment : path_segments) {
    bool found = false;
    for (auto& node : *current) {
      if (node.name == segment && node.is_dir) {
        current = &node.children;
        found = true;
        break;
      }
    }
    if (!found) {
      return;
    }
  }

  // Replace the target's children with the new entries.
  current->clear();
  for (const auto& entry : entries) {
    DirTreeNode node;
    node.name = entry.name;
    node.relative_path = entry.relative_path;
    node.is_dir = entry.is_dir;
    // children left empty — eligible for lazy expansion later.
    current->push_back(std::move(node));
  }
}

auto SplitPath(const std::string& path) -> std::vector<std::string> {
  std::vector<std::string> segments;
  std::istringstream stream(path);
  std::string segment;
  while (std::getline(stream, segment, '/')) {
    if (!segment.empty()) {
      segments.push_back(segment);
    }
  }
  return segments;
}

auto JoinPath(const std::vector<std::string>& segments) -> std::string {
  std::string result;
  for (size_t idx = 0; idx < segments.size(); ++idx) {
    if (idx > 0) {
      result += "/";
    }
    result += segments[idx];
  }
  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// SelectCurrentItem
// ---------------------------------------------------------------------------

void SelectCurrentItem(AutocompleteState& state, const std::vector<DirTreeNode*>& items,
                       const std::function<void(const std::string& relative_path)>& on_select_file,
                       const std::function<void(const std::string& relative_path)>& on_lazy_load) {
  if (items.empty() || state.selected_index < 0 || state.selected_index >= static_cast<int>(items.size())) {
    return;
  }

  const auto* item = items[state.selected_index];

  if (item->is_dir) {
    // Drill into directory.
    state.current_path = SplitPath(item->relative_path);
    state.query.clear();
    state.selected_index = 0;

    // Check if children need lazy loading.
    if (item->children.empty() && on_lazy_load) {
      on_lazy_load(item->relative_path);
    }
  } else {
    // Select file.
    if (on_select_file) {
      on_select_file(item->relative_path);
    }
    state.is_open = false;
    state.trigger_position = std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// HandleAutocompleteKeydown
// ---------------------------------------------------------------------------

auto HandleAutocompleteKeydown(AutocompleteState& state, const std::vector<DirTreeNode*>& items, const ftxui::Event& event,
                               const std::function<void()>& on_select) -> bool {
  if (!state.is_open) {
    return false;
  }

  int breadcrumb_offset = state.current_path.empty() ? 0 : 1;
  int total_rows = static_cast<int>(items.size()) + breadcrumb_offset;

  if (event == ftxui::Event::ArrowDown) {
    if (total_rows > 0) {
      state.selected_index = (state.selected_index + 1) % total_rows;
    }
    return true;
  }

  if (event == ftxui::Event::ArrowUp) {
    if (total_rows > 0) {
      state.selected_index = (state.selected_index - 1 + total_rows) % total_rows;
    }
    return true;
  }

  if (event == ftxui::Event::Escape) {
    state.is_open = false;
    state.trigger_position = std::nullopt;
    return true;
  }

  if (event == ftxui::Event::Return || event == ftxui::Event::Tab) {
    // Check if breadcrumb row is selected (index 0 when current_path
    // is non-empty means the breadcrumb).
    if (!state.current_path.empty() && state.selected_index == 0) {
      // Back-navigation: pop last path segment.
      state.current_path.pop_back();
      state.query.clear();
      state.selected_index = 0;
      return true;
    }
    if (on_select) {
      on_select();
    }
    return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// FileAutocompleteComponent
// ---------------------------------------------------------------------------

auto FileAutocompleteComponent(std::shared_ptr<AutocompleteState> state, std::filesystem::path project_root,
                               ftxui::ScreenInteractive& screen, std::function<void(const std::string& insertion)> on_insert)
    -> ftxui::Component {
  using namespace ftxui;

  auto root = std::make_shared<std::filesystem::path>(std::move(project_root));

  auto component = Renderer([state, root, on_insert]() -> Element {
    std::scoped_lock lock(state->mutex);

    if (!state->is_open || !state->tree_cache.has_value()) {
      return emptyElement();
    }

    auto& tree = *state->tree_cache;

    // Build the filtered item list.
    std::vector<DirTreeNode*> items;
    bool has_breadcrumb = !state->current_path.empty();

    if (state->current_path.empty() && !state->query.empty()) {
      items = FlattenTree(tree, state->query);
    } else {
      items = GetChildrenAtPath(tree, state->current_path, state->query);
    }

    // Clamp selected_index.
    int total_rows = static_cast<int>(items.size()) + (has_breadcrumb ? 1 : 0);
    if (total_rows > 0) {
      state->selected_index = std::max(0, std::min(state->selected_index, total_rows - 1));
    } else {
      state->selected_index = 0;
    }

    // Build element rows.
    Elements rows;

    // Breadcrumb row.
    if (has_breadcrumb) {
      auto breadcrumb_text = "< " + JoinPath(state->current_path);
      bool selected = (state->selected_index == 0);
      auto row = text(breadcrumb_text) | bold;
      if (selected) {
        row = row | inverted;
      }
      rows.push_back(row);
    }

    if (items.empty()) {
      rows.push_back(text("No matches") | dim | center);
    } else {
      int offset = has_breadcrumb ? 1 : 0;
      for (int idx = 0; idx < static_cast<int>(items.size()); ++idx) {
        const auto* item = items[idx];
        bool selected = (idx + offset == state->selected_index);

        auto icon = item->is_dir ? text("/ ") : text("  ");
        auto name_el = text(item->relative_path) | flex;
        auto chevron = item->is_dir ? text(" >") : text("  ");

        auto row = hbox({icon, name_el, chevron});
        if (selected) {
          row = row | inverted | focus;
        }
        rows.push_back(row);
      }
    }

    return vbox(std::move(rows)) | vscroll_indicator | yframe | xflex | size(HEIGHT, LESS_THAN, 10) | border;
  });

  // Wrap with CatchEvent for keyboard/mouse handling.
  component = CatchEvent(component, [state, root, &screen, on_insert](const Event& event) -> bool {
    std::scoped_lock lock(state->mutex);

    if (!state->is_open || !state->tree_cache.has_value()) {
      return false;
    }

    auto& tree = *state->tree_cache;
    bool has_breadcrumb = !state->current_path.empty();

    // Build items for the handler.
    std::vector<DirTreeNode*> items;
    if (state->current_path.empty() && !state->query.empty()) {
      items = FlattenTree(tree, state->query);
    } else {
      items = GetChildrenAtPath(tree, state->current_path, state->query);
    }

    // For HandleAutocompleteKeydown, the items list includes the
    // breadcrumb conceptually at index 0 when has_breadcrumb is true.
    // The handler accounts for this internally.
    auto on_select = [&state, &items, &root, &screen, &on_insert, has_breadcrumb]() -> void {
      // Adjust index for breadcrumb offset.
      int item_idx = has_breadcrumb ? state->selected_index - 1 : state->selected_index;

      if (item_idx < 0 || item_idx >= static_cast<int>(items.size())) {
        return;
      }

      // Build a temporary items view for SelectCurrentItem.
      auto* selected_item = items[item_idx];
      std::vector<DirTreeNode*> single = {selected_item};

      state->selected_index = 0;  // point at the single item

      SelectCurrentItem(
          *state, single,
          [&on_insert](const std::string& path) -> void {
            if (on_insert) {
              on_insert("@" + path);
            }
          },
          [&root, &state, &screen](const std::string& path) -> void {
            // Lazy load: fire background thread.
            const auto& state_copy = state;
            const auto& root_copy = root;
            std::thread([state_copy, root_copy, path, &screen]() -> void {
              ally::commands::storage::list_directory_entries(
                  *root_copy, path, [state_copy, &screen, path](const std::vector<ally::commands::storage::DirEntry>& entries) -> void {
                    std::scoped_lock lock(state_copy->mutex);
                    if (state_copy->tree_cache.has_value()) {
                      auto segments = SplitPath(path);
                      InsertEntriesAtPath(*state_copy->tree_cache, segments, entries);
                    }
                    screen.PostEvent(Event::Custom);
                  });
            }).detach();
          });

      // If the item was a directory, selected_index was reset by
      // SelectCurrentItem. If it was a file, overlay is now closed.
      if (state->is_open && selected_item->is_dir) {
        state->selected_index = 0;
      }
    };

    return HandleAutocompleteKeydown(*state, items, event, on_select);
  });

  return component;
}

}  // namespace ally::autocomplete
