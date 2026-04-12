#pragma once

#include <ftxui/component/component.hpp>
#include <optional>
#include <string>

#include "src/app/AppContext.hpp"
#include "src/app/Navigator.hpp"

namespace ally::views {

auto workflow_form(AppContext& ctx, Navigator& nav, std::optional<std::string> workflow_id) -> ftxui::Component;

}  // namespace ally::views
