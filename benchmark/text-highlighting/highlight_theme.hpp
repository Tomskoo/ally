#pragma once
#include <ftxui/dom/elements.hpp>
#include <string_view>

namespace ally::rendering {

// Neovim-inspired theme using true-color (RGB).
// Loosely based on the "tokyonight" palette.
struct HighlightTheme {
  // Language constructs
  ftxui::Color keyword = ftxui::Color::RGB(187, 154, 247);  // purple
  ftxui::Color keyword_return = ftxui::Color::RGB(187, 154, 247);
  ftxui::Color conditional = ftxui::Color::RGB(187, 154, 247);
  ftxui::Color repeat = ftxui::Color::RGB(187, 154, 247);
  ftxui::Color include = ftxui::Color::RGB(125, 174, 247);  // blue-ish

  // Functions
  ftxui::Color function = ftxui::Color::RGB(122, 162, 247);  // blue
  ftxui::Color function_method = ftxui::Color::RGB(122, 162, 247);
  ftxui::Color function_builtin = ftxui::Color::RGB(125, 207, 255);  // light blue
  ftxui::Color function_macro = ftxui::Color::RGB(125, 207, 255);

  // Types and constructors
  ftxui::Color type = ftxui::Color::RGB(42, 195, 222);  // cyan
  ftxui::Color type_builtin = ftxui::Color::RGB(42, 195, 222);
  ftxui::Color constructor = ftxui::Color::RGB(255, 158, 100);  // orange

  // Variables
  ftxui::Color variable = ftxui::Color::RGB(192, 202, 245);          // light gray-blue
  ftxui::Color variable_builtin = ftxui::Color::RGB(255, 117, 127);  // red
  ftxui::Color variable_param = ftxui::Color::RGB(224, 175, 104);    // yellow-ish
  ftxui::Color property = ftxui::Color::RGB(115, 218, 202);          // teal

  // Literals
  ftxui::Color string = ftxui::Color::RGB(158, 206, 106);        // green
  ftxui::Color string_escape = ftxui::Color::RGB(42, 195, 222);  // cyan
  ftxui::Color number = ftxui::Color::RGB(255, 158, 100);        // orange
  ftxui::Color constant = ftxui::Color::RGB(255, 158, 100);      // orange
  ftxui::Color constant_builtin = ftxui::Color::RGB(255, 158, 100);
  ftxui::Color boolean = ftxui::Color::RGB(255, 158, 100);

  // Comments
  ftxui::Color comment = ftxui::Color::RGB(86, 95, 137);  // muted blue-gray

  // Operators and punctuation
  ftxui::Color operator_ = ftxui::Color::RGB(137, 221, 255);  // light cyan
  ftxui::Color delimiter = ftxui::Color::RGB(86, 95, 137);    // muted
  ftxui::Color bracket = ftxui::Color::RGB(169, 177, 214);    // lighter gray
  ftxui::Color punctuation_special = ftxui::Color::RGB(42, 195, 222);

  // Misc
  ftxui::Color attribute = ftxui::Color::RGB(224, 175, 104);  // yellow
  ftxui::Color label = ftxui::Color::RGB(122, 162, 247);
  ftxui::Color embedded = ftxui::Color::RGB(42, 195, 222);
  ftxui::Color default_ = ftxui::Color::RGB(192, 202, 245);  // fg
};

/// Map a tree-sitter query capture name (e.g. "function", "keyword",
/// "variable.builtin") to a theme color. Capture names follow Neovim
/// conventions from nvim-treesitter highlight queries.
inline ftxui::Color ResolveCapture(std::string_view capture, const HighlightTheme& theme) {
  // Functions
  if (capture == "function") return theme.function;
  if (capture == "function.method") return theme.function_method;
  if (capture == "function.builtin") return theme.function_builtin;
  if (capture == "function.special") return theme.function_macro;
  if (capture == "function.macro") return theme.function_macro;

  // Keywords
  if (capture == "keyword") return theme.keyword;
  if (capture == "keyword.return") return theme.keyword_return;
  if (capture == "conditional") return theme.conditional;
  if (capture == "repeat") return theme.repeat;
  if (capture == "include") return theme.include;

  // Types
  if (capture == "type") return theme.type;
  if (capture == "type.builtin") return theme.type_builtin;
  if (capture == "constructor") return theme.constructor;

  // Variables
  if (capture == "variable") return theme.variable;
  if (capture == "variable.builtin") return theme.variable_builtin;
  if (capture == "variable.parameter") return theme.variable_param;
  if (capture == "property") return theme.property;

  // Literals
  if (capture == "string") return theme.string;
  if (capture == "string.special") return theme.string;
  if (capture == "escape") return theme.string_escape;
  if (capture == "number") return theme.number;
  if (capture == "constant") return theme.constant;
  if (capture == "constant.builtin") return theme.constant_builtin;
  if (capture == "boolean") return theme.boolean;

  // Comments
  if (capture == "comment") return theme.comment;
  if (capture == "comment.documentation") return theme.comment;

  // Operators & punctuation
  if (capture == "operator") return theme.operator_;
  if (capture == "delimiter") return theme.delimiter;
  if (capture == "punctuation.delimiter") return theme.delimiter;
  if (capture == "punctuation.bracket") return theme.bracket;
  if (capture == "punctuation.special") return theme.punctuation_special;

  // Misc
  if (capture == "attribute") return theme.attribute;
  if (capture == "label") return theme.label;
  if (capture == "embedded") return theme.embedded;

  return theme.default_;
}

}  // namespace ally::rendering
