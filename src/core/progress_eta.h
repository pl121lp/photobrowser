#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

namespace pb {

// Time left for a long job (e.g. first-run indexing) from the rate over the last
// `window` seconds, so it follows slow stretches instead of the average since the start.
class ProgressEta {
 public:
  explicit ProgressEta(double window = 15, double minSpan = 3) : window_(window), minSpan_(minSpan) {}

  // `done` units finished at time `t` (seconds, increasing).
  void add(double t, uint32_t done) {
    samples_.emplace_back(t, done);
    while (samples_.size() > 2 && samples_[1].first <= t - window_) samples_.pop_front();
  }
  // Seconds until `total` is reached; nothing until there is a rate to go on.
  std::optional<double> secondsLeft(uint32_t total) const {
    if (samples_.size() < 2) return std::nullopt;
    const auto& [t0, d0] = samples_.front();
    const auto& [t1, d1] = samples_.back();
    if (d1 >= total) return 0.0;
    if (t1 - t0 < minSpan_ || d1 <= d0) return std::nullopt;
    return double(total - d1) * (t1 - t0) / double(d1 - d0);
  }

 private:
  double window_, minSpan_;
  std::deque<std::pair<double, uint32_t>> samples_;
};

}  // namespace pb
