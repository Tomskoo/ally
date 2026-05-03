#include "src/configuration/HotkeyParser.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <ftxui/component/event.hpp>
#include <yaml-cpp/yaml.h>

using ftxui::Event;

namespace ally::configuration {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

auto ToLower(std::string s) -> std::string {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

// Named keys that can appear bare (e.g. `<escape>`) or after a modifier.
// All lookups use lowercase.
auto NamedKeyTable() -> const std::unordered_map<std::string, Event>& {
  static const auto* table = new std::unordered_map<std::string, Event>{
      {"up", Event::ArrowUp},
      {"down", Event::ArrowDown},
      {"left", Event::ArrowLeft},
      {"right", Event::ArrowRight},
      {"escape", Event::Escape},
      {"esc", Event::Escape},
      {"tab", Event::Tab},
      {"enter", Event::Return},
      {"return", Event::Return},
      {"backspace", Event::Backspace},
      {"delete", Event::Delete},
      {"home", Event::Home},
      {"end", Event::End},
      {"pageup", Event::PageUp},
      {"pgup", Event::PageUp},
      {"pagedown", Event::PageDown},
      {"pgdn", Event::PageDown},
  };
  return *table;
}

// CSI suffix character for arrow/nav keys that support modifier parameters.
// Format: \x1b[1;{modifier_code}{suffix}
auto CsiSuffixTable() -> const std::unordered_map<std::string, char>& {
  static const auto* table = new std::unordered_map<std::string, char>{
      {"up", 'A'},
      {"down", 'B'},
      {"right", 'C'},
      {"left", 'D'},
      {"home", 'H'},
      {"end", 'F'},
  };
  return *table;
}

// Modifier name -> xterm modifier parameter (used in CSI sequences).
auto ModifierCode() -> const std::unordered_map<std::string, int>& {
  static const auto* table = new std::unordered_map<std::string, int>{
      {"shift", 2}, {"alt", 3}, {"ctrl", 5}, {"meta", 9},
  };
  return *table;
}

// Build an Event for ctrl+char.  Terminal convention: ctrl+letter sends
// the character code = letter - 'a' + 1.
auto CtrlChar(char ch) -> Event {
  if (ch >= 'a' && ch <= 'z') {
    return Event::Special({static_cast<char>(ch - 'a' + 1)});
  }
  throw std::runtime_error(std::string("Cannot apply <ctrl> to '") + ch +
                           "': only a-z letters are supported with <ctrl>");
}

// Build an Event for alt+char.  Terminal convention: ESC followed by the char.
auto AltChar(char ch) -> Event { return Event::Special({27, ch}); }

// Build a modified-arrow/nav CSI Event: \x1b[1;{mod}{suffix}
auto ModifiedCsi(int mod_code, char suffix) -> Event {
  std::string seq = "\x1b[1;";
  seq += std::to_string(mod_code);
  seq += suffix;
  return Event::Special(seq);
}

// ---------------------------------------------------------------------------
// Tokeniser: split a spec like "<ctrl>+j" into modifier tokens and a key
// token.  The `+` is the separator, but `+` itself can be the key if it
// appears as the last token (e.g. "<ctrl>++", or bare "+").
// ---------------------------------------------------------------------------

struct ParsedSpec {
  std::vector<std::string> modifiers;  // lowercase
  std::string key;                     // original case preserved
};

auto Tokenise(const std::string& spec) -> ParsedSpec {
  if (spec.empty()) {
    throw std::runtime_error("Empty hotkey string");
  }

  ParsedSpec result;

  // Collect segments split on '+', but keep the final '+' as a key if the
  // string ends with '+' (e.g. "<ctrl>++" means ctrl + the '+' char).
  std::vector<std::string> parts;
  std::string current;
  for (size_t i = 0; i < spec.size(); ++i) {
    if (spec[i] == '+' && i > 0) {
      if (!current.empty()) {
        parts.push_back(std::move(current));
        current.clear();
      }
      // If this is the last character, the key is '+'.
      if (i + 1 == spec.size()) {
        parts.emplace_back("+");
      }
    } else {
      current += spec[i];
    }
  }
  if (!current.empty()) {
    parts.push_back(std::move(current));
  }

  if (parts.empty()) {
    throw std::runtime_error("Cannot parse hotkey '" + spec + "'");
  }

  // Last part is the key; everything before is a modifier.
  result.key = parts.back();
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    auto mod = parts[i];
    // Strip surrounding angle brackets: <ctrl> -> ctrl
    if (mod.size() >= 2 && mod.front() == '<' && mod.back() == '>') {
      mod = mod.substr(1, mod.size() - 2);
    }
    result.modifiers.push_back(ToLower(mod));
  }

  return result;
}

// ---------------------------------------------------------------------------
// Action map: maps "category.action" strings to EventBinding pointers.
// ---------------------------------------------------------------------------

auto BuildActionMap(InputConfig& config)
    -> std::unordered_map<std::string, EventBinding*> {
  return {
      // Chat
      {"chat.next_message", &config.chat.next_message},
      {"chat.prev_message", &config.chat.prev_message},
      {"chat.next_user_message", &config.chat.next_user_message},
      {"chat.prev_user_message", &config.chat.prev_user_message},
      {"chat.send_message", &config.chat.send_message},
      {"chat.toggle_panel", &config.chat.toggle_panel},
      {"chat.new_session", &config.chat.new_session},
      {"chat.prev_session", &config.chat.prev_session},
      {"chat.next_session", &config.chat.next_session},
      // Navigation
      {"navigation.cycle_right", &config.navigation.cycle_right},
      {"navigation.cycle_left", &config.navigation.cycle_left},
      {"navigation.escape", &config.navigation.escape},
      {"navigation.focus_provider", &config.navigation.focus_provider},
      {"navigation.toggle_quick_chat", &config.navigation.toggle_quick_chat},
      // Vim
      {"vim.up", &config.vim.up},
      {"vim.down", &config.vim.down},
      {"vim.left", &config.vim.left},
      {"vim.right", &config.vim.right},
      {"vim.enter_insert", &config.vim.enter_insert},
      {"vim.enter_visual", &config.vim.enter_visual},
      {"vim.exit_visual", &config.vim.exit_visual},
      {"vim.yank", &config.vim.yank},
      {"vim.dirty_yank", &config.vim.dirty_yank},
      // Artifact
      {"artifact.scroll_up", &config.artifact.scroll_up},
      {"artifact.scroll_down", &config.artifact.scroll_down},
      {"artifact.toggle_render", &config.artifact.toggle_render},
      {"artifact.force_reload", &config.artifact.force_reload},
      // Autocomplete
      {"autocomplete.next", &config.autocomplete.next},
      {"autocomplete.prev", &config.autocomplete.prev},
      {"autocomplete.dismiss", &config.autocomplete.dismiss},
      {"autocomplete.select", &config.autocomplete.select},
  };
}

auto ValidActionList(
    const std::unordered_map<std::string, EventBinding*>& actions)
    -> std::string {
  std::vector<std::string> names;
  names.reserve(actions.size());
  for (const auto& [name, _] : actions) {
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  std::string out;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0) out += ", ";
    out += names[i];
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// ParseHotkey
// ---------------------------------------------------------------------------

auto ParseHotkey(const std::string& spec) -> Event {
  auto parsed = Tokenise(spec);

  // Resolve the key portion.
  auto key_lower = ToLower(parsed.key);

  // Strip angle brackets from the key if present (e.g. `<escape>`).
  std::string key_name = key_lower;
  if (key_name.size() >= 2 && key_name.front() == '<' && key_name.back() == '>') {
    key_name = key_name.substr(1, key_name.size() - 2);
  }

  const auto& named_keys = NamedKeyTable();
  const auto& csi_suffixes = CsiSuffixTable();
  const auto& mod_codes = ModifierCode();

  // --- No modifiers ---
  if (parsed.modifiers.empty()) {
    // Named key?
    if (auto it = named_keys.find(key_name); it != named_keys.end()) {
      return it->second;
    }
    // Single character?
    if (parsed.key.size() == 1) {
      return Event::Character(parsed.key[0]);
    }
    throw std::runtime_error(
        "Cannot parse hotkey '" + spec +
        "': '" + parsed.key +
        "' is not a recognized key name. Use a single character or a named "
        "key like <up>, <escape>, <tab>, <enter>, etc.");
  }

  // --- With modifiers ---
  // Validate all modifiers and compute combined modifier code.
  // For simplicity we only support a single modifier for now; combining
  // multiple modifiers works for CSI keys via xterm encoding.
  int combined_mod = 0;
  for (const auto& mod : parsed.modifiers) {
    auto it = mod_codes.find(mod);
    if (it == mod_codes.end()) {
      throw std::runtime_error(
          "Cannot parse hotkey '" + spec + "': '<" + mod +
          ">' is not a recognized modifier. "
          "Valid modifiers: <ctrl>, <shift>, <alt>, <meta>");
    }
    // xterm modifier parameter encoding: params are additive with base 1.
    // shift=2, alt=3, ctrl=5 means shift=+1, alt=+2, ctrl=+4 from base 1.
    // To combine: sum the individual offsets, then add 1.
    // Individual offset = code - 1.
    combined_mod += (it->second - 1);
  }
  combined_mod += 1;  // Add back the base.

  bool has_shift = std::find(parsed.modifiers.begin(), parsed.modifiers.end(),
                             "shift") != parsed.modifiers.end();
  bool has_ctrl = std::find(parsed.modifiers.begin(), parsed.modifiers.end(),
                            "ctrl") != parsed.modifiers.end();
  bool has_alt = std::find(parsed.modifiers.begin(), parsed.modifiers.end(),
                           "alt") != parsed.modifiers.end();

  // Named key with modifier -> CSI sequence if available.
  if (auto csi_it = csi_suffixes.find(key_name); csi_it != csi_suffixes.end()) {
    return ModifiedCsi(combined_mod, csi_it->second);
  }

  // Tab with shift -> Event::TabReverse
  if (key_name == "tab" && has_shift && !has_ctrl && !has_alt) {
    return Event::TabReverse;
  }

  // Alt + named key with a known byte value (e.g. <alt>+<enter> = ESC + CR).
  static const std::unordered_map<std::string, char> named_key_bytes = {
      {"enter", 13}, {"return", 13}, {"tab", 9}, {"escape", 27}, {"esc", 27},
      {"backspace", 127},
  };
  if (has_alt && !has_ctrl) {
    if (auto byte_it = named_key_bytes.find(key_name);
        byte_it != named_key_bytes.end()) {
      return Event::Special({27, byte_it->second});
    }
  }

  // Single character with modifier.
  if (parsed.key.size() == 1) {
    char ch = parsed.key[0];

    // shift+letter -> uppercase letter
    if (has_shift && !has_ctrl && !has_alt && std::isalpha(ch)) {
      return Event::Character(static_cast<char>(std::toupper(ch)));
    }

    // shift+non-letter is ambiguous across keyboard layouts.
    if (has_shift && !std::isalpha(ch)) {
      throw std::runtime_error(
          "Cannot parse hotkey '" + spec +
          "': <shift> with non-letter characters is layout-dependent. "
          "Use the shifted character directly (e.g. '!' instead of "
          "'<shift>+1').");
    }

    // ctrl+letter
    if (has_ctrl && !has_alt && std::isalpha(ch)) {
      return CtrlChar(static_cast<char>(std::tolower(ch)));
    }

    // alt+char
    if (has_alt && !has_ctrl) {
      return AltChar(ch);
    }

    // ctrl+alt+char
    if (has_ctrl && has_alt && std::isalpha(ch)) {
      return Event::Special({27, static_cast<char>(ch - 'a' + 1)});
    }
  }

  throw std::runtime_error(
      "Cannot parse hotkey '" + spec +
      "': unsupported modifier + key combination");
}

// ---------------------------------------------------------------------------
// ApplyHotkeyOverrides
// ---------------------------------------------------------------------------

auto ApplyHotkeyOverrides(const std::filesystem::path& project_root,
                          InputConfig& config) -> std::optional<ConfigError> {
  auto config_path = project_root / ".ally" / "config.yaml";
  if (!std::filesystem::exists(config_path)) {
    return std::nullopt;
  }

  YAML::Node root;
  try {
    root = YAML::LoadFile(config_path.string());
  } catch (const YAML::Exception& e) {
    return ConfigError{config_path.string(),
                       "Failed to parse YAML: " + std::string(e.what())};
  }

  auto hotkeys = root["hotkeys"];
  if (!hotkeys || !hotkeys.IsMap()) {
    return std::nullopt;  // No hotkeys section or not a map — nothing to do.
  }

  auto action_map = BuildActionMap(config);

  for (const auto& entry : hotkeys) {
    auto action_name = entry.first.as<std::string>();
    auto it = action_map.find(action_name);
    if (it == action_map.end()) {
      return ConfigError{
          config_path.string(),
          "Unknown hotkey action '" + action_name +
              "'. Valid actions: " + ValidActionList(action_map)};
    }

    if (!entry.second.IsSequence()) {
      return ConfigError{
          config_path.string(),
          "Hotkey action '" + action_name + "' must be a list of hotkey strings"};
    }

    std::vector<Event> events;
    events.reserve(entry.second.size());
    for (const auto& key_node : entry.second) {
      auto key_str = key_node.as<std::string>();
      try {
        events.push_back(ParseHotkey(key_str));
      } catch (const std::runtime_error& e) {
        return ConfigError{
            config_path.string(),
            "In action '" + action_name + "': " + std::string(e.what())};
      }
    }

    it->second->events = std::move(events);
  }

  return std::nullopt;
}

}  // namespace ally::configuration
