#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace pb {

// Collects frame times (ms) for percentile reporting.
class FrameStats {
 public:
  void add(double ms) { samples_.push_back(ms); }
  void reset() { samples_.clear(); }
  size_t count() const { return samples_.size(); }
  double percentile(double p) const {
    if (samples_.empty()) return 0;
    std::vector<double> s = samples_;
    const size_t k = std::min(s.size() - 1, size_t(std::ceil(p / 100 * double(s.size()))) - (p > 0 ? 1 : 0));
    std::nth_element(s.begin(), s.begin() + long(k), s.end());
    return s[k];
  }
  double max() const { return samples_.empty() ? 0 : *std::max_element(samples_.begin(), samples_.end()); }
  size_t countAbove(double ms) const {
    return size_t(std::count_if(samples_.begin(), samples_.end(), [&](double v) { return v > ms; }));
  }

 private:
  std::vector<double> samples_;
};

}  // namespace pb
