#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

#include "stream/job_system.h"

using namespace std::chrono_literals;

namespace {
void waitFor(const std::function<bool()>& cond) {
  for (int i = 0; i < 500 && !cond(); ++i) std::this_thread::sleep_for(2ms);
}
}  // namespace

TEST_CASE("jobs: all jobs run") {
  pb::JobSystem js(4);
  std::atomic<int> n{0};
  for (int i = 0; i < 100; ++i) js.submit(i, [&] { ++n; });
  waitFor([&] { return n == 100; });
  CHECK(n == 100);
}

TEST_CASE("jobs: priority order with one worker") {
  pb::JobSystem js(1);
  std::latch gate(1);
  std::latch started(1);
  js.submit(0, [&] {
    started.count_down();
    gate.wait();
  });
  started.wait();
  std::mutex m;
  std::vector<int> order;
  for (int p : {5, 1, 3, 1}) js.submit(p, [&, p] {
    std::lock_guard lock(m);
    order.push_back(p);
  });
  CHECK(js.pending() == 4);
  gate.count_down();
  waitFor([&] {
    std::lock_guard lock(m);
    return order.size() == 4;
  });
  std::lock_guard lock(m);
  CHECK(order == std::vector<int>{1, 1, 3, 5});
}

TEST_CASE("jobs: clearPending drops queued work") {
  pb::JobSystem js(1);
  std::latch gate(1), started(1);
  js.submit(0, [&] {
    started.count_down();
    gate.wait();
  });
  started.wait();
  std::atomic<int> n{0};
  for (int i = 0; i < 10; ++i) js.submit(1, [&] { ++n; });
  js.clearPending();
  CHECK(js.pending() == 0);
  gate.count_down();
  std::this_thread::sleep_for(20ms);
  CHECK(n == 0);
}

TEST_CASE("jobs: destructor with pending work does not hang") {
  std::atomic<int> n{0};
  {
    pb::JobSystem js(2);
    for (int i = 0; i < 1000; ++i) js.submit(i, [&] {
      std::this_thread::sleep_for(1ms);
      ++n;
    });
  }
  CHECK(n < 1000);
}

TEST_CASE("jobs: default worker count") { CHECK(pb::defaultWorkerCount() >= 2); }

TEST_CASE("results: drain respects max") {
  pb::ResultQueue<int> q;
  for (int i = 0; i < 5; ++i) q.push(i);
  CHECK(q.size() == 5);
  auto a = q.drain(2);
  CHECK(a == std::vector<int>{0, 1});
  auto b = q.drain();
  CHECK(b == std::vector<int>{2, 3, 4});
  CHECK(q.size() == 0);
}

TEST_CASE("jobs: streaming pool is small so decoding cannot starve rendering") {
  CHECK(pb::streamingWorkerCount() >= 2);
  CHECK(pb::streamingWorkerCount() <= 4);
}
