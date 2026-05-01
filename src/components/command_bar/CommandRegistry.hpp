#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ally::components {

struct CommandRegistration {
  std::string name;
  std::string description;
  std::function<void(const std::string& args)> handler;
};

class CommandRegistry {
 public:
  void Register(CommandRegistration reg);

  /// Returns all registrations whose name starts with or fuzzy-matches prefix.
  auto Match(const std::string& prefix) const -> std::vector<const CommandRegistration*>;

  /// Parses "name args" from raw_input and dispatches to the matching handler.
  /// Returns true if a command was found and executed.
  auto Execute(const std::string& raw_input) -> bool;

  auto GetAll() const -> const std::vector<CommandRegistration>& { return commands_; }

 private:
  std::vector<CommandRegistration> commands_;
};

}  // namespace ally::components
