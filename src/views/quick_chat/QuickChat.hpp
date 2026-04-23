#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"

namespace ally::views {

auto quick_chat(AppContext& ctx, Navigator& nav, ftxui::ScreenInteractive& screen) -> ftxui::Component;

}  // namespace ally::views
