#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>

#include "efsw/efsw.hpp"
#include "src/services/watcher/WatcherBroadcast.hpp"
#include "src/services/watcher/WatcherEvents.hpp"

namespace ally::watcher {

class ArtifactWatcher : public efsw::FileWatchListener {
 public:
  ArtifactWatcher(std::filesystem::path project_root, WatcherBroadcast<ArtifactChangedEvent>& broadcast);
  ~ArtifactWatcher() override;

  ArtifactWatcher(const ArtifactWatcher&) = delete;
  auto operator=(const ArtifactWatcher&) -> ArtifactWatcher& = delete;

 private:
  void handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action,
                        std::string old_filename) override;

  void Run();
  void ProcessPending();

  static auto ParseArtifactPath(const std::filesystem::path& project_root, const std::filesystem::path& abs_path)
      -> std::optional<std::tuple<std::string, std::string, std::string>>;

  std::filesystem::path project_root_;
  WatcherBroadcast<ArtifactChangedEvent>& broadcast_;

  std::mutex known_mutex_;
  std::unordered_set<std::string> known_;

  std::mutex pending_mutex_;
  std::unordered_set<std::string> pending_paths_;
  bool new_events_{false};  // set by handleFileAction, cleared by Run()

  std::unique_ptr<efsw::FileWatcher> file_watcher_;
  efsw::WatchID watch_id_{0};
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

}  // namespace ally::watcher
