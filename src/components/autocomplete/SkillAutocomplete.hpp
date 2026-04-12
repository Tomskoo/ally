#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/components/autocomplete/SkillTypes.hpp"
#include "src/services/SkillService.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

namespace ally::autocomplete {

/// Scans backwards from cursor_pos for a '/' trigger character.
/// Updates state accordingly (open/close overlay, update query).
void CheckSkillTrigger(SkillAutocompleteState& state, const std::string& text, int cursor_pos);

/// Returns pointers into skills_cache for entries matching query against
/// either dir_name or name via DefaultMatchStrategy.
/// Caller must hold state.mutex while the returned pointers are live.
auto GetFilteredSkills(const std::vector<SkillEntry>& skills_cache, std::string_view query) -> std::vector<const SkillEntry*>;

/// Splices "/" + selected skill's dir_name + " " into the text buffer.
/// Closes the overlay.
void SelectCurrentSkill(SkillAutocompleteState& state, const std::string& current_text, std::string& text_out);

/// Routes keyboard events when the overlay is open.
/// Returns true if the event was consumed.
auto HandleSkillKeydown(SkillAutocompleteState& state, const std::string& current_text, std::string& text_out,
                        const ftxui::Event& event) -> bool;

/// Creates the FTXUI overlay component for skill autocomplete.
/// on_insert receives the full updated text after a skill is selected.
auto SkillAutocompleteComponent(const std::shared_ptr<SkillAutocompleteState>& state, ftxui::ScreenInteractive& screen,
                                std::function<void(const std::string& new_text)> on_insert) -> ftxui::Component;

/// Fetches skills on a background thread, then subscribes to the skills
/// watcher broadcast for live updates. The thread runs until
/// state->listener_stop is set to true.
void SetupSkillsListener(const std::shared_ptr<SkillAutocompleteState>& state, ally::services::SkillService& service,
                         watcher::WatcherBroadcast<watcher::SkillsChangedEvent>& broadcast, ftxui::ScreenInteractive& screen);

}  // namespace ally::autocomplete
