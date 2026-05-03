#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "src/configuration/ConfigError.hpp"

namespace ally::components {

/// Full-screen error component that blocks startup when configuration is invalid.
/// Consumes all input except 'q' and Escape which exit the application.
auto ConfigErrorScreen(ftxui::ScreenInteractive& screen,
                       const configuration::ConfigError& error) -> ftxui::Component;

}  // namespace ally::components
