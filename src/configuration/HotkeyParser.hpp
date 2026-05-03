#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <ftxui/component/event.hpp>

#include "src/configuration/ConfigError.hpp"
#include "src/configuration/InputConfig.hpp"

namespace ally::configuration {

/// Parse a single hotkey string (e.g. "<ctrl>+l", "j", "<shift>+down") into an
/// ftxui::Event.  Throws std::runtime_error on invalid input.
auto ParseHotkey(const std::string& spec) -> ftxui::Event;

/// Load the `hotkeys` section from `.ally/config.yaml` and apply overrides to
/// @p config.  Actions not mentioned in the file keep their current bindings.
/// Returns a ConfigError on unknown action names or unparseable YAML; returns
/// std::nullopt on success.
auto ApplyHotkeyOverrides(const std::filesystem::path& project_root,
                          InputConfig& config) -> std::optional<ConfigError>;

}  // namespace ally::configuration
