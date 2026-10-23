#include <doctest/doctest.h>

#include "../plugins/video/iso6709.h"

using pbvideo::parseIso6709;

TEST_CASE("iso6709: decimal degrees, as phones write them") {
  auto p = parseIso6709("+60.3939+5.3252/");
  REQUIRE(p);
  CHECK(p->lat == doctest::Approx(60.3939));
  CHECK(p->lon == doctest::Approx(5.3252));
  CHECK_FALSE(p->altitude);

  p = parseIso6709("+40.7105-74.0136/");
  REQUIRE(p);
  CHECK(p->lat == doctest::Approx(40.7105));
  CHECK(p->lon == doctest::Approx(-74.0136));

  p = parseIso6709("-33.8688+151.2093/");
  REQUIRE(p);
  CHECK(p->lat == doctest::Approx(-33.8688));
  CHECK(p->lon == doctest::Approx(151.2093));
}

TEST_CASE("iso6709: zero-padded longitude with altitude (iPhone)") {
  auto p = parseIso6709("+67.2317+014.6150+007.567/");
  REQUIRE(p);
  CHECK(p->lat == doctest::Approx(67.2317));
  CHECK(p->lon == doctest::Approx(14.6150));
  REQUIRE(p->altitude);
  CHECK(*p->altitude == doctest::Approx(7.567));

  p = parseIso6709("+35.6895+139.6917-012.5/");
  REQUIRE(p);
  REQUIRE(p->altitude);
  CHECK(*p->altitude == doctest::Approx(-12.5));
}

TEST_CASE("iso6709: degrees-minutes and degrees-minutes-seconds forms") {
  auto p = parseIso6709("+4042.63-07400.82/");  // DDMM.MM / DDDMM.MM
  REQUIRE(p);
  CHECK(p->lat == doctest::Approx(40 + 42.63 / 60));
  CHECK(p->lon == doctest::Approx(-(74 + 0.82 / 60)));

  p = parseIso6709("+404238-0740049/");  // DDMMSS / DDDMMSS
  REQUIRE(p);
  CHECK(p->lat == doctest::Approx(40 + 42 / 60.0 + 38 / 3600.0));
  CHECK(p->lon == doctest::Approx(-(74 + 0 / 60.0 + 49 / 3600.0)));
}

TEST_CASE("iso6709: no trailing slash, CRS suffix") {
  auto p = parseIso6709("+60.3939+5.3252");
  REQUIRE(p);
  CHECK(p->lon == doctest::Approx(5.3252));
  p = parseIso6709("+60.3939+005.3252+012.0CRSWGS_84/");
  REQUIRE(p);
  REQUIRE(p->altitude);
  CHECK(*p->altitude == doctest::Approx(12.0));
}

TEST_CASE("iso6709: malformed or out-of-range strings are rejected") {
  CHECK_FALSE(parseIso6709(nullptr));
  CHECK_FALSE(parseIso6709(""));
  CHECK_FALSE(parseIso6709("/"));
  CHECK_FALSE(parseIso6709("60.3939+5.3252/"));  // latitude without a sign
  CHECK_FALSE(parseIso6709("+60.3939/"));        // no longitude
  CHECK_FALSE(parseIso6709("+60.3939+/"));
  CHECK_FALSE(parseIso6709("+91.0000+5.0000/"));
  CHECK_FALSE(parseIso6709("+60.0000+181.0000/"));
  CHECK_FALSE(parseIso6709("+4075.00+00500.00/"));  // 75 minutes
  CHECK_FALSE(parseIso6709("+60.39x9+5.3252/"));
}
