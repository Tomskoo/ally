#pragma once

#include <ftxui/screen/color.hpp>

using namespace ftxui;

namespace ally::style::colour {

struct Settings {
  ftxui::Color primary;
  ftxui::Color secondary;
  ftxui::Color surface;
  ftxui::Color positive_accent;
  ftxui::Color neutral_accent;
  ftxui::Color negative_accent;
};

}  // namespace ally::style::colour
