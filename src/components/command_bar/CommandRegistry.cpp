#include "src/components/command_bar/CommandRegistry.hpp"

#include <algorithm>
#include <cctype>

namespace ally::components {

namespace {

auto ToLower(std::string_view s) -> std::string {
  std::string result(s);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
  return result;
}

/// Case-insensitive subsequence match (same strategy as FileAutocomplete).
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

void CommandRegistry::Register(CommandRegistration reg) {
  commands_.push_back(std::move(reg));
}

auto CommandRegistry::Match(const std::string& prefix) const -> std::vector<const CommandRegistration*> {
  std::vector<const CommandRegistration*> result;
  for (const auto& cmd : commands_) {
    if (FuzzyMatch(prefix, cmd.name)) {
      result.push_back(&cmd);
    }
  }
  return result;
}

auto CommandRegistry::FindExact(const std::string& name) const -> const CommandRegistration* {
  auto lower_name = ToLower(name);
  for (const auto& cmd : commands_) {
    if (ToLower(cmd.name) == lower_name) {
      return &cmd;
    }
  }
  return nullptr;
}

auto CommandRegistry::Execute(const std::string& raw_input) -> bool {
  if (raw_input.empty()) {
    return false;
  }

  // Split into command name and args at first space.
  std::string name;
  std::string args;
  auto space = raw_input.find(' ');
  if (space == std::string::npos) {
    name = raw_input;
  } else {
    name = raw_input.substr(0, space);
    // Trim leading whitespace from args.
    size_t args_start = raw_input.find_first_not_of(' ', space);
    if (args_start != std::string::npos) {
      args = raw_input.substr(args_start);
    }
  }

  auto lower_name = ToLower(name);

  // Exact match first.
  for (const auto& cmd : commands_) {
    if (ToLower(cmd.name) == lower_name) {
      cmd.handler(args);
      return true;
    }
  }

  // Unique prefix match.
  std::vector<const CommandRegistration*> matches;
  for (const auto& cmd : commands_) {
    auto lower_cmd = ToLower(cmd.name);
    if (lower_cmd.size() >= lower_name.size() && lower_cmd.compare(0, lower_name.size(), lower_name) == 0) {
      matches.push_back(&cmd);
    }
  }
  if (matches.size() == 1) {
    matches[0]->handler(args);
    return true;
  }

  return false;
}

}  // namespace ally::components
