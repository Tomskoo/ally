#pragma once

#include <ftxui/component/screen_interactive.hpp>
#include <string>

#include "src/app/AppContext.hpp"

namespace ally::auth {

// Attempts OAuth authorization for the given provider on the current thread.
// On success, adds provider_id to ctx.connected_providers.
// Always posts a custom event to trigger re-render.
// Must be called from a background thread, not the FTXUI render thread.
void TryAuthenticateProvider(AppContext& ctx, const std::string& provider_id, ftxui::ScreenInteractive& screen);

}  // namespace ally::auth
