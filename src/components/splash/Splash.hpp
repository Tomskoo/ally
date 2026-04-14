#pragma once

#include <functional>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace ally::components {

auto splash_screen(ftxui::ScreenInteractive& screen, std::function<void()> on_done) -> ftxui::Component;

}  // namespace ally::components
