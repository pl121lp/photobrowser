#include "stream/job_system.h"

#include <algorithm>

#include "platform/paths.h"

namespace pb {

unsigned defaultWorkerCount() { return std::max(2u, std::thread::hardware_concurrency() - 1); }

unsigned streamingWorkerCount(unsigned configured) {
  if (configured > 0) return configured;
  // More decoders compete with the (often integrated) GPU for memory bandwidth
  // and with the render thread for CPU; measured smoother frames with 4 than 15.
  return std::clamp(std::thread::hardware_concurrency() / 4, 2u, 4u);
}

JobSystem::JobSystem(unsigned threads) {
  for (unsigned i = 0; i < std::max(1u, threads); ++i) {
    workers_.emplace_back([this](std::stop_token st) { run(st); });
  }
}

JobSystem::~JobSystem() {
  clearPending();
  for (auto& w : workers_) w.request_stop();
  cv_.notify_all();
  workers_.clear();  // joins
}

void JobSystem::submit(int64_t priority, std::function<void()> job) {
  {
    std::lock_guard lock(m_);
    queue_.push({priority, seq_++, std::move(job)});
  }
  cv_.notify_one();
}

size_t JobSystem::pending() const {
  std::lock_guard lock(m_);
  return queue_.size();
}

void JobSystem::clearPending() {
  std::lock_guard lock(m_);
  queue_ = {};
}

void JobSystem::run(std::stop_token st) {
  lowerCurrentThreadPriority();
  while (true) {
    std::function<void()> job;
    {
      std::unique_lock lock(m_);
      if (!cv_.wait(lock, st, [this] { return !queue_.empty(); })) return;
      job = std::move(const_cast<Item&>(queue_.top()).job);
      queue_.pop();
    }
    job();
  }
}

}  // namespace pb
