#pragma once

#include <ftxui/component/component.hpp>
#include <string>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"

namespace ally::views {

auto task_detail(AppContext& ctx, Navigator& nav, const std::string& task_id) -> ftxui::Component;

}  // namespace ally::views
