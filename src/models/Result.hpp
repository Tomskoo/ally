#pragma once

#include <string>
#include <variant>

namespace ally::models {

template <typename T>
using Result = std::variant<T, std::string>;

template <typename T>
auto is_ok(const Result<T>& result) -> bool {
  return std::holds_alternative<T>(result);
}

template <typename T>
auto get_value(const Result<T>& result) -> const T& {
  return std::get<T>(result);
}

template <typename T>
auto get_error(const Result<T>& result) -> const std::string& {
  return std::get<std::string>(result);
}

}  // namespace ally::models
