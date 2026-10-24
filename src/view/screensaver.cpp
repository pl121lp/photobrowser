#include "view/screensaver.h"

#include <algorithm>
#include <cmath>

namespace pb {

Screensaver::Screensaver(double holdSeconds, uint32_t seed) : hold_(holdSeconds), rng_(seed) {}

void Screensaver::start() {
  phase_ = Phase::Starting;
  arrived_ = false;
  target_.reset();
}

uint32_t Screensaver::pick(uint32_t count, std::optional<uint32_t> open) {
  if (order_ == ScreensaverOrder::Sequential) {
    const std::optional<uint32_t> from = target_ ? target_ : open;
    return from && *from + 1 < count ? *from + 1 : 0;
  }
  if (!target_ || *target_ >= count || count < 2) return std::uniform_int_distribution<uint32_t>(0, count - 1)(rng_);
  const uint32_t r = std::uniform_int_distribution<uint32_t>(0, count - 2)(rng_);
  return r >= *target_ ? r + 1 : r;  // never the photo we are leaving
}

void Screensaver::update(Interaction& ui, uint32_t count, double t, bool busy) {
  switch (phase_) {
    case Phase::Off: return;
    case Phase::Holding:
      if (slowZoom_ > 1) ui.setFocusZoom(std::pow(slowZoom_, std::min(1.0, (t - since_) / hold_)));
      if (t - since_ < hold_ || busy || count < 2) return;
      [[fallthrough]];
    case Phase::Starting:
      if (count == 0) return;
      target_ = pick(count, ui.focused());
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
      arrived_ = true;
      return;
  }
}

}  // namespace pb
