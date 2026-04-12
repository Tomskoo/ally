#include "FormStyle.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

using namespace ftxui;

namespace ally::style::form {

namespace {
constexpr int kLabelWidth = 16;
}

auto decorated_input_option() -> InputOption {
  InputOption opt;
  opt.multiline = false;
  opt.transform = [](const InputState& state) -> Element {
    auto elem = state.element;
    if (state.focused) {
      elem = elem | border | color(Color::Cyan);
    } else {
      elem = elem | border | dim;
    }
    return elem;
  };
  return opt;
}

auto primary_button_option() -> ButtonOption {
  ButtonOption opt;
  opt.transform = [](const EntryState& state) -> Element {
    auto label = text(state.label) | bold;
    if (state.focused) {
      return label | inverted | border;
    }
    return label | border;
  };
  return opt;
}

auto secondary_button_option() -> ButtonOption {
  ButtonOption opt;
  opt.transform = [](const EntryState& state) -> Element {
    auto label = text(state.label);
    if (state.focused) {
      return label | inverted | border;
    }
    return label | border | dim;
  };
  return opt;
}

auto make_field_row(const std::string& label, Element field, const std::string& error) -> Element {
  auto row = hbox({
      text(label) | bold | size(WIDTH, EQUAL, kLabelWidth) | vcenter,
      std::move(field) | flex,
  });
  if (!error.empty()) {
    return vbox({
        row,
        hbox({
            text("") | size(WIDTH, EQUAL, kLabelWidth),
            text(error) | color(Color::Red),
        }),
    });
  }
  return row;
}

auto trim(const std::string& str) -> std::string {
  auto start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) {
    return "";
  }
  auto end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

}  // namespace ally::style::form
