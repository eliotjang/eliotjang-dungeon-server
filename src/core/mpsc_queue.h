#pragma once

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

namespace ejd::core {

// Push 다중생산자 안전. Drain 단일 소비자. out 빈 벡터 전달
template <typename T>
class MpscQueue {
 public:
  void Push(T item) {
    {
      std::lock_guard<std::mutex> lock(mutex_);

      items_.push_back(std::move(item));
    }
    cv_.notify_one();
  }
  size_t WaitDrainUntil(std::vector<T>& out, std::stop_token st,
                        std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait_until(lock, st, deadline, [this]() { return !items_.empty(); });

    out.clear();
    if (items_.empty()) return 0;

    std::swap(out, items_);
    return out.size();
  }
  size_t TryDrain(std::vector<T>& out) {
    std::lock_guard<std::mutex> lock(mutex_);

    out.clear();
    if (items_.empty()) return 0;

    std::swap(out, items_);
    return out.size();
  }

 private:
  std::mutex mutex_;
  std::condition_variable_any cv_;
  std::vector<T> items_;
};

}  // namespace ejd::core
