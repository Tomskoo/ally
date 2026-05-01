#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ally {

enum class FocusTarget : std::uint8_t { Navbar, MainView, ProviderBar, CommandBar };

struct FocusState {
  FocusTarget target = FocusTarget::MainView;
  std::optional<size_t> navbar_cursor = std::nullopt;
};

}  // namespace ally
