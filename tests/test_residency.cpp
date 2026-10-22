#include <doctest/doctest.h>

#include <set>

#include "stream/residency.h"

using pb::Residency;

namespace {
// Runs one frame touching `ids` at `level`, returns the requests issued.
std::vector<Residency::Request> frame(Residency& r, std::initializer_list<pb::PhotoId> ids, int level) {
  std::vector<Residency::Request> out;
  r.beginFrame();
  for (auto id : ids) r.touch(id, level, 0, out);
  r.endFrame();
  return out;
}
}  // namespace

TEST_CASE("levels: thresholds") {
  CHECK(pb::desiredLevel(40, -1) == 0);
  CHECK(pb::desiredLevel(50, -1) == 1);
  CHECK(pb::desiredLevel(300, -1) == 2);
  CHECK(pb::desiredLevel(1400, -1) == 4);
  CHECK(pb::desiredLevel(1500, -1) == pb::kFullLevel);
}

TEST_CASE("levels: hysteresis") {
  CHECK(pb::desiredLevel(40, 1) == 1);
  CHECK(pb::desiredLevel(30, 1) == 0);
  CHECK(pb::desiredLevel(1300, pb::kFullLevel) == pb::kFullLevel);
  CHECK(pb::desiredLevel(1000, pb::kFullLevel) == 4);
}

TEST_CASE("slots: allocate fills page then needs new page") {
  pb::SlotAllocator a(1024);
  CHECK(a.slotsPerPage() == 16);
  CHECK_FALSE(a.allocate());
  CHECK(a.addPage() == 0);
  std::set<uint16_t> seen;
  for (int i = 0; i < 16; ++i) {
    auto s = a.allocate();
    REQUIRE(s);
    CHECK(s->page == 0);
    seen.insert(s->slot);
  }
  CHECK(seen.size() == 16);
  CHECK_FALSE(a.allocate());
  a.release({0, 3});
  CHECK(a.allocate()->slot == 3);
}

TEST_CASE("slots: uv inset and origin") {
  pb::SlotAllocator a(128);
  CHECK(a.slotsPerPage() == 1024);
  const pb::SlotRef s{0, 33};  // row 1, col 1
  CHECK(a.origin(s) == pb::Vec2d{128, 128});
  auto uv = a.uv(s, 96, 128);
  CHECK(uv.u0 == doctest::Approx(128.5 / 4096));
  CHECK(uv.u1 == doctest::Approx((128 + 96 - 0.5) / 4096));
  CHECK(uv.v1 == doctest::Approx((256 - 0.5) / 4096));
}

TEST_CASE("residency: request deduped across frames") {
  Residency r(10, 4 * pb::kPageBytes);
  auto a = frame(r, {1, 2}, 2);
  CHECK(a.size() == 2);
  CHECK(a[0].level == 2);
  auto b = frame(r, {1, 2}, 2);
  CHECK(b.empty());
  CHECK(r.pendingRequests() == 2);
}

TEST_CASE("residency: level 0 needs no request") {
  Residency r(10, pb::kPageBytes);
  std::vector<Residency::Request> out;
  r.beginFrame();
  CHECK_FALSE(r.touch(1, 0, 0, out));
  CHECK(out.empty());
}

TEST_CASE("residency: falls back to best resident level") {
  Residency r(10, 4 * pb::kPageBytes);
  frame(r, {1}, 1);
  REQUIRE(r.place(1, 1, 96, 128));
  std::vector<Residency::Request> out;
  r.beginFrame();
  auto s = r.touch(1, 3, 0, out);  // want L3, only L1 resident
  REQUIRE(s);
  CHECK(s->level == 1);
  CHECK(s->w == 96);
  REQUIRE(out.size() == 1);
  CHECK(out[0].level == 3);
  r.endFrame();
  REQUIRE(r.place(1, 3, 384, 512));
  r.beginFrame();
  CHECK(r.touch(1, 3, 0, out)->level == 3);
  CHECK(r.touch(1, 2, 0, out)->level == 3);  // higher resolution is fine too
}

TEST_CASE("residency: coarser levels are requested first") {
  Residency r(10, 4 * pb::kPageBytes);
  std::vector<Residency::Request> out;
  r.beginFrame();
  r.touch(1, 1, 500, out);
  r.touch(2, 3, 0, out);
  CHECK(out[0].priority < out[1].priority);
}

TEST_CASE("residency: budget exhausted evicts LRU but never touched-this-frame") {
  Residency r(100, pb::kPageBytes);  // one L4 page = 16 slots
  std::vector<pb::PhotoId> ids;
  for (pb::PhotoId i = 0; i < 17; ++i) ids.push_back(i);
  // Frame A: photos 0..15 placed, all touched.
  std::vector<Residency::Request> out;
  r.beginFrame();
  for (pb::PhotoId i = 0; i < 16; ++i) r.touch(i, 4, 0, out);
  for (pb::PhotoId i = 0; i < 16; ++i) REQUIRE(r.place(i, 4, 1024, 768));
  r.endFrame();
  CHECK(r.pageCount(4) == 1);
  // Frame B: 0..15 still visible plus 16: no slot may be stolen from a visible photo.
  r.beginFrame();
  for (pb::PhotoId i = 0; i < 17; ++i) r.touch(i, 4, 0, out);
  CHECK_FALSE(r.place(16, 4, 1024, 768));
  r.endFrame();
  // Photo 0 scrolls away. Once L4 is no longer blocked, 16 asks again and takes 0's slot.
  for (int f = 0; f < 61; ++f) {
    r.beginFrame();
    for (pb::PhotoId i = 1; i < 17; ++i) r.touch(i, 4, 0, out);
    r.endFrame();
  }
  r.beginFrame();
  for (pb::PhotoId i = 1; i < 17; ++i) r.touch(i, 4, 0, out);
  auto s = r.place(16, 4, 1024, 768);
  REQUIRE(s);
  CHECK(r.bytesUsed() == pb::kPageBytes);
  CHECK_FALSE(r.touch(0, 4, 0, out).has_value());  // 0 was evicted
}

TEST_CASE("residency: budget-blocked level is capped, not re-requested every frame") {
  Residency r(100, pb::kPageBytes);
  std::vector<Residency::Request> out;
  r.beginFrame();
  for (pb::PhotoId i = 0; i < 17; ++i) r.touch(i, 4, 0, out);
  for (pb::PhotoId i = 0; i < 16; ++i) REQUIRE(r.place(i, 4, 1024, 768));
  CHECK_FALSE(r.place(16, 4, 1024, 768));
  r.endFrame();
  for (int f = 0; f < 5; ++f) {
    out.clear();
    r.beginFrame();
    for (pb::PhotoId i = 0; i < 17; ++i) r.touch(i, 4, 0, out);
    r.endFrame();
    for (auto& q : out) CHECK(q.level < 4);
  }
}

TEST_CASE("residency: stale requests are cancelled") {
  Residency r(10, pb::kPageBytes);
  auto reqs = frame(r, {5}, 2);
  REQUIRE(reqs.size() == 1);
  for (int i = 0; i < 31; ++i) frame(r, {}, 2);
  CHECK(reqs[0].cancel->cancelled);
  CHECK(r.pendingRequests() == 0);
  CHECK_FALSE(r.place(5, 2, 256, 192));  // late result for a cancelled request is dropped
  CHECK(frame(r, {5}, 2).size() == 1);   // can be requested again
}

TEST_CASE("residency: failed request is not retried") {
  Residency r(10, pb::kPageBytes);
  frame(r, {5}, 2);
  r.requestFailed(5, 2);
  CHECK(frame(r, {5}, 2).empty());
  CHECK(r.pendingRequests() == 0);
}

TEST_CASE("residency: empty pages are released to budget") {
  Residency r(100, 2 * pb::kPageBytes);
  std::vector<Residency::Request> out;
  r.beginFrame();
  r.touch(1, 4, 0, out);
  REQUIRE(r.place(1, 4, 1024, 768));
  r.endFrame();
  CHECK(r.bytesUsed() == pb::kPageBytes);
  auto ev = r.takePageEvents();
  REQUIRE(ev.size() == 1);
  CHECK(ev[0].added);
  CHECK(ev[0].level == 4);
  // Photo 1 is not touched for a long time while another level is under pressure.
  for (int f = 0; f < 200; ++f) frame(r, {}, 1);
  r.beginFrame();
  for (pb::PhotoId i = 2; i < 100; ++i) r.touch(i, 1, 0, out);
  for (pb::PhotoId i = 2; i < 100; ++i) r.place(i, 1, 96, 128);
  r.endFrame();
  r.releaseIdle(120);
  CHECK(r.pageCount(4) == 0);
  bool released = false;
  for (auto& e : r.takePageEvents()) released |= (!e.added && e.level == 4);
  CHECK(released);
}
