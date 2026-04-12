#pragma once

#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>

namespace ally::style::form {

/// Returns an InputOption whose transform wraps the input element in a border.
/// Focused: cyan border. Unfocused: dimmed border.
auto decorated_input_option() -> ftxui::InputOption;

/// Returns a ButtonOption for primary actions (e.g. "Create Task").
/// Bold text, border, inverted on focus.
auto primary_button_option() -> ftxui::ButtonOption;

/// Returns a ButtonOption for secondary actions (e.g. "Cancel").
/// Normal text, border, inverted on focus.
auto secondary_button_option() -> ftxui::ButtonOption;

/// Renders a label + field row, with an optional error message below the field.
auto make_field_row(const std::string& label, ftxui::Element field, const std::string& error = "") -> ftxui::Element;

/// Trims leading and trailing whitespace from a string.
auto trim(const std::string& str) -> std::string;

}  // namespace ally::style::form
