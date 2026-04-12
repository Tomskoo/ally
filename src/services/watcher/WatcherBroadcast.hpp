#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

#include "src/services/watcher/WatcherQueues.hpp"

namespace ally::watcher {

template <typename T>
class WatcherBroadcast {
 public:
  auto subscribe() -> std::shared_ptr<WatcherQueue<T>> {
    auto queue = std::make_shared<WatcherQueue<T>>();
    std::scoped_lock lock(mutex_);
    subscribers_.push_back(queue);
    return queue;
  }

  void unsubscribe(const std::shared_ptr<WatcherQueue<T>>& queue) {
    std::scoped_lock lock(mutex_);
    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                      [&queue](const std::weak_ptr<WatcherQueue<T>>& weak_sub) -> bool {
                                        auto sub = weak_sub.lock();
                                        return !sub || sub == queue;
                                      }),
                       subscribers_.end());
  }

  void push(const T& event) {
    std::scoped_lock lock(mutex_);
    // Clean up expired subscribers and push to live ones
    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                      [&event](const std::weak_ptr<WatcherQueue<T>>& weak_sub) -> bool {
                                        auto sub = weak_sub.lock();
                                        if (!sub) {
                                          return true;  // expired, remove
                                        }
                                        sub->push(T{event});
                                        return false;
                                      }),
                       subscribers_.end());
  }

 private:
  std::mutex mutex_;
  std::vector<std::weak_ptr<WatcherQueue<T>>> subscribers_;
};

}  // namespace ally::watcher
