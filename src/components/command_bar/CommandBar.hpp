#pragma once

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>

#include "src/components/command_bar/CommandBarState.hpp"
#include "src/components/command_bar/CommandRegistry.hpp"

namespace ally::components {

/// Renders the command bar input line: ":input_text" with a cursor indicator.
auto RenderCommandBar(const CommandBarState& state) -> ftxui::Element;

/// Renders the autocomplete suggestion overlay (bordered list above the bar).
auto RenderCommandAutocomplete(const CommandBarState& state, const CommandRegistry& registry) -> ftxui::Element;

/// Handles a single event while the command bar is active.
/// Returns true if the command bar consumed the event.
/// Sets state.is_active = false when the bar should close (Enter or Escape).
auto HandleCommandBarEvent(CommandBarState& state, CommandRegistry& registry, const ftxui::Event& event) -> bool;

}  // namespace ally::components
