#pragma once

#include <vector>

#include "src/models/Task.hpp"
#include "src/providers/repo_file/FileTaskProvider.hpp"

namespace ally::commands::task {

auto list_tasks(const providers::FileTaskProvider& provider) -> std::vector<models::Task>;

}  // namespace ally::commands::task
