#include "src/services/watcher/SkillsWatcher.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace ally::watcher {

namespace {
constexpr auto kDebounceMs = std::chrono::milliseconds(300);
constexpr int kPollMs = 50;
}  // namespace

SkillsWatcher::SkillsWatcher(std::filesystem::path project_root, WatcherBroadcast<SkillsChangedEvent>& broadcast)
    : project_root_(std::move(project_root)), broadcast_(broadcast) {
  auto skills_dir = project_root_ / ".opencode" / "skills";
  std::filesystem::create_directories(skills_dir);

  // Register efsw watch on .opencode/skills/ recursively
  // Non-fatal: if registration fails, enter no-op state
  file_watcher_ = std::make_unique<efsw::FileWatcher>();
  watch_id_ = file_watcher_->addWatch(skills_dir.string(), this, true);
  if (watch_id_ < 0) {
    std::cerr << "SkillsWatcher: failed to register OS watch on " << skills_dir.string() << " — entering no-op state\n";
    file_watcher_.reset();
    return;
  }
  file_watcher_->watch();

  // Start background debounce thread
  thread_ = std::thread([this]() -> void { Run(); });
}

SkillsWatcher::~SkillsWatcher() {
  stop_.store(true);
  if (thread_.joinable()) {
    thread_.join();
  }
}

void SkillsWatcher::handleFileAction(efsw::WatchID /*watchid*/, const std::string& /*dir*/, const std::string& /*filename*/,
                                     efsw::Action /*action*/, std::string /*old_filename*/) {
  // Any event in the skills directory triggers a notification.
  // No path filtering — all changes are significant.
  std::scoped_lock lock(pending_mutex_);
  has_pending_ = true;
}

void SkillsWatcher::Run() {
  auto last_event_time = std::chrono::steady_clock::now();
  bool active_debounce = false;

  while (!stop_.load()) {
    // Check for new pending events
    {
      std::scoped_lock lock(pending_mutex_);
      if (has_pending_) {
        active_debounce = true;
        has_pending_ = false;
        last_event_time = std::chrono::steady_clock::now();
      }
    }

    // Fire after debounce window
    if (active_debounce) {
      auto elapsed = std::chrono::steady_clock::now() - last_event_time;
      if (elapsed >= kDebounceMs) {
        broadcast_.push(SkillsChangedEvent{});
        active_debounce = false;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
}

}  // namespace ally::watcher
