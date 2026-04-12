#include "src/services/watcher/ArtifactWatcher.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace ally::watcher {

namespace {
constexpr auto kDebounceMs = std::chrono::milliseconds(300);
constexpr int kPollMs = 50;

// Path component indices for .ally/tasks/{task}/threads/{thread}/stages/{stage}/artifact.md
constexpr std::size_t kExpectedComponents = 8;
constexpr std::size_t kTaskIdIndex = 2;
constexpr std::size_t kThreadsIndex = 3;
constexpr std::size_t kThreadIdIndex = 4;
constexpr std::size_t kStagesIndex = 5;
constexpr std::size_t kStageIndex = 6;
constexpr std::size_t kFilenameIndex = 7;
}  // namespace

ArtifactWatcher::ArtifactWatcher(std::filesystem::path project_root, WatcherBroadcast<ArtifactChangedEvent>& broadcast)
    : project_root_(std::move(project_root)), broadcast_(broadcast) {
  auto tasks_dir = project_root_ / ".ally" / "tasks";
  std::filesystem::create_directories(tasks_dir);

  // Startup scan: populate known_ set with existing artifact.md files
  std::error_code scan_ec;
  for (auto it = std::filesystem::recursive_directory_iterator(tasks_dir,
                                                               std::filesystem::directory_options::skip_permission_denied, scan_ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(scan_ec)) {
    if (scan_ec) {
      continue;
    }
    if (!it->is_regular_file()) {
      continue;
    }
    if (it->path().filename() != "artifact.md") {
      continue;
    }
    auto result = ParseArtifactPath(project_root_, it->path());
    if (result) {
      known_.insert(it->path().string());
    }
  }

  // Register efsw watch on .ally/ recursively
  auto ally_dir = project_root_ / ".ally";
  file_watcher_ = std::make_unique<efsw::FileWatcher>();
  watch_id_ = file_watcher_->addWatch(ally_dir.string(), this, true);
  if (watch_id_ < 0) {
    throw std::runtime_error("ArtifactWatcher: failed to register OS watch on " + ally_dir.string());
  }
  file_watcher_->watch();

  // Start background debounce thread
  thread_ = std::thread([this]() -> void { Run(); });
}

ArtifactWatcher::~ArtifactWatcher() {
  stop_.store(true);
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ArtifactWatcher::handleFileAction(efsw::WatchID /*watchid*/, const std::string& dir, const std::string& filename,
                                       efsw::Action action, std::string /*old_filename*/) {
  // Only process create, write, and rename events
  if (action == efsw::Actions::Delete) {
    return;
  }

  // Quick filename check before acquiring lock
  std::filesystem::path filepath(filename);
  if (filepath.filename() != "artifact.md") {
    return;
  }

  std::filesystem::path abs_path = std::filesystem::path(dir) / filename;

  std::scoped_lock lock(pending_mutex_);
  pending_paths_.insert(abs_path.string());
  new_events_ = true;
}

void ArtifactWatcher::Run() {
  auto last_event_time = std::chrono::steady_clock::now();
  bool active_debounce = false;

  while (!stop_.load()) {
    // Check for genuinely new events (flag set by handleFileAction)
    {
      std::scoped_lock lock(pending_mutex_);
      if (new_events_) {
        active_debounce = true;
        new_events_ = false;
        last_event_time = std::chrono::steady_clock::now();
      }
    }

    // Process after debounce window
    if (active_debounce) {
      auto elapsed = std::chrono::steady_clock::now() - last_event_time;
      if (elapsed >= kDebounceMs) {
        ProcessPending();
        active_debounce = false;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
}

void ArtifactWatcher::ProcessPending() {
  std::unordered_set<std::string> paths;
  {
    std::scoped_lock lock(pending_mutex_);
    paths.swap(pending_paths_);
  }

  for (const auto& path_str : paths) {
    std::filesystem::path abs_path(path_str);

    // Skip files that don't exist (handles deletion events that slipped through)
    std::error_code exists_ec;
    if (!std::filesystem::is_regular_file(abs_path, exists_ec)) {
      continue;
    }

    auto result = ParseArtifactPath(project_root_, abs_path);
    if (!result) {
      continue;
    }
    auto [task_id, thread_id, stage] = *result;

    ArtifactChangedEvent::Kind kind;
    {
      std::scoped_lock lock(known_mutex_);
      auto [iter, inserted] = known_.insert(path_str);
      kind = inserted ? ArtifactChangedEvent::Kind::Created : ArtifactChangedEvent::Kind::Modified;
    }

    broadcast_.push(ArtifactChangedEvent{
        .kind = kind,
        .task_id = std::move(task_id),
        .thread_id = std::move(thread_id),
        .stage = std::move(stage),
    });
  }
}

auto ArtifactWatcher::ParseArtifactPath(const std::filesystem::path& project_root, const std::filesystem::path& abs_path)
    -> std::optional<std::tuple<std::string, std::string, std::string>> {
  auto rel = std::filesystem::relative(abs_path, project_root);

  std::vector<std::string> components;
  for (const auto& part : rel) {
    components.push_back(part.string());
  }

  // Expected: .ally / tasks / {task_id} / threads / {thread_id} / stages / {stage} / artifact.md
  if (components.size() != kExpectedComponents) {
    return std::nullopt;
  }
  if (components[0] != ".ally" || components[1] != "tasks" || components[kThreadsIndex] != "threads" ||
      components[kStagesIndex] != "stages" || components[kFilenameIndex] != "artifact.md") {
    return std::nullopt;
  }

  return std::make_tuple(std::move(components[kTaskIdIndex]), std::move(components[kThreadIdIndex]),
                         std::move(components[kStageIndex]));
}

}  // namespace ally::watcher
