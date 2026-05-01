#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <memory>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"
#include "src/components/command_bar/CommandBarState.hpp"
#include "src/components/command_bar/CommandRegistry.hpp"

namespace ally::components {

auto bottom_bar(AppContext& ctx, Navigator& nav, ftxui::ScreenInteractive& screen,
                const std::shared_ptr<CommandBarState>& cmd_state,
                const std::shared_ptr<CommandRegistry>& cmd_registry) -> ftxui::Component;

}  // namespace ally::components
