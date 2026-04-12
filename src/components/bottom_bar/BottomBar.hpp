#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"

namespace ally::components {

auto bottom_bar(AppContext& ctx, Navigator& nav, ftxui::ScreenInteractive& screen) -> ftxui::Component;

}  // namespace ally::components
