#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace pb {

// Set by the requester when a queued job is no longer wanted; jobs check it.
struct CancelToken {
  std::atomic<bool> cancelled{false};
};

unsigned defaultWorkerCount();    // max(2, hardware threads - 1): bulk indexing
// Loads while browsing: `configured` threads ([performance] decode_threads), or for
// 0 (auto) 2..4 depending on the CPU.
unsigned streamingWorkerCount(unsigned configured = 0);

// Fixed pool of worker threads running prioritized jobs (lower priority value first,
// FIFO among equal priorities).
class JobSystem {
 public:
  explicit JobSystem(unsigned threads);
  ~JobSystem();  // drops pending jobs, finishes running ones, joins
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  void submit(int64_t priority, std::function<void()> job);
  size_t pending() const;
  void clearPending();

 private:
  struct Item {
    int64_t priority;
    uint64_t seq;
    std::function<void()> job;
    bool operator<(const Item& o) const {  // std::priority_queue is a max-heap
      return priority != o.priority ? priority > o.priority : seq > o.seq;
    }
  };
  void run(std::stop_token st);

  mutable std::mutex m_;
  std::condition_variable_any cv_;
  std::priority_queue<Item> queue_;
  uint64_t seq_ = 0;
  std::vector<std::jthread> workers_;
};

// Thread-safe FIFO for handing results back to the main thread.
template <class T>
class ResultQueue {
 public:
  void push(T v) {
    std::lock_guard lock(m_);
    q_.push_back(std::move(v));
  }
  std::vector<T> drain(size_t max = SIZE_MAX) {
    std::lock_guard lock(m_);
    std::vector<T> out;
    while (!q_.empty() && out.size() < max) {
      out.push_back(std::move(q_.front()));
      q_.pop_front();
    }
    return out;
  }
  size_t size() const {
    std::lock_guard lock(m_);
    return q_.size();
  }

 private:
  mutable std::mutex m_;
  std::deque<T> q_;
};

}  // namespace pb
