#pragma once

#include <ftxui/component/component.hpp>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"

namespace ally::views {

auto task_board(AppContext& ctx, Navigator& nav) -> ftxui::Component;

}  // namespace ally::views
