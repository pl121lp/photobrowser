#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace pb {

// The most recent `capacity` frame times (ms), for the stats overlay graph.
class FrameHistory {
 public:
  explicit FrameHistory(size_t capacity = 240) : values_(capacity) {}

  void add(double ms) {
    values_[next_] = ms;
    next_ = (next_ + 1) % values_.size();
    count_ = std::min(count_ + 1, values_.size());
  }
  void clear() { next_ = count_ = 0; }
  size_t size() const { return count_; }
  size_t capacity() const { return values_.size(); }
  // i = 0 is the oldest kept value.
  double at(size_t i) const { return values_[(next_ + values_.size() - count_ + i) % values_.size()]; }
  double median() const { return percentile(50); }
  double percentile(double p) const {
    if (count_ == 0) return 0;
    std::vector<double> v(count_);
    for (size_t i = 0; i < count_; ++i) v[i] = at(i);
    const size_t k = std::min(count_ - 1, size_t(std::ceil(p / 100 * double(count_))) - (p > 0 ? 1 : 0));
    std::nth_element(v.begin(), v.begin() + long(k), v.end());
    return v[k];
  }

 private:
  std::vector<double> values_;
  size_t next_ = 0, count_ = 0;
};

}  // namespace pb
