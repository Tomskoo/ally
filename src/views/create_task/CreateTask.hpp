#pragma once

#include <ftxui/component/component.hpp>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"

namespace ally::views {

auto create_task(AppContext& ctx, Navigator& nav) -> ftxui::Component;

}  // namespace ally::views
