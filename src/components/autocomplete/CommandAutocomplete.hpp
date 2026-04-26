#pragma once

#include "src/configuration/InputConfig.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/components/autocomplete/CommandTypes.hpp"
#include "src/services/CommandService.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

namespace ally::autocomplete {

/// Scans backwards from cursor_pos for a '/' trigger character.
/// Updates state accordingly (open/close overlay, update query).
void CheckCommandTrigger(CommandAutocompleteState& state, const std::string& text, int cursor_pos);

/// Returns pointers into commands_cache for entries matching query against
/// either name or description via DefaultMatchStrategy.
/// Caller must hold state.mutex while the returned pointers are live.
auto GetFilteredCommands(const std::vector<CommandEntry>& commands_cache, std::string_view query) -> std::vector<const CommandEntry*>;

/// Splices "/" + selected command's name + " " into the text buffer.
/// Closes the overlay.
void SelectCurrentCommand(CommandAutocompleteState& state, const std::string& current_text, std::string& text_out);

/// Routes keyboard events when the overlay is open.
/// Returns true if the event was consumed.
auto HandleCommandKeydown(CommandAutocompleteState& state, const std::string& current_text, std::string& text_out,
                          const ftxui::Event& event, const ally::configuration::InputConfig& input_config) -> bool;

/// Creates the FTXUI overlay component for command autocomplete.
/// on_insert receives the full updated text after a command is selected.
auto CommandAutocompleteComponent(const std::shared_ptr<CommandAutocompleteState>& state, ftxui::ScreenInteractive& screen,
                                  std::function<void(const std::string& new_text)> on_insert) -> ftxui::Component;

/// Fetches commands on a background thread, then subscribes to the commands
/// watcher broadcast for live updates. The thread runs until
/// state->listener_stop is set to true.
void SetupCommandsListener(const std::shared_ptr<CommandAutocompleteState>& state, ally::services::CommandService& service,
                           watcher::WatcherBroadcast<watcher::CommandsChangedEvent>& broadcast, ftxui::ScreenInteractive& screen);

}  // namespace ally::autocomplete
