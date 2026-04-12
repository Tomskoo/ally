#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "nlohmann/json.hpp"

namespace ally::opencode {

enum class OpenCodeErrorKind : std::uint8_t {
  BinaryNotFound,
  NotRunning,
  StartFailed,
  HealthTimeout,
  Crashed,
  Http,
  ApiError,
  Sse,
};

struct OpenCodeError {
  OpenCodeErrorKind kind;
  std::string message;

  [[nodiscard]] auto ToJson() const -> nlohmann::json;
};

inline auto OpenCodeError::ToJson() const -> nlohmann::json {
  auto code = [&]() -> std::string {
    switch (kind) {
      case OpenCodeErrorKind::BinaryNotFound:
        return "binary_not_found";
      case OpenCodeErrorKind::NotRunning:
        return "not_running";
      case OpenCodeErrorKind::StartFailed:
        return "start_failed";
      case OpenCodeErrorKind::HealthTimeout:
        return "health_timeout";
      case OpenCodeErrorKind::Crashed:
        return "crashed";
      case OpenCodeErrorKind::Http:
        return "http";
      case OpenCodeErrorKind::ApiError:
        return "api_error";
      case OpenCodeErrorKind::Sse:
        return "sse";
    }
  }();
  return {{"code", code}, {"message", message}};
}

inline void to_json(nlohmann::json& json, const OpenCodeError& err) { json = err.ToJson(); }

inline void from_json(const nlohmann::json& json, OpenCodeError& err) {
  auto code = json.at("code").get<std::string>();
  if (code == "binary_not_found") {
    err.kind = OpenCodeErrorKind::BinaryNotFound;
  } else if (code == "not_running") {
    err.kind = OpenCodeErrorKind::NotRunning;
  } else if (code == "start_failed") {
    err.kind = OpenCodeErrorKind::StartFailed;
  } else if (code == "health_timeout") {
    err.kind = OpenCodeErrorKind::HealthTimeout;
  } else if (code == "crashed") {
    err.kind = OpenCodeErrorKind::Crashed;
  } else if (code == "http") {
    err.kind = OpenCodeErrorKind::Http;
  } else if (code == "api_error") {
    err.kind = OpenCodeErrorKind::ApiError;
  } else if (code == "sse") {
    err.kind = OpenCodeErrorKind::Sse;
  }
  json.at("message").get_to(err.message);
}

template <typename T>
using Result = std::variant<T, OpenCodeError>;

template <typename T>
auto is_ok(const Result<T>& result) -> bool {
  return std::holds_alternative<T>(result);
}

template <typename T>
auto get_value(const Result<T>& result) -> const T& {
  return std::get<T>(result);
}

template <typename T>
auto get_value(Result<T>&& result) -> T&& {
  return std::get<T>(std::move(result));
}

template <typename T>
auto get_error(const Result<T>& result) -> const OpenCodeError& {
  return std::get<OpenCodeError>(result);
}

}  // namespace ally::opencode
