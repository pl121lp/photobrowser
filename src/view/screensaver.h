#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <utility>

#include "view/interaction.h"

namespace pb {

// Which photo comes next: any other at random, or the next in the layout (timeline)
// order, wrapping around at the end.
enum class ScreensaverOrder { Random, Sequential };

// Screensaver mode: flies to a random (or the next) photo, opens it in the whole-photo view,
// holds it for `holdSeconds`, then flies on to another one, until stopped.
// Pure logic driving an Interaction; times are seconds from any monotonic clock.
class Screensaver {
 public:
  explicit Screensaver(double holdSeconds, uint32_t seed = std::random_device{}());

  // Zoom reached at the end of each hold (1 = none): a slow, steady push into the photo.
  void setSlowZoom(double zoom) { slowZoom_ = zoom; }
  // Sequential starts after the photo open when started, else at the first photo.
  void setOrder(ScreensaverOrder order) { order_ = order; }
  void start();
  void stop() { phase_ = Phase::Off; }
  bool active() const { return phase_ != Phase::Off; }
  // True while it changes the view every frame on its own (the slow zoom).
  bool animating() const { return phase_ == Phase::Holding && slowZoom_ > 1; }
  // Call every frame after Interaction::update(), so each step starts in the frame
  // the previous one ends (no idle frame between). `count`: photos in the layout.
  // `busy`: the open item is still playing (e.g. a video); it stays until done.
  void update(Interaction& ui, uint32_t count, double t, bool busy = false);
  // True once each time a photo has fully opened (to start its live content).
  bool takeArrived() { return std::exchange(arrived_, false); }
  // The photo it is flying to, until it starts opening (e.g. for the map to fly along).
  std::optional<uint32_t> heading() const { return phase_ == Phase::Flying ? target_ : std::nullopt; }

 private:
  enum class Phase { Off, Starting, Flying, Opening, Holding };
  uint32_t pick(uint32_t count, std::optional<uint32_t> open);

  double hold_;
  double slowZoom_ = 1;
  ScreensaverOrder order_ = ScreensaverOrder::Random;
  std::mt19937 rng_;
  Phase phase_ = Phase::Off;
  std::optional<uint32_t> target_;
  double since_ = 0;
  bool arrived_ = false;
};

}  // namespace pb
