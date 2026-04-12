#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "efsw/efsw.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

namespace ally::watcher {

class SkillsWatcher : public efsw::FileWatchListener {
 public:
  SkillsWatcher(std::filesystem::path project_root, WatcherBroadcast<SkillsChangedEvent>& broadcast);
  ~SkillsWatcher() override;

  SkillsWatcher(const SkillsWatcher&) = delete;
  auto operator=(const SkillsWatcher&) -> SkillsWatcher& = delete;

 private:
  void handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action,
                        std::string old_filename) override;

  void Run();

  std::filesystem::path project_root_;
  WatcherBroadcast<SkillsChangedEvent>& broadcast_;

  std::mutex pending_mutex_;
  bool has_pending_{false};

  std::unique_ptr<efsw::FileWatcher> file_watcher_;
  efsw::WatchID watch_id_{0};
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

}  // namespace ally::watcher
