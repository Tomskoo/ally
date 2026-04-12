#pragma once

#include <filesystem>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/components/autocomplete/Types.hpp"

namespace ally::autocomplete {

/// Case-insensitive subsequence match. An empty query matches everything.
/// Shared by file, artifact, and skill autocomplete modules.
auto DefaultMatchStrategy(std::string_view query, std::string_view target) -> bool;

/// Called on every text change. Scans backwards from cursor_pos for an '@'
/// trigger. Invokes on_open_callback exactly once when transitioning from
/// closed to open.
void CheckAutocompleteTrigger(AutocompleteState& state, const std::string& text, int cursor_pos,
                              const std::function<void()>& on_open_callback);

/// Returns true if the event was consumed (overlay is open and key matched).
/// items is the currently filtered/visible item list; on_select is called
/// when the user confirms a selection (Enter/Tab).
auto HandleAutocompleteKeydown(AutocompleteState& state, const std::vector<DirTreeNode*>& items, const ftxui::Event& event,
                               const std::function<void()>& on_select) -> bool;

/// Processes the selection of the currently highlighted item.
/// - Directory: drills in, clears query, optionally triggers lazy load.
/// - File: invokes on_select_file with the relative_path, closes overlay.
void SelectCurrentItem(AutocompleteState& state, const std::vector<DirTreeNode*>& items,
                       const std::function<void(const std::string& relative_path)>& on_select_file,
                       const std::function<void(const std::string& relative_path)>& on_lazy_load);

/// Creates the FTXUI component for the file autocomplete overlay.
///
/// The component renders as zero-size when the overlay is closed. When open,
/// it renders as a bordered panel with filtered file/directory entries.
///
/// on_insert is called with the full insertion string (e.g. "@src/main.cpp")
/// when a file is selected. The caller is responsible for splicing this into
/// the text buffer.
auto FileAutocompleteComponent(std::shared_ptr<AutocompleteState> state, std::filesystem::path project_root,
                               ftxui::ScreenInteractive& screen, std::function<void(const std::string& insertion)> on_insert)
    -> ftxui::Component;

}  // namespace ally::autocomplete
