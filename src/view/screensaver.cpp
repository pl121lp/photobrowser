#include "view/screensaver.h"

namespace pb {

Screensaver::Screensaver(double holdSeconds, uint32_t seed) : hold_(holdSeconds), rng_(seed) {}

void Screensaver::start() {
  phase_ = Phase::Starting;
  target_.reset();
}

uint32_t Screensaver::pick(uint32_t count) {
  if (!target_ || *target_ >= count || count < 2) return std::uniform_int_distribution<uint32_t>(0, count - 1)(rng_);
  const uint32_t r = std::uniform_int_distribution<uint32_t>(0, count - 2)(rng_);
  return r >= *target_ ? r + 1 : r;  // never the photo we are leaving
}

void Screensaver::update(Interaction& ui, uint32_t count, double t) {
  switch (phase_) {
    case Phase::Off: return;
    case Phase::Holding:
      if (t - since_ < hold_ || count < 2) return;
      [[fallthrough]];
    case Phase::Starting:
      if (count == 0) return;
      target_ = pick(count);
      ui.closePhoto();
      ui.flyTo(*target_, t);
      phase_ = Phase::Flying;
      return;
    case Phase::Flying:
      if (ui.flying()) return;
      ui.openPhoto(*target_, t);
      phase_ = Phase::Opening;
      return;
    case Phase::Opening:
      if (ui.focusProgress() < 1) return;
      since_ = t;
      phase_ = Phase::Holding;
      return;
  }
}

}  // namespace pb
