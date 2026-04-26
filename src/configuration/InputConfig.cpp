#include "src/configuration/InputConfig.hpp"

using ftxui::Event;

namespace ally::configuration {

auto DefaultInputConfig() -> InputConfig {
  InputConfig c;

  // -- Chat -------------------------------------------------------------------
  c.chat.next_message       = {{Event::Character('J'), Event::Special("\x1b[1;2B")}};
  c.chat.prev_message       = {{Event::Character('K'), Event::Special("\x1b[1;2A")}};
  c.chat.next_user_message  = {{Event::AltJ, Event::Special("\x1b[1;3B")}};
  c.chat.prev_user_message  = {{Event::AltK, Event::Special("\x1b[1;3A")}};
  c.chat.send_message       = {{Event::Special({27, 13})}};
  c.chat.toggle_panel       = {{Event::Tab, Event::TabReverse}};

  // -- Navigation -------------------------------------------------------------
  c.navigation.cycle_right       = {{Event::Character(']')}};
  c.navigation.cycle_left        = {{Event::Character('[')}};
  c.navigation.escape            = {{Event::Escape}};
  c.navigation.focus_provider    = {{Event::Special({27, 'p'})}};
  c.navigation.toggle_quick_chat = {{Event::Special({27, 'w'})}};

  // -- Vim --------------------------------------------------------------------
  c.vim.up            = {{Event::Character('k'), Event::ArrowUp}};
  c.vim.down          = {{Event::Character('j'), Event::ArrowDown}};
  c.vim.left          = {{Event::Character('h'), Event::ArrowLeft}};
  c.vim.right         = {{Event::Character('l'), Event::ArrowRight}};
  c.vim.enter_insert  = {{Event::Character('i')}};
  c.vim.enter_visual  = {{Event::Character('v')}};
  c.vim.exit_visual   = {{Event::Escape, Event::Character('n'), Event::Character('v')}};
  c.vim.yank          = {{Event::Character('y')}};

  // -- Artifact ---------------------------------------------------------------
  c.artifact.scroll_up     = {{Event::ArrowUp}};
  c.artifact.scroll_down   = {{Event::ArrowDown}};
  c.artifact.toggle_render = {{Event::Character('r')}};
  c.artifact.force_reload  = {{Event::Character('R')}};

  // -- Autocomplete -----------------------------------------------------------
  c.autocomplete.next    = {{Event::ArrowDown}};
  c.autocomplete.prev    = {{Event::ArrowUp}};
  c.autocomplete.dismiss = {{Event::Escape}};
  c.autocomplete.select  = {{Event::Return, Event::Tab}};

  return c;
}

}  // namespace ally::configuration
