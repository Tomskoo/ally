#include "src/opencode/Sse.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "httplib.h"
#include "nlohmann/json.hpp"

namespace ally::opencode::sse {

namespace {

constexpr std::chrono::seconds kReconnectDelay{2};

struct SseParser {
  std::string event_type;
  std::string data_buffer;
  std::string line_buffer;

  void Feed(const char* input, size_t length, const std::function<void(OpenCodeEvent)>& callback) {
    for (size_t idx = 0; idx < length; ++idx) {
      if (input[idx] == '\n') {
        ProcessLine(callback);
        line_buffer.clear();
      } else {
        line_buffer += input[idx];
      }
    }
  }

  void ProcessLine(const std::function<void(OpenCodeEvent)>& callback) {
    if (line_buffer.empty()) {
      // Empty line = end of event
      if (!data_buffer.empty()) {
        DispatchEvent(callback);
      }
      event_type.clear();
      data_buffer.clear();
      return;
    }

    constexpr std::string_view kEventPrefix = "event:";
    constexpr std::string_view kDataPrefix = "data:";

    if (line_buffer.rfind(kEventPrefix, 0) == 0) {
      event_type = line_buffer.substr(kEventPrefix.size());
      // Trim leading space
      if (!event_type.empty() && event_type.front() == ' ') {
        event_type = event_type.substr(1);
      }
    } else if (line_buffer.rfind(kDataPrefix, 0) == 0) {
      auto data_str = line_buffer.substr(kDataPrefix.size());
      if (!data_str.empty() && data_str.front() == ' ') {
        data_str = data_str.substr(1);
      }
      if (!data_buffer.empty()) {
        data_buffer += '\n';
      }
      data_buffer += data_str;
    }
  }

  void DispatchEvent(const std::function<void(OpenCodeEvent)>& callback) {
    nlohmann::json data;
    try {
      data = nlohmann::json::parse(data_buffer);
    } catch (const nlohmann::json::exception&) {
      data = nlohmann::json(data_buffer);
    }
    callback(OpenCodeEvent{.event_type = event_type, .data = std::move(data)});
  }
};

}  // namespace

void SseSubscription::Stop() {
  stop_flag.store(true);
  if (thread.joinable()) {
    thread.join();
  }
}

SseSubscription::~SseSubscription() { Stop(); }

auto SubscribeEvents(const std::string& base_url, std::function<void(OpenCodeEvent)> callback) -> std::unique_ptr<SseSubscription> {
  auto sub = std::make_unique<SseSubscription>();

  sub->thread = std::thread([base_url, callback = std::move(callback), &stop_flag = sub->stop_flag]() -> void {
    while (!stop_flag.load()) {
      httplib::Client client(base_url);

      SseParser parser;
      auto res = client.Get(
          "/event", [](const httplib::Response& response) -> bool { return response.status == 200; },
          [&parser, &callback, &stop_flag](const char* data, size_t data_length) -> bool {
            if (stop_flag.load()) {
              return false;
            }
            parser.Feed(data, data_length, callback);
            return true;
          });

      if (stop_flag.load()) {
        break;
      }

      std::this_thread::sleep_for(kReconnectDelay);
    }
  });

  return sub;
}

}  // namespace ally::opencode::sse
