#pragma once

#include <ftxui/dom/elements.hpp>

namespace ally::components {

// Scrollable viewport driven by an external scroll offset.
// Caps min_y to 0 so the element does not inflate the parent layout,
// then clips and offsets the child like yframe — but scroll position
// is controlled by the caller rather than FTXUI's focus system.
auto make_scrollable(ftxui::Element child, int* scroll_y, int* viewport_height_out = nullptr,
                     int* content_height_out = nullptr) -> ftxui::Element;

// Caps min_y to 0 without adding scroll — used with yframe for
// views that scroll via FTXUI focus (e.g. the list view).
auto cap_height(ftxui::Element child) -> ftxui::Element;

// Like ftxui::reflect() but captures the box at layout time (SetBox) without
// re-intersecting with the screen stencil during Render.  This gives the true
// content-space coordinates even for elements that are scrolled out of view.
auto reflect_layout(ftxui::Box& box) -> ftxui::Decorator;

}  // namespace ally::components
