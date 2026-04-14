#include "Splash.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace ally::components {
namespace {

using namespace ftxui;

constexpr int kFrameIntervalMs = 50;
constexpr int kRaccoonSlideFrames = 18;
constexpr int kAllySlideFrames = 12;
constexpr int kAllyStartFrame = kRaccoonSlideFrames;
constexpr int kHoldStart = kAllyStartFrame + kAllySlideFrames;
constexpr int kHoldFrames = 18;
constexpr int kExitFrame = kHoldStart + kHoldFrames;
constexpr int kSlideDistance = 60;

// clang-format off
const std::vector<std::string> kRaccoon = {
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⣀⣤⣤⣤⣄⣀⣀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⣤⡶⠞⠋⠉⠁⠀⠀⠀⠀⠀⠈⠉⠻⣶⣤⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣴⡶⠟⠛⠛⠳⠟⠋⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉⠻⣷⡄⠀⠀⠀⠀⠀⠀⠀⠀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⢠⣾⠋⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⢻⡆⠀⠀⠀⠀⠀⠀⠀",
    "⢠⡟⢳⢦⣄⠀⢀⣠⠟⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠙⣿⡀⠀⠀⠀⠀⠀⠀",
    "⠈⢿⣮⣳⣿⡍⠉⠉⠑⣲⣒⣶⡓⡆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⣧⠀⠀⠀⠀⠀⠀",
    "⠀⠀⡞⠉⠁⢠⡇⠀⠈⠉⠉⢸⣧⣿⣆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣿⣦⠀⠀⠀⠀⠀",
    "⠀⠀⡇⠀⢀⣿⡀⠀⠀⠀⠀⠀⠈⠉⠻⢧⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣿⣿⣷⡀⠀⠀⠀",
    "⠀⠀⣧⣄⢸⠏⠀⣠⠀⠀⠀⠀⠀⠀⠀⠈⢧⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣼⠿⠟⠛⣷⡀⠀⠀",
    "⠀⠀⣿⣽⣾⠀⣼⡻⣶⣶⣶⣿⣤⣀⣀⣠⡾⠇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣼⠃⣠⣤⣶⣿⣧⠀⠀",
    "⠀⠀⡾⠋⠙⠀⠉⠙⠛⠉⠙⠏⢩⡿⠟⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⣾⣧⣶⡿⠛⠉⠉⢽⣆⠀",
    "⠀⠀⣷⣤⣦⡀⠀⠀⠀⣀⣤⡿⠟⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⣼⠿⣿⠛⣴⣾⠿⠿⠿⣿⡆",
    "⠀⠀⠙⠷⠶⠯⠶⢾⠋⠙⠛⢶⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡞⠁⢸⣷⣄⠀⠀⠀⠀⠀⠀⠀⣿⠁⠀⢹⣶⡟⠉⣀⣠⣄⣀⢹",
    "⠀⠀⠀⠀⠀⠀⠀⢸⡇⠀⠀⠀⢿⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⢸⡏⠻⢷⣄⡀⠀⠀⠀⠀⢹⡄⠀⢈⣿⣿⡿⠛⠉⠛⢿⣿",
    "⠀⠀⠀⠀⠀⠀⠀⢸⡇⠀⠀⠀⢸⡇⠀⠀⠀⠀⠀⢰⡴⠶⠷⠶⠟⣁⣤⠶⠋⠁⠀⠀⠀⢀⣼⠃⠀⣼⣯⣾⣷⣾⣦⣄⠀⣿",
    "⠀⠀⠀⠀⠀⠀⠀⣼⠁⠀⠀⣠⣾⡇⠀⠀⠀⠀⢠⣸⡇⠀⢠⣴⣟⡍⠀⠀⠀⠀⣀⣤⠞⠛⠁⢀⣼⣟⣻⢋⠉⠛⠛⢿⣧⡿",
    "⠀⠀⠀⠀⣀⣀⣰⠏⠀⠀⣴⠏⢸⠃⠀⠀⣆⢠⡼⠿⠇⠀⠙⢻⡿⣶⣶⣂⣴⠿⠋⠁⠀⠀⢠⣿⣿⣿⣿⣿⣷⣄⡀⠈⣿⠃",
    "⠀⠀⢼⣿⡭⣼⡍⢠⢄⣼⠏⠀⣼⠀⠀⢠⡟⣿⠁⠀⠀⠀⠀⠀⠀⠉⠉⠉⠀⠀⠀⠀⠀⢠⣿⠅⠀⠀⠀⠈⠹⢻⣿⣾⠏⠀",
    "⠀⠀⠈⠉⠿⠟⠚⠿⠟⣁⣤⡼⠃⠀⢀⣾⠁⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣾⣿⣿⣦⣄⣀⠀⠀⢀⣿⠋⠀⠀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⢰⡫⢄⡀⠀⣀⢸⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⣿⣿⣿⣿⣿⣿⣷⠟⠁⠀⠀⠀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠙⠿⢿⣥⠾⠛⢼⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠻⠿⠛⠛⠉⠀⠀⠀⠀⠀⠀",
    "⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠁⠀⠀⠀⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀",
};

const std::vector<std::string> kAlly = {
    R"(  █████╗  ██╗     ██╗  ██╗   ██╗ )",
    R"( ██╔══██╗ ██║     ██║  ╚██╗ ██╔╝ )",
    R"( ███████║ ██║     ██║   ╚████╔╝  )",
    R"( ██╔══██║ ██║     ██║    ╚██╔╝   )",
    R"( ██║  ██║ ███████╗███████╗██║    )",
    R"( ╚═╝  ╚═╝ ╚══════╝╚══════╝╚═╝   )",
};
// clang-format on

auto ease_out(float t) -> float {
  return 1.0F - (1.0F - t) * (1.0F - t);
}

auto slide_offset(int current_frame, int start_frame, int slide_frames) -> int {
  int progress = std::clamp(current_frame - start_frame, 0, slide_frames);
  float t = static_cast<float>(progress) / static_cast<float>(slide_frames);
  return static_cast<int>(static_cast<float>(kSlideDistance) * (1.0F - ease_out(t)));
}

}  // namespace

auto splash_screen(ScreenInteractive& screen, std::function<void()> on_done) -> Component {
  auto frame = std::make_shared<std::atomic<int>>(0);
  auto done = std::make_shared<std::atomic<bool>>(false);
  auto finish = std::make_shared<std::function<void()>>(std::move(on_done));

  std::thread([&screen, frame, done, finish]() {
    while (!done->load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kFrameIntervalMs));
      if (done->load()) return;
      int f = frame->fetch_add(1) + 1;
      screen.PostEvent(Event::Custom);
      if (f >= kExitFrame) {
        bool expected = false;
        if (done->compare_exchange_strong(expected, true)) {
          (*finish)();
        }
        return;
      }
    }
  }).detach();

  auto skip = [done, finish]() {
    bool expected = false;
    if (done->compare_exchange_strong(expected, true)) {
      (*finish)();
    }
  };

  return Renderer([frame]() {
    int f = frame->load();

    Elements raccoon_els;
    for (const auto& line : kRaccoon) {
      raccoon_els.push_back(text(line) | color(Color::GrayLight));
    }
    auto raccoon_block = vbox(std::move(raccoon_els));

    int raccoon_extra = slide_offset(f, 0, kRaccoonSlideFrames);
    auto raccoon_row = hbox({
        filler(),
        text(std::string(raccoon_extra, ' ')),
        raccoon_block,
        filler(),
    });

    Elements content;
    content.push_back(raccoon_row);

    if (f >= kAllyStartFrame) {
      Elements ally_els;
      for (const auto& line : kAlly) {
        ally_els.push_back(text(line) | bold | color(Color::Cyan));
      }
      auto ally_block = vbox(std::move(ally_els));

      int ally_extra = slide_offset(f, kAllyStartFrame, kAllySlideFrames);
      auto ally_row = hbox({
          filler(),
          ally_block,
          text(std::string(ally_extra, ' ')),
          filler(),
      });

      content.push_back(text(""));
      content.push_back(ally_row);
    }

    return vcenter(vbox(std::move(content)));
  }) | CatchEvent([skip](const Event& event) {
    if (event.is_character() || event == Event::Return ||
        event == Event::Escape || event == Event::ArrowUp ||
        event == Event::ArrowDown || event == Event::ArrowLeft ||
        event == Event::ArrowRight) {
      skip();
      return true;
    }
    return false;
  });
}

}  // namespace ally::components
