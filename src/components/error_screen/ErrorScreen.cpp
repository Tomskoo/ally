#include "src/components/error_screen/ErrorScreen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace ally::components {

auto ConfigErrorScreen(ScreenInteractive& screen,
                       const configuration::ConfigError& error) -> Component {
  auto renderer = Renderer([&error]() -> Element {
    return vcenter(hcenter(vbox({
        text("CONFIGURATION ERROR") | bold | color(Color::Red) | hcenter,
        text(""),
        hbox({text("File: ") | bold, text(error.file)}) | dim,
        text(""),
        text(error.message) | color(Color::RedLight),
        text(""),
        separator(),
        text("Fix the file above and relaunch ally.") | dim | hcenter,
        text("Press 'q' or Escape to exit.") | dim | hcenter,
    }) | border | size(WIDTH, LESS_THAN, 70)));
  });

  return CatchEvent(renderer, [&screen](const Event& event) -> bool {
    if (event == Event::Character('q') || event == Event::Escape) {
      screen.Exit();
      return true;
    }
    return true;  // consume all events
  });
}

}  // namespace ally::components
