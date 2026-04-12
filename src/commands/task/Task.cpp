#include "Task.hpp"

namespace ally::commands::task {

auto list_tasks(const providers::FileTaskProvider& provider) -> std::vector<models::Task> { return provider.list_tasks(); }

}  // namespace ally::commands::task
