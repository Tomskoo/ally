#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "FocusState.hpp"
#include "NavState.hpp"

namespace ally {

class Navigator {
 public:
  explicit Navigator(std::function<void()> rebuild) : rebuild_(std::move(rebuild)) {}

  void go(NavState new_state) {
    history_.push_back(current_);
    current_ = std::move(new_state);
    scroll_y = 0;
    reset_focus();
    rebuild_();
  }

  void replace(NavState new_state) {
    current_ = std::move(new_state);
    scroll_y = 0;
    reset_focus();
    rebuild_();
  }

  void back() {
    if (history_.empty()) {
      return;
    }
    current_ = history_.back();
    history_.pop_back();
    scroll_y = 0;
    reset_focus();
    rebuild_();
  }

  [[nodiscard]] auto current() const -> const NavState& { return current_; }
  [[nodiscard]] auto history() const -> const std::vector<NavState>& { return history_; }

  // Focus management
  void set_focus(FocusTarget target) {
    focus_.target = target;
    if (target != FocusTarget::Navbar) {
      focus_.navbar_cursor = std::nullopt;
    }
  }

  [[nodiscard]] auto focus_target() const -> FocusTarget { return focus_.target; }

  [[nodiscard]] auto navbar_cursor() const -> std::optional<size_t> { return focus_.navbar_cursor; }

  void set_navbar_cursor(std::optional<size_t> idx) { focus_.navbar_cursor = idx; }

  [[nodiscard]] auto navbar_element_count() const -> size_t { return navbar_element_count_; }

  void set_navbar_element_count(size_t n) { navbar_element_count_ = n; }

  int scroll_y = 0;

 private:
  void reset_focus() {
    focus_.target = FocusTarget::MainView;
    focus_.navbar_cursor = std::nullopt;
  }

  NavState current_{BoardState{}};
  std::vector<NavState> history_;
  std::function<void()> rebuild_;
  FocusState focus_;
  size_t navbar_element_count_ = 0;
};

}  // namespace ally
