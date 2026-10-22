#include "view/bench_script.h"

namespace pb {


BenchScript::BenchScript() {
  auto at = [&](double time, std::function<void(Interaction&, Vec2d, double)> f) {
    steps_.push_back({time, std::move(f)});
  };
  // Zoom out to the whole library.
  for (double s = 1.0; s < 3.0; s += 0.08) {
    at(s, [](Interaction& ui, Vec2d vp, double) { ui.onWheel(vp * 0.5, -1); });
  }
  // Two flings: right-to-left, then bottom-to-top.
  for (double base : {3.0, 4.0}) {
    const bool horizontal = base == 3.0;
    at(base, [horizontal](Interaction& ui, Vec2d vp, double t) {
      ui.onPointerDown(horizontal ? Vec2d{vp.x * 0.8, vp.y * 0.5} : Vec2d{vp.x * 0.5, vp.y * 0.8}, t);
    });
    for (int i = 1; i <= 6; ++i) {
      at(base + i * 0.016, [horizontal, i](Interaction& ui, Vec2d vp, double t) {
        const double d = i * vp.x * 0.05;
        ui.onPointerMove(horizontal ? Vec2d{vp.x * 0.8 - d, vp.y * 0.5} : Vec2d{vp.x * 0.5, vp.y * 0.8 - d}, t);
      });
    }
    at(base + 0.1, [horizontal](Interaction& ui, Vec2d vp, double t) {
      const double d = 6 * vp.x * 0.05;
      ui.onPointerUp(horizontal ? Vec2d{vp.x * 0.8 - d, vp.y * 0.5} : Vec2d{vp.x * 0.5, vp.y * 0.8 - d}, t);
    });
  }
  // Zoom into detail around a point left of center.
  for (double s = 5.0; s < 7.5; s += 0.08) {
    at(s, [](Interaction& ui, Vec2d vp, double) { ui.onWheel({vp.x * 0.3, vp.y * 0.4}, 1); });
  }
  at(7.6, [](Interaction& ui, Vec2d, double t) { ui.onKey(Key::Enter, t); });
  for (double s : {8.2, 8.6, 9.0}) at(s, [](Interaction& ui, Vec2d, double t) { ui.onKey(Key::Right, t); });
  at(9.6, [](Interaction& ui, Vec2d, double t) { ui.onKey(Key::Escape, t); });
}

void BenchScript::drive(Interaction& ui, Vec2d viewport, double t) {
  if (!started_) {
    started_ = true;
    t0_ = t;
  }
  while (next_ < steps_.size() && steps_[next_].at <= t - t0_) {
    steps_[next_].act(ui, viewport, t);
    ++next_;
  }
}

}  // namespace pb
