#pragma once

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "VimMode.hpp"

namespace ally::vim {

/// Overlay state describing what to render on top of the child element.
struct CursorOverlay {
  enum class Mode { Cursor, Selection };
  Mode mode = Mode::Cursor;

  // Cursor position (content-relative coordinates, i.e. row within the
  // scrollable content, not the screen).  For Cursor mode only cursor_* are
  // used.  For Selection mode all four are used.
  int cursor_row = 0;
  int cursor_col = 0;

  // Selection endpoints (normalised start <= end by caller).
  int sel_start_row = 0;
  int sel_start_col = 0;
  int sel_end_row = 0;
  int sel_end_col = 0;

  // If non-null, the render pass captures the text under the selection
  // from the actual screen pixels so that yank gets exactly what's highlighted.
  std::string* captured_text = nullptr;
};

/// Wrap `child` so that after it renders, the overlay inverts pixel(s) for the
/// cursor or selection.  `overlay` must outlive the returned Element (typically
/// lives on the stack of the Render() call).
inline auto make_cursor_overlay(ftxui::Element child, const CursorOverlay* overlay) -> ftxui::Element {
  class Impl : public ftxui::Node {
   public:
    Impl(ftxui::Element child, const CursorOverlay* overlay)
        : ftxui::Node({std::move(child)}), overlay_(overlay) {}

    void ComputeRequirement() override {
      children_[0]->ComputeRequirement();
      requirement_ = children_[0]->requirement();
    }

    void SetBox(ftxui::Box box) override {
      ftxui::Node::SetBox(box);
      children_[0]->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
      children_[0]->Render(screen);

      if (overlay_ == nullptr) { return; }

      // The child's box_ gives us the absolute screen coordinates.
      // Overlay rows/cols are content-relative (matching the child's layout).
      // Since the scrollable node already offsets the child, the child's
      // box_.y_min is the screen row of content row 0.
      const int base_x = box_.x_min;
      const int base_y = box_.y_min;

      if (overlay_->mode == CursorOverlay::Mode::Cursor) {
        int sx = base_x + overlay_->cursor_col;
        int sy = base_y + overlay_->cursor_row;
        InvertPixel(screen, sx, sy);
      } else {
        // Selection: invert all cells in the range and optionally capture text.
        std::string captured;
        bool capturing = (overlay_->captured_text != nullptr);

        for (int row = overlay_->sel_start_row; row <= overlay_->sel_end_row; ++row) {
          int col_begin = (row == overlay_->sel_start_row) ? overlay_->sel_start_col : 0;
          int col_end = (row == overlay_->sel_end_row) ? overlay_->sel_end_col : (box_.x_max - base_x);

          if (capturing && row > overlay_->sel_start_row) {
            captured += '\n';
          }

          std::string line;
          for (int col = col_begin; col <= col_end; ++col) {
            int sx = base_x + col;
            int sy = base_y + row;
            InvertPixel(screen, sx, sy);
            if (capturing && sx >= 0 && sx < screen.dimx() && sy >= 0 && sy < screen.dimy()) {
              const auto& ch = screen.PixelAt(sx, sy).character;
              line += ch.empty() ? " " : ch;
            }
          }

          if (capturing) {
            // Trim trailing spaces from this line.
            auto last = line.find_last_not_of(' ');
            if (last != std::string::npos) {
              line.resize(last + 1);
            } else {
              line.clear();
            }
            // Strip trailing "HH:MM:SS" timestamps from chat rendering.
            if (line.size() >= 9) {
              auto ts_start = line.size() - 8;
              bool is_timestamp = (line[ts_start - 1] == ' ') &&
                                  std::isdigit(static_cast<unsigned char>(line[ts_start + 0])) &&
                                  std::isdigit(static_cast<unsigned char>(line[ts_start + 1])) &&
                                  line[ts_start + 2] == ':' &&
                                  std::isdigit(static_cast<unsigned char>(line[ts_start + 3])) &&
                                  std::isdigit(static_cast<unsigned char>(line[ts_start + 4])) &&
                                  line[ts_start + 5] == ':' &&
                                  std::isdigit(static_cast<unsigned char>(line[ts_start + 6])) &&
                                  std::isdigit(static_cast<unsigned char>(line[ts_start + 7]));
              if (is_timestamp) {
                line.resize(ts_start);
                // Re-trim trailing spaces left by the filler.
                auto last2 = line.find_last_not_of(' ');
                if (last2 != std::string::npos) {
                  line.resize(last2 + 1);
                } else {
                  line.clear();
                }
              }
            }
            captured += line;
          }
        }

        if (capturing) {
          *overlay_->captured_text = std::move(captured);
        }

        // Also show cursor position.
        int cx = base_x + overlay_->cursor_col;
        int cy = base_y + overlay_->cursor_row;
        InvertPixel(screen, cx, cy);
      }
    }

   private:
    const CursorOverlay* overlay_;

    static void InvertPixel(ftxui::Screen& screen, int screen_x, int screen_y) {
      if (screen_x < screen.stencil.x_min || screen_x > screen.stencil.x_max ||
          screen_y < screen.stencil.y_min || screen_y > screen.stencil.y_max) {
        return;
      }
      auto& pixel = screen.PixelAt(screen_x, screen_y);
      pixel.inverted = !pixel.inverted;
    }
  };

  return std::make_shared<Impl>(std::move(child), overlay);
}

/// Extract displayable text from a region of the rendered screen.
/// Reads pixel characters, joins rows with newlines, and trims trailing spaces.
inline auto ExtractScreenText(ftxui::Screen& screen, ftxui::Box region) -> std::string {
  std::string result;
  int y_min = std::max(region.y_min, 0);
  int y_max = std::min(region.y_max, screen.dimy() - 1);
  int x_min = std::max(region.x_min, 0);
  int x_max = std::min(region.x_max, screen.dimx() - 1);

  for (int row = y_min; row <= y_max; ++row) {
    std::string line;
    for (int col = x_min; col <= x_max; ++col) {
      const auto& ch = screen.PixelAt(col, row).character;
      line += ch.empty() ? " " : ch;
    }
    // Trim trailing spaces.
    auto last = line.find_last_not_of(' ');
    if (last != std::string::npos) {
      line.resize(last + 1);
    } else {
      line.clear();
    }
    if (!result.empty()) { result += '\n'; }
    result += line;
  }
  return result;
}

}  // namespace ally::vim
