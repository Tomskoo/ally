#pragma once

#include <ftxui/component/component.hpp>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"

namespace ally::components {

auto nav_bar(ally::AppContext& ctx, ally::Navigator& nav) -> ftxui::Component;

}  // namespace ally::components
