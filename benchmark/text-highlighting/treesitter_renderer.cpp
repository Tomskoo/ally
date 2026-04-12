#include "treesitter_renderer.hpp"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstring>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <vector>

#include "highlight_theme.hpp"

// Language declarations from grammar libraries
extern "C" {
const TSLanguage* tree_sitter_cpp();
const TSLanguage* tree_sitter_python();
const TSLanguage* tree_sitter_javascript();
const TSLanguage* tree_sitter_rust();
}

using namespace ftxui;

namespace ally::rendering {

namespace {

// ---------------------------------------------------------------------------
// Embedded highlight queries from nvim-treesitter / tree-sitter grammar repos.
// The C++ query inherits from C, so we concatenate C + C++ queries.
// Predicates like (#match? ...) are ignored by the tree-sitter query engine
// at runtime (they become predicate steps we skip), but the patterns still
// produce captures for non-predicated nodes.
// ---------------------------------------------------------------------------

constexpr const char* kHighlightsCpp = R"scm(
; === C base highlights ===

(identifier) @variable

((identifier) @constant
 (#match? @constant "^[A-Z][A-Z\\d_]*$"))

"break" @keyword
"case" @keyword
"const" @keyword
"continue" @keyword
"default" @keyword
"do" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"for" @keyword
"if" @keyword
"inline" @keyword
"return" @keyword
"sizeof" @keyword
"static" @keyword
"struct" @keyword
"switch" @keyword
"typedef" @keyword
"union" @keyword
"volatile" @keyword
"while" @keyword

"#define" @keyword
"#elif" @keyword
"#else" @keyword
"#endif" @keyword
"#if" @keyword
"#ifdef" @keyword
"#ifndef" @keyword
"#include" @keyword
(preproc_directive) @keyword

"--" @operator
"-" @operator
"-=" @operator
"->" @operator
"=" @operator
"!=" @operator
"*" @operator
"&" @operator
"&&" @operator
"+" @operator
"++" @operator
"+=" @operator
"<" @operator
"==" @operator
">" @operator
"||" @operator
"!" @operator
"%" @operator
"^" @operator
"|" @operator
"~" @operator
">>" @operator
"<<" @operator

"." @delimiter
";" @delimiter

"(" @punctuation.bracket
")" @punctuation.bracket
"[" @punctuation.bracket
"]" @punctuation.bracket
"{" @punctuation.bracket
"}" @punctuation.bracket

(string_literal) @string
(system_lib_string) @string

(null) @constant
(number_literal) @number
(char_literal) @number

(field_identifier) @property
(statement_identifier) @label
(type_identifier) @type
(primitive_type) @type
(sized_type_specifier) @type

(call_expression
  function: (identifier) @function)
(call_expression
  function: (field_expression
    field: (field_identifier) @function))
(function_declarator
  declarator: (identifier) @function)
(preproc_function_def
  name: (identifier) @function.special)

(comment) @comment

; === C++ additions ===

(call_expression
  function: (qualified_identifier
    name: (identifier) @function))

(template_function
  name: (identifier) @function)

(template_method
  name: (field_identifier) @function)

(function_declarator
  declarator: (qualified_identifier
    name: (identifier) @function))

(function_declarator
  declarator: (field_identifier) @function)

((namespace_identifier) @type
 (#match? @type "^[A-Z]"))

(auto) @type

(this) @variable.builtin
(null "nullptr" @constant)

[
 "catch"
 "class"
 "co_await"
 "co_return"
 "co_yield"
 "constexpr"
 "constinit"
 "consteval"
 "delete"
 "explicit"
 "final"
 "friend"
 "mutable"
 "namespace"
 "noexcept"
 "new"
 "override"
 "private"
 "protected"
 "public"
 "template"
 "throw"
 "try"
 "typename"
 "using"
 "concept"
 "requires"
 "virtual"
] @keyword

(raw_string_literal) @string
)scm";

constexpr const char* kHighlightsPython = R"scm(
(identifier) @variable

((identifier) @constructor
 (#match? @constructor "^[A-Z]"))

((identifier) @constant
 (#match? @constant "^[A-Z][A-Z_]*$"))

(decorator) @function
(decorator
  (identifier) @function)

(call
  function: (attribute attribute: (identifier) @function.method))
(call
  function: (identifier) @function)

((call
  function: (identifier) @function.builtin)
 (#match?
   @function.builtin
   "^(abs|all|any|ascii|bin|bool|breakpoint|bytearray|bytes|callable|chr|classmethod|compile|complex|delattr|dict|dir|divmod|enumerate|eval|exec|filter|float|format|frozenset|getattr|globals|hasattr|hash|help|hex|id|input|int|isinstance|issubclass|iter|len|list|locals|map|max|memoryview|min|next|object|oct|open|ord|pow|print|property|range|repr|reversed|round|set|setattr|slice|sorted|staticmethod|str|sum|super|tuple|type|vars|zip|__import__)$"))

(function_definition
  name: (identifier) @function)

(attribute attribute: (identifier) @property)
(type (identifier) @type)

[
  (none)
  (true)
  (false)
] @constant.builtin

[
  (integer)
  (float)
] @number

(comment) @comment
(string) @string
(escape_sequence) @escape

(interpolation
  "{" @punctuation.special
  "}" @punctuation.special) @embedded

[
  "-"
  "-="
  "!="
  "*"
  "**"
  "**="
  "*="
  "/"
  "//"
  "//="
  "/="
  "&"
  "&="
  "%"
  "%="
  "^"
  "^="
  "+"
  "->"
  "+="
  "<"
  "<<"
  "<<="
  "<="
  "<>"
  "="
  ":="
  "=="
  ">"
  ">="
  ">>"
  ">>="
  "|"
  "|="
  "~"
  "@="
] @operator

["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["." "," ":" ";"] @delimiter

[
  "as"
  "assert"
  "async"
  "await"
  "break"
  "class"
  "continue"
  "def"
  "del"
  "elif"
  "else"
  "except"
  "exec"
  "finally"
  "for"
  "from"
  "global"
  "if"
  "import"
  "lambda"
  "nonlocal"
  "pass"
  "print"
  "raise"
  "return"
  "try"
  "while"
  "with"
  "yield"
  "match"
  "case"
  "and"
  "in"
  "is"
  "not"
  "or"
  "is not"
  "not in"
] @keyword
)scm";

constexpr const char* kHighlightsJavascript = R"scm(
(identifier) @variable

(property_identifier) @property

(function_expression
  name: (identifier) @function)
(function_declaration
  name: (identifier) @function)
(method_definition
  name: (property_identifier) @function.method)

(pair
  key: (property_identifier) @function.method
  value: [(function_expression) (arrow_function)])

(assignment_expression
  left: (member_expression
    property: (property_identifier) @function.method)
  right: [(function_expression) (arrow_function)])

(variable_declarator
  name: (identifier) @function
  value: [(function_expression) (arrow_function)])

(assignment_expression
  left: (identifier) @function
  right: [(function_expression) (arrow_function)])

(call_expression
  function: (identifier) @function)

(call_expression
  function: (member_expression
    property: (property_identifier) @function.method))

((identifier) @constructor
 (#match? @constructor "^[A-Z]"))

([
    (identifier)
    (shorthand_property_identifier)
    (shorthand_property_identifier_pattern)
 ] @constant
 (#match? @constant "^[A-Z_][A-Z\\d_]+$"))

((identifier) @variable.builtin
 (#match? @variable.builtin "^(arguments|module|console|window|document)$"))

((identifier) @function.builtin
 (#eq? @function.builtin "require"))

(this) @variable.builtin
(super) @variable.builtin

[
  (true)
  (false)
  (null)
  (undefined)
] @constant.builtin

(comment) @comment

[
  (string)
  (template_string)
] @string

(regex) @string.special
(number) @number

[
  ";"
  "."
  ","
] @delimiter

[
  "-"
  "--"
  "-="
  "+"
  "++"
  "+="
  "*"
  "*="
  "**"
  "**="
  "/"
  "/="
  "%"
  "%="
  "<"
  "<="
  "<<"
  "<<="
  "="
  "=="
  "==="
  "!"
  "!="
  "!=="
  "=>"
  ">"
  ">="
  ">>"
  ">>="
  ">>>"
  ">>>="
  "~"
  "^"
  "&"
  "|"
  "^="
  "&="
  "|="
  "&&"
  "||"
  "??"
  "&&="
  "||="
  "??="
] @operator

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
]  @punctuation.bracket

(template_substitution
  "${" @punctuation.special
  "}" @punctuation.special) @embedded

[
  "as"
  "async"
  "await"
  "break"
  "case"
  "catch"
  "class"
  "const"
  "continue"
  "debugger"
  "default"
  "delete"
  "do"
  "else"
  "export"
  "extends"
  "finally"
  "for"
  "from"
  "function"
  "get"
  "if"
  "import"
  "in"
  "instanceof"
  "let"
  "new"
  "of"
  "return"
  "set"
  "static"
  "switch"
  "throw"
  "try"
  "typeof"
  "var"
  "void"
  "while"
  "with"
  "yield"
] @keyword
)scm";

constexpr const char* kHighlightsRust = R"scm(
(type_identifier) @type
(primitive_type) @type.builtin
(field_identifier) @property

((identifier) @constant
 (#match? @constant "^[A-Z][A-Z\\d_]+$"))

((identifier) @constructor
 (#match? @constructor "^[A-Z]"))

((scoped_identifier
  path: (identifier) @type)
 (#match? @type "^[A-Z]"))
((scoped_type_identifier
  path: (identifier) @type)
 (#match? @type "^[A-Z]"))

(call_expression
  function: (identifier) @function)
(call_expression
  function: (field_expression
    field: (field_identifier) @function.method))
(call_expression
  function: (scoped_identifier
    "::"
    name: (identifier) @function))

(generic_function
  function: (identifier) @function)
(generic_function
  function: (scoped_identifier
    name: (identifier) @function))
(generic_function
  function: (field_expression
    field: (field_identifier) @function.method))

(macro_invocation
  macro: (identifier) @function.macro
  "!" @function.macro)

(function_item (identifier) @function)
(function_signature_item (identifier) @function)

(line_comment) @comment
(block_comment) @comment

"(" @punctuation.bracket
")" @punctuation.bracket
"[" @punctuation.bracket
"]" @punctuation.bracket
"{" @punctuation.bracket
"}" @punctuation.bracket

(type_arguments
  "<" @punctuation.bracket
  ">" @punctuation.bracket)
(type_parameters
  "<" @punctuation.bracket
  ">" @punctuation.bracket)

"::" @punctuation.delimiter
":" @delimiter
"." @delimiter
"," @delimiter
";" @delimiter

(parameter (identifier) @variable.parameter)

(lifetime (identifier) @label)

"as" @keyword
"async" @keyword
"await" @keyword
"break" @keyword
"const" @keyword
"continue" @keyword
"default" @keyword
"dyn" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"fn" @keyword
"for" @keyword
"if" @keyword
"impl" @keyword
"in" @keyword
"let" @keyword
"loop" @keyword
"macro_rules!" @keyword
"match" @keyword
"mod" @keyword
"move" @keyword
"pub" @keyword
"ref" @keyword
"return" @keyword
"static" @keyword
"struct" @keyword
"trait" @keyword
"type" @keyword
"unsafe" @keyword
"use" @keyword
"where" @keyword
"while" @keyword
"yield" @keyword
(crate) @keyword
(mutable_specifier) @keyword
(use_list (self) @keyword)
(scoped_use_list (self) @keyword)
(scoped_identifier (self) @keyword)
(super) @keyword

(self) @variable.builtin
(identifier) @variable

(char_literal) @string
(string_literal) @string
(raw_string_literal) @string

(boolean_literal) @constant.builtin
(integer_literal) @number
(float_literal) @number

(escape_sequence) @escape

(attribute_item) @attribute
(inner_attribute_item) @attribute

"*" @operator
"&" @operator
"'" @operator
"=" @operator
"==" @operator
"!=" @operator
"<" @operator
">" @operator
"<=" @operator
">=" @operator
"+" @operator
"-" @operator
"%" @operator
"|" @operator
"&&" @operator
"||" @operator
"!" @operator
"->" @operator
"=>" @operator
".." @operator
"..=" @operator
)scm";

// ---------------------------------------------------------------------------
// Language info: language pointer + highlight query source
// ---------------------------------------------------------------------------

struct LangInfo {
  const TSLanguage* language;
  const char* highlights;
  TSQuery* compiled_query;  // lazily compiled, cached for reuse
  std::vector<uint8_t> capture_to_slot;
  std::vector<ftxui::Color> slot_colors;
};

// Static cache of compiled queries. Initialized on first use.
LangInfo& GetLangInfo(const std::string& lang) {
  static LangInfo langs[] = {
      {tree_sitter_cpp(), kHighlightsCpp, nullptr, {}, {}},
      {tree_sitter_python(), kHighlightsPython, nullptr, {}, {}},
      {tree_sitter_javascript(), kHighlightsJavascript, nullptr, {}, {}},
      {tree_sitter_rust(), kHighlightsRust, nullptr, {}, {}},
      {nullptr, nullptr, nullptr, {}, {}},
  };
  if (lang == "cpp" || lang == "c++") return langs[0];
  if (lang == "python" || lang == "py") return langs[1];
  if (lang == "javascript" || lang == "js") return langs[2];
  if (lang == "rust" || lang == "rs") return langs[3];
  return langs[4];
}

void EnsureQueryCompiled(LangInfo& info, const HighlightTheme& theme) {
  if (info.compiled_query || !info.language) return;

  uint32_t error_offset = 0;
  TSQueryError error_type = TSQueryErrorNone;
  info.compiled_query = ts_query_new(info.language, info.highlights, strlen(info.highlights), &error_offset, &error_type);

  if (!info.compiled_query) return;

  uint32_t capture_count = ts_query_capture_count(info.compiled_query);
  info.slot_colors.push_back(theme.default_);  // slot 0 = default
  info.capture_to_slot.resize(capture_count, 0);

  for (uint32_t c = 0; c < capture_count; ++c) {
    uint32_t name_len = 0;
    const char* name = ts_query_capture_name_for_id(info.compiled_query, c, &name_len);
    auto col = ResolveCapture(std::string_view(name, name_len), theme);
    uint8_t slot = 0;
    for (uint8_t s = 0; s < info.slot_colors.size(); ++s) {
      if (info.slot_colors[s] == col) {
        slot = s;
        break;
      }
      if (s == info.slot_colors.size() - 1) {
        slot = static_cast<uint8_t>(info.slot_colors.size());
        info.slot_colors.push_back(col);
        break;
      }
    }
    info.capture_to_slot[c] = slot;
  }
}

// ---------------------------------------------------------------------------
// Span with byte range and capture name resolved to a color
// ---------------------------------------------------------------------------

struct ColorSpan {
  uint32_t start;
  uint32_t end;
  ftxui::Color color;
};

// Run the cached highlight query and collect color spans.
// Uses a uint8_t-per-byte slot index for performance.
std::vector<ColorSpan> RunHighlightQuery(LangInfo& info, TSNode root, const std::string& code) {
  if (!info.compiled_query) return {};

  // Per-byte color slot index (1 byte each instead of ~24 bytes for Color)
  std::vector<uint8_t> byte_slots(code.size(), 0);

  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, info.compiled_query, root);

  TSQueryMatch match;
  uint32_t capture_index = 0;
  while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
    const TSQueryCapture& cap = match.captures[capture_index];
    uint32_t start = ts_node_start_byte(cap.node);
    uint32_t end = ts_node_end_byte(cap.node);
    uint8_t slot = info.capture_to_slot[cap.index];

    uint32_t clamp_end = (end < code.size()) ? end : static_cast<uint32_t>(code.size());
    std::memset(byte_slots.data() + start, slot, clamp_end - start);
  }

  ts_query_cursor_delete(cursor);

  // Compress consecutive bytes with the same slot into spans
  std::vector<ColorSpan> spans;
  if (code.empty()) return spans;

  uint32_t span_start = 0;
  uint8_t span_slot = byte_slots[0];
  for (uint32_t i = 1; i <= code.size(); ++i) {
    uint8_t cur = (i < code.size()) ? byte_slots[i] : 255;
    if (cur != span_slot) {
      spans.push_back({span_start, i, info.slot_colors[span_slot]});
      span_start = i;
      span_slot = cur;
    }
  }

  return spans;
}

}  // namespace

Element TreeSitterRenderer::RenderCodeBlock(const std::string& code, const std::string& language) {
  auto& info = GetLangInfo(language);
  if (!info.language) {
    return PlainRenderer::RenderCodeBlock(code, language);
  }

  HighlightTheme theme;
  EnsureQueryCompiled(info, theme);

  TSParser* parser = ts_parser_new();
  ts_parser_set_language(parser, info.language);

  TSTree* tree = ts_parser_parse_string(parser, nullptr, code.c_str(), code.size());
  TSNode root = ts_tree_root_node(tree);

  auto spans = RunHighlightQuery(info, root, code);

  // Build colored elements line by line
  Elements lines;
  Elements current_line;

  auto flush_text = [&](const std::string& txt, ftxui::Color col) {
    std::istringstream stream(txt);
    std::string line;
    bool first = true;
    while (std::getline(stream, line)) {
      if (!first) {
        lines.push_back(hbox(std::move(current_line)));
        current_line.clear();
      }
      if (!line.empty()) {
        current_line.push_back(text(line) | color(col));
      }
      first = false;
    }
    if (!txt.empty() && txt.back() == '\n') {
      lines.push_back(hbox(std::move(current_line)));
      current_line.clear();
    }
  };

  size_t pos = 0;
  for (const auto& span : spans) {
    if (span.start > pos) {
      flush_text(code.substr(pos, span.start - pos), theme.default_);
    }
    flush_text(code.substr(span.start, span.end - span.start), span.color);
    pos = span.end;
  }
  if (pos < code.size()) {
    flush_text(code.substr(pos), theme.default_);
  }
  if (!current_line.empty()) {
    lines.push_back(hbox(std::move(current_line)));
  }

  ts_tree_delete(tree);
  ts_parser_delete(parser);

  return vbox(std::move(lines)) | border;
}

}  // namespace ally::rendering
