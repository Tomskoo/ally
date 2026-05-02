#pragma once

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"
#include "src/components/command_bar/CommandRegistry.hpp"

namespace ally::commands::navigation {

void RegisterCommands(components::CommandRegistry& registry, AppContext& ctx, Navigator& nav);

}  // namespace ally::commands::navigation
