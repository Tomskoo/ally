#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/components/autocomplete/ArtifactTypes.hpp"
#include "src/models/Workflow.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

namespace ally::autocomplete {

/// Scans backwards from cursor_pos for a '$' trigger character.
/// Updates state accordingly (open/close overlay, update query).
void CheckArtifactTrigger(ArtifactAutocompleteState& state, const std::string& text, int cursor_pos);

/// Returns pointers into artifacts_cache for entries matching query against
/// either stage or product via DefaultMatchStrategy.
/// Caller must hold state.mutex while the returned pointers are live.
auto GetFilteredArtifacts(const std::vector<ArtifactEntry>& artifacts_cache, std::string_view query)
    -> std::vector<const ArtifactEntry*>;

/// Splices "@" + selected artifact's relative_path + " " into the text buffer.
/// Closes the overlay.
void SelectCurrentArtifact(ArtifactAutocompleteState& state, const std::string& current_text, std::string& text_out);

/// Routes keyboard events when the overlay is open.
/// Returns true if the event was consumed.
auto HandleArtifactKeydown(ArtifactAutocompleteState& state, const std::string& current_text, std::string& text_out,
                           const ftxui::Event& event) -> bool;

/// Creates the FTXUI overlay component for artifact autocomplete.
/// on_insert receives the full updated text after an artifact is selected.
auto ArtifactAutocompleteComponent(const std::shared_ptr<ArtifactAutocompleteState>& state, ftxui::ScreenInteractive& screen,
                                   std::function<void(const std::string& new_text)> on_insert) -> ftxui::Component;

/// Loads artifacts on a background thread, then subscribes to the artifact
/// watcher broadcast for live updates. The thread runs until
/// state->listener_stop is set to true.
void SetupArtifactsListener(const std::shared_ptr<ArtifactAutocompleteState>& state, const std::string& project_root,
                            const std::string& task_id, const std::string& thread_id,
                            const std::vector<models::WorkflowStage>& stages,
                            watcher::WatcherBroadcast<watcher::ArtifactChangedEvent>& broadcast, ftxui::ScreenInteractive& screen);

}  // namespace ally::autocomplete
