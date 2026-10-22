#include "stream/job_system.h"

#include <algorithm>

namespace pb {

unsigned defaultWorkerCount() { return std::max(2u, std::thread::hardware_concurrency() - 1); }

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
