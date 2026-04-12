#include "src/opencode/Lifecycle.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <csignal>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "src/opencode/Client.hpp"

extern char** environ;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace ally::opencode::lifecycle {

namespace {

constexpr int kMaxSpawnRetries = 3;
constexpr int kShutdownGracePeriodMs = 5000;
constexpr int kShutdownPollIntervalMs = 100;
constexpr std::chrono::milliseconds kInitialBackoff{100};
constexpr std::chrono::milliseconds kMaxBackoff{2000};

auto FindInPath(const std::string& name) -> std::optional<std::filesystem::path> {
  const char* path_env = std::getenv("PATH");  // NOLINT(concurrency-mt-unsafe)
  if (path_env == nullptr) {
    return std::nullopt;
  }

  std::string path_str(path_env);
  std::string::size_type start = 0;
  while (start < path_str.size()) {
    auto end = path_str.find(':', start);
    if (end == std::string::npos) {
      end = path_str.size();
    }
    auto dir = path_str.substr(start, end - start);
    auto candidate = std::filesystem::path(dir) / name;
    if (std::filesystem::exists(candidate) && access(candidate.c_str(), X_OK) == 0) {
      return candidate;
    }
    start = end + 1;
  }
  return std::nullopt;
}

auto BindRandomPort() -> Result<int> {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return OpenCodeError{OpenCodeErrorKind::StartFailed, "Failed to create socket: " + std::string(strerror(errno))};
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;

  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    close(sock);
    return OpenCodeError{OpenCodeErrorKind::StartFailed, "Failed to bind socket: " + std::string(strerror(errno))};
  }

  socklen_t addr_len = sizeof(addr);
  if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addr_len) <
      0) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    close(sock);
    return OpenCodeError{OpenCodeErrorKind::StartFailed, "Failed to get socket name: " + std::string(strerror(errno))};
  }

  int port = ntohs(addr.sin_port);
  close(sock);
  return port;
}

}  // namespace

auto FindBinary() -> Result<std::filesystem::path> {
  auto path = FindInPath("opencode");
  if (!path) {
    return OpenCodeError{OpenCodeErrorKind::BinaryNotFound, "opencode binary not found in PATH"};
  }
  return *path;
}

auto Spawn(const SpawnArgs& args) -> Result<OpenCodeProcess> {
  auto binary_result = FindBinary();
  if (!is_ok(binary_result)) {
    return get_error(binary_result);
  }
  const auto& binary_path = get_value(binary_result);

  for (int attempt = 0; attempt < kMaxSpawnRetries; ++attempt) {
    auto port_result = BindRandomPort();
    if (!is_ok(port_result)) {
      return get_error(port_result);
    }
    int port = get_value(port_result);
    auto port_str = std::to_string(port);
    auto base_url = "http://127.0.0.1:" + port_str;

    std::vector<std::string> argv_strings;
    argv_strings.push_back(binary_path.string());
    argv_strings.emplace_back("serve");
    argv_strings.emplace_back("--port");
    argv_strings.push_back(port_str);
    argv_strings.emplace_back("--hostname");
    argv_strings.emplace_back("127.0.0.1");
    for (const auto& arg : args.extra_args) {
      argv_strings.push_back(arg);
    }

    std::vector<char*> argv;
    argv.reserve(argv_strings.size() + 1);
    for (auto& str : argv_strings) {
      argv.push_back(str.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_addopen(&file_actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&file_actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = 0;
    int spawn_result = posix_spawn(&pid, binary_path.c_str(), &file_actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&file_actions);

    if (spawn_result != 0) {
      if (attempt == kMaxSpawnRetries - 1) {
        return OpenCodeError{OpenCodeErrorKind::StartFailed, "posix_spawn failed after " + std::to_string(kMaxSpawnRetries) +
                                                                 " attempts: " + std::string(strerror(spawn_result))};
      }
      continue;
    }

    auto health_result = WaitForHealth(base_url);
    if (is_ok(health_result)) {
      return OpenCodeProcess{.pid = pid, .port = port, .base_url = base_url};
    }

    // Health check failed — kill the process and retry
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);

    if (attempt == kMaxSpawnRetries - 1) {
      return get_error(health_result);
    }
  }

  return OpenCodeError{OpenCodeErrorKind::StartFailed, "Failed to spawn opencode after retries"};
}

auto WaitForHealth(const std::string& base_url, std::chrono::seconds timeout) -> Result<HealthResponse> {
  auto start = std::chrono::steady_clock::now();
  auto backoff = kInitialBackoff;

  while (true) {
    auto client = OpenCodeClient::Create(base_url);
    auto result = client.Health();
    if (is_ok(result)) {
      return result;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed >= timeout) {
      return OpenCodeError{OpenCodeErrorKind::HealthTimeout,
                           "Health check timed out after " + std::to_string(timeout.count()) + "s"};
    }

    std::this_thread::sleep_for(backoff);
    backoff = std::min(backoff * 2, kMaxBackoff);
  }
}

void Shutdown(OpenCodeProcess& process) {
  if (process.pid <= 0) {
    return;
  }

  kill(process.pid, SIGTERM);

  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kShutdownGracePeriodMs);
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    pid_t result = waitpid(process.pid, &status, WNOHANG);
    if (result == process.pid || result == -1) {
      process.pid = 0;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kShutdownPollIntervalMs));
  }

  kill(process.pid, SIGKILL);
  waitpid(process.pid, nullptr, 0);
  process.pid = 0;
}

}  // namespace ally::opencode::lifecycle
