#pragma once

#include <deque>
#include <mutex>
#include <vector>

namespace ally::watcher {

template <typename T>
class WatcherQueue {
 public:
  static constexpr std::size_t kCapacity = 256;

  auto push(T event) -> bool {
    std::scoped_lock lock(mutex_);
    if (queue_.size() >= kCapacity) {
      return false;
    }
    queue_.push_back(std::move(event));
    return true;
  }

  auto drain() -> std::vector<T> {
    std::scoped_lock lock(mutex_);
    std::vector<T> out(std::make_move_iterator(queue_.begin()), std::make_move_iterator(queue_.end()));
    queue_.clear();
    return out;
  }

 private:
  std::mutex mutex_;
  std::deque<T> queue_;
};

}  // namespace ally::watcher
