#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <string>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"

namespace ally::views {

auto stage_view(AppContext& ctx, Navigator& nav, ftxui::ScreenInteractive& screen, const std::string& task_id,
                const std::string& thread_id, const std::string& stage) -> ftxui::Component;

}  // namespace ally::views
