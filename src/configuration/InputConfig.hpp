#pragma once

#include <algorithm>
#include <vector>

#include <ftxui/component/event.hpp>

namespace ally::configuration {

/// A binding that matches if *any* of its configured events match.
struct EventBinding {
  std::vector<ftxui::Event> events;

  auto matches(const ftxui::Event& e) const -> bool {
    return std::any_of(events.begin(), events.end(), [&e](const ftxui::Event& ev) { return ev == e; });
  }
};

struct InputConfig {
  struct Chat {
    EventBinding next_message;
    EventBinding prev_message;
    EventBinding next_user_message;
    EventBinding prev_user_message;
    EventBinding send_message;
    EventBinding toggle_panel;
  };

  struct Navigation {
    EventBinding cycle_right;
    EventBinding cycle_left;
    EventBinding escape;
    EventBinding focus_provider;
    EventBinding toggle_quick_chat;
  };

  struct Vim {
    EventBinding up;
    EventBinding down;
    EventBinding left;
    EventBinding right;
    EventBinding enter_insert;
    EventBinding enter_visual;
    EventBinding exit_visual;
    EventBinding yank;
    EventBinding dirty_yank;
  };

  struct Artifact {
    EventBinding scroll_up;
    EventBinding scroll_down;
    EventBinding toggle_render;
    EventBinding force_reload;
  };

  struct Autocomplete {
    EventBinding next;
    EventBinding prev;
    EventBinding dismiss;
    EventBinding select;
  };

  Chat chat;
  Navigation navigation;
  Vim vim;
  Artifact artifact;
  Autocomplete autocomplete;
};

/// Returns the default keybinding configuration (matches current hardcoded keys).
auto DefaultInputConfig() -> InputConfig;

}  // namespace ally::configuration
