#include <doctest/doctest.h>

#include "core/progress_eta.h"

TEST_CASE("progress eta: no estimate until enough time has passed") {
  pb::ProgressEta eta;
  CHECK_FALSE(eta.secondsLeft(100));
  eta.add(0, 0);
  eta.add(1, 10);
  CHECK_FALSE(eta.secondsLeft(100));  // one second is too little to go on
}

TEST_CASE("progress eta: remaining work over the recent rate") {
  pb::ProgressEta eta;
  for (int t = 0; t <= 5; ++t) eta.add(t, uint32_t(t * 10));  // 10 per second
  REQUIRE(eta.secondsLeft(100));
  CHECK(*eta.secondsLeft(100) == doctest::Approx(5));
}

TEST_CASE("progress eta: only the last window counts, so a slowdown shows up") {
  pb::ProgressEta eta(10);
  uint32_t done = 0;
  for (int t = 0; t <= 30; ++t) {
    eta.add(t, done);
    done += t < 15 ? 100 : 10;  // fast files first, then slow ones
  }
  // Last 10 s ran at 10 per second.
  REQUIRE(eta.secondsLeft(done + 990));
  CHECK(*eta.secondsLeft(done + 990) == doctest::Approx(100).epsilon(0.02));
}

TEST_CASE("progress eta: stalled or finished") {
  pb::ProgressEta eta;
  for (int t = 0; t <= 5; ++t) eta.add(t, 7);
  CHECK_FALSE(eta.secondsLeft(100));  // no progress, no rate
  pb::ProgressEta done;
  for (int t = 0; t <= 5; ++t) done.add(t, uint32_t(t * 20));
  CHECK(*done.secondsLeft(100) == doctest::Approx(0));
}
