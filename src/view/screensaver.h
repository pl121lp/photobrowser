#pragma once

#include <cstdint>
#include <optional>
#include <random>

#include "view/interaction.h"

namespace pb {

// Screensaver mode: flies to a random photo, opens it in the whole-photo view,
// holds it for `holdSeconds`, then flies on to another one, until stopped.
// Pure logic driving an Interaction; times are seconds from any monotonic clock.
class Screensaver {
 public:
  explicit Screensaver(double holdSeconds, uint32_t seed = std::random_device{}());

  void start();
  void stop() { phase_ = Phase::Off; }
  bool active() const { return phase_ != Phase::Off; }
  // Call every frame after Interaction::update(), so each step starts in the frame
  // the previous one ends (no idle frame between). `count`: photos in the layout.
  void update(Interaction& ui, uint32_t count, double t);

 private:
  enum class Phase { Off, Starting, Flying, Opening, Holding };
  uint32_t pick(uint32_t count);

  double hold_;
  std::mt19937 rng_;
  Phase phase_ = Phase::Off;
  std::optional<uint32_t> target_;
  double since_ = 0;
};

}  // namespace pb
