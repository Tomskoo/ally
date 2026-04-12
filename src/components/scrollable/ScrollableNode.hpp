#pragma once

#include <ftxui/dom/elements.hpp>

namespace ally::components {

// Scrollable viewport driven by an external scroll offset.
// Caps min_y to 0 so the element does not inflate the parent layout,
// then clips and offsets the child like yframe — but scroll position
// is controlled by the caller rather than FTXUI's focus system.
auto make_scrollable(ftxui::Element child, int* scroll_y) -> ftxui::Element;

// Caps min_y to 0 without adding scroll — used with yframe for
// views that scroll via FTXUI focus (e.g. the list view).
auto cap_height(ftxui::Element child) -> ftxui::Element;

}  // namespace ally::components
