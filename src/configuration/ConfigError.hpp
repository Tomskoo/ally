#pragma once

#include <string>

namespace ally::configuration {

struct ConfigError {
  std::string file;
  std::string message;
};

}  // namespace ally::configuration
