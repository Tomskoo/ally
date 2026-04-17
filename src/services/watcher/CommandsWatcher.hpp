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

class CommandsWatcher : public efsw::FileWatchListener {
 public:
  CommandsWatcher(std::filesystem::path project_root, WatcherBroadcast<CommandsChangedEvent>& broadcast);
  ~CommandsWatcher() override;

  CommandsWatcher(const CommandsWatcher&) = delete;
  auto operator=(const CommandsWatcher&) -> CommandsWatcher& = delete;

 private:
  void handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action,
                        std::string old_filename) override;

  void Run();

  std::filesystem::path project_root_;
  WatcherBroadcast<CommandsChangedEvent>& broadcast_;

  std::mutex pending_mutex_;
  bool has_pending_{false};

  std::unique_ptr<efsw::FileWatcher> file_watcher_;
  efsw::WatchID watch_id_{0};
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

}  // namespace ally::watcher
