#include "Auth.hpp"

#include <algorithm>
#include <ftxui/component/event.hpp>
#include <shared_mutex>

#include "src/opencode/Error.hpp"
#include "src/opencode/Service.hpp"

namespace ally::auth {

void TryAuthenticateProvider(AppContext& ctx, const std::string& provider_id, ftxui::ScreenInteractive& screen) {
  auto result = opencode::ProviderOAuthAuthorize(ctx.opencode_state, ctx.opencode_mutex, provider_id);

  if (opencode::is_ok(result)) {
    std::unique_lock lock(ctx.provider_mutex);
    auto& connected = ctx.connected_providers;
    if (std::find(connected.begin(), connected.end(), provider_id) == connected.end()) {
      connected.push_back(provider_id);
    }
  }

  screen.PostEvent(ftxui::Event::Custom);
}

}  // namespace ally::auth
