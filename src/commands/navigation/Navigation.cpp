#include "src/commands/navigation/Navigation.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/app/NavState.hpp"
#include "src/models/Task.hpp"

namespace ally::commands::navigation {

namespace {

auto FuzzyMatch(std::string_view query, std::string_view target) -> bool {
  if (query.empty()) {
    return true;
  }
  size_t qi = 0;
  for (char c : target) {
    if (std::tolower(static_cast<unsigned char>(c)) == std::tolower(static_cast<unsigned char>(query[qi]))) {
      ++qi;
      if (qi == query.size()) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

void RegisterCommands(components::CommandRegistry& registry, AppContext& ctx, Navigator& nav) {
  registry.Register({"board", "Go to the task board", [&nav](const std::string&) { nav.go(BoardState{}); }});

  registry.Register({"back", "Navigate back", [&nav](const std::string&) { nav.back(); }});

  registry.Register({"forward", "Navigate forward", [&nav](const std::string&) { nav.forward(); }});

  registry.Register({"workflows", "Go to the workflow list", [&nav](const std::string&) { nav.go(WorkflowsState{}); }});

  registry.Register({"quickchat", "Toggle quick chat", [&nav](const std::string&) {
                        if (std::holds_alternative<QuickChatState>(nav.current())) {
                          nav.back();
                        } else {
                          nav.go(QuickChatState{});
                        }
                      }});

  registry.Register({"task", "Jump to a task by name",
                      [&ctx, &nav](const std::string& args) {
                        if (args.empty()) {
                          ctx.SetStatus("Usage: :task <name>");
                          return;
                        }
                        auto tasks = ctx.task_provider.list_tasks();
                        // Exact match on id or name first.
                        for (const auto& t : tasks) {
                          if (t.id == args || t.name == args) {
                            nav.go(TaskDetailState{t.id});
                            return;
                          }
                        }
                        // Fuzzy match.
                        std::vector<const models::Task*> matches;
                        for (const auto& t : tasks) {
                          if (FuzzyMatch(args, t.name) || FuzzyMatch(args, t.id)) {
                            matches.push_back(&t);
                          }
                        }
                        if (matches.size() == 1) {
                          nav.go(TaskDetailState{matches[0]->id});
                        } else if (matches.empty()) {
                          ctx.SetStatus("No task matching: " + args);
                        } else {
                          ctx.SetStatus("Ambiguous: " + std::to_string(matches.size()) + " tasks match");
                        }
                      },
                      [&ctx]() -> std::vector<std::pair<std::string, std::string>> {
                        auto tasks = ctx.task_provider.list_tasks();
                        std::vector<std::pair<std::string, std::string>> result;
                        result.reserve(tasks.size());
                        for (const auto& t : tasks) {
                          result.emplace_back(t.name, t.description.value_or(""));
                        }
                        return result;
                      }});

  registry.Register({"thread", "Jump to a thread in current task",
                      [&ctx, &nav](const std::string& args) {
                        if (!ctx.current_task) {
                          ctx.SetStatus("No current task");
                          return;
                        }
                        if (args.empty()) {
                          ctx.SetStatus("Usage: :thread <name>");
                          return;
                        }
                        const auto& task = *ctx.current_task;
                        // Exact match.
                        for (const auto& th : task.threads) {
                          if (th.id == args || th.name == args) {
                            nav.go(StageViewState{task.id, th.id, th.current_stage});
                            return;
                          }
                        }
                        // Fuzzy match.
                        std::vector<const models::Thread*> matches;
                        for (const auto& th : task.threads) {
                          if (FuzzyMatch(args, th.name) || FuzzyMatch(args, th.id)) {
                            matches.push_back(&th);
                          }
                        }
                        if (matches.size() == 1) {
                          nav.go(StageViewState{task.id, matches[0]->id, matches[0]->current_stage});
                        } else if (matches.empty()) {
                          ctx.SetStatus("No thread matching: " + args);
                        } else {
                          ctx.SetStatus("Ambiguous: " + std::to_string(matches.size()) + " threads match");
                        }
                      },
                      [&ctx]() -> std::vector<std::pair<std::string, std::string>> {
                        if (!ctx.current_task) return {};
                        std::vector<std::pair<std::string, std::string>> result;
                        for (const auto& th : ctx.current_task->threads) {
                          result.emplace_back(th.name, ctx.current_task->description.value_or(""));
                        }
                        return result;
                      }});

  registry.Register({"stage", "Jump to a stage in current thread",
                      [&ctx, &nav](const std::string& args) {
                        if (args.empty()) {
                          ctx.SetStatus("Usage: :stage <slug>");
                          return;
                        }
                        auto* sv = std::get_if<StageViewState>(&nav.current());
                        if (!sv) {
                          ctx.SetStatus("Not in a thread view");
                          return;
                        }
                        if (!ctx.current_task) {
                          ctx.SetStatus("No current task");
                          return;
                        }
                        for (const auto& th : ctx.current_task->threads) {
                          if (th.id != sv->thread_id) {
                            continue;
                          }
                          // Exact match.
                          if (th.stage_sessions.count(args)) {
                            nav.go(StageViewState{sv->task_id, sv->thread_id, args});
                            return;
                          }
                          // Fuzzy match stage names.
                          std::vector<std::string> stage_matches;
                          for (const auto& [stage_name, _] : th.stage_sessions) {
                            if (FuzzyMatch(args, stage_name)) {
                              stage_matches.push_back(stage_name);
                            }
                          }
                          if (stage_matches.size() == 1) {
                            nav.go(StageViewState{sv->task_id, sv->thread_id, stage_matches[0]});
                            return;
                          } else if (stage_matches.size() > 1) {
                            ctx.SetStatus("Ambiguous: " + std::to_string(stage_matches.size()) + " stages match");
                            return;
                          }
                          break;
                        }
                        ctx.SetStatus("No stage matching: " + args);
                      },
                      [&ctx, &nav]() -> std::vector<std::pair<std::string, std::string>> {
                        auto* sv = std::get_if<StageViewState>(&nav.current());
                        if (!sv || !ctx.current_task) return {};
                        for (const auto& th : ctx.current_task->threads) {
                          if (th.id == sv->thread_id) {
                            std::vector<std::pair<std::string, std::string>> result;
                            for (const auto& [stage_name, sessions] : th.stage_sessions) {
                              result.emplace_back(stage_name, std::to_string(sessions.size()) + " sessions");
                            }
                            return result;
                          }
                        }
                        return {};
                      }});

  registry.Register(
      {"help", "Show available commands",
       [&ctx](const std::string&) { ctx.SetStatus("Commands: :q :board :back :forward :task :thread :stage :workflows :quickchat"); }});
}

}  // namespace ally::commands::navigation
