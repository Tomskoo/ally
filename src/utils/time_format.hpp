#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace ally::utils {

namespace detail {
constexpr long long kSecondsPerMinute = 60;
constexpr long long kSecondsPerHour = 3600;
constexpr long long kSecondsPerDay = 86400;
constexpr long long kSecondsPerWeek = 604800;
constexpr size_t kDateBufSize = 32;
constexpr size_t kTimeBufSize = 16;
constexpr int64_t kMillisPerSecond = 1000;
}  // namespace detail

// Format an epoch-seconds string as "Apr 11, 2026".
// Returns the raw string unchanged if parsing fails.
inline auto format_date(const std::string& epoch_secs) -> std::string {
  unsigned long long val = 0;
  try {
    val = std::stoull(epoch_secs);
  } catch (...) {
    return epoch_secs;
  }
  auto secs = static_cast<std::time_t>(val);
  std::array<char, detail::kDateBufSize> buf{};
  std::strftime(buf.data(), buf.size(), "%b %d, %Y", std::localtime(&secs));
  return buf.data();
}

// Format an epoch-seconds string as a relative time:
//   "just now", "5m ago", "3h ago", "2d ago", "Apr 5", "Dec 2025"
// Returns the raw string unchanged if parsing fails.
inline auto format_relative(const std::string& epoch_secs) -> std::string {
  unsigned long long val = 0;
  try {
    val = std::stoull(epoch_secs);
  } catch (...) {
    return epoch_secs;
  }

  auto then = static_cast<std::time_t>(val);
  auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  auto diff = static_cast<long long>(now) - static_cast<long long>(then);

  if (diff < 0) {
    return "just now";
  }
  if (diff < detail::kSecondsPerMinute) {
    return "just now";
  }
  if (diff < detail::kSecondsPerHour) {
    return std::to_string(diff / detail::kSecondsPerMinute) + "m ago";
  }
  if (diff < detail::kSecondsPerDay) {
    return std::to_string(diff / detail::kSecondsPerHour) + "h ago";
  }
  if (diff < detail::kSecondsPerWeek) {
    return std::to_string(diff / detail::kSecondsPerDay) + "d ago";
  }

  // Older than 7 days: show "Mon DD" or "Mon YYYY" if different year
  auto* now_tm = std::localtime(&now);
  int now_year = now_tm->tm_year;

  auto* then_tm = std::localtime(&then);
  int then_year = then_tm->tm_year;

  std::array<char, detail::kDateBufSize> buf{};
  if (then_year == now_year) {
    std::strftime(buf.data(), buf.size(), "%b %d", then_tm);
  } else {
    std::strftime(buf.data(), buf.size(), "%b %Y", then_tm);
  }
  return buf.data();
}

// Format epoch milliseconds as "HH:MM:SS" (for chat timestamps).
inline auto format_time_hms(int64_t epoch_ms) -> std::string {
  auto secs = static_cast<std::time_t>(epoch_ms / detail::kMillisPerSecond);
  std::array<char, detail::kTimeBufSize> buf{};
  std::strftime(buf.data(), buf.size(), "%H:%M:%S", std::localtime(&secs));
  return buf.data();
}

}  // namespace ally::utils
