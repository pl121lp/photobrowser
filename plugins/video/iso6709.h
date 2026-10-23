// ISO 6709 location strings, as MP4/MOV files store them ("location" tag from Android,
// "com.apple.quicktime.location.ISO6709" from iPhones): "+60.3939+5.3252/",
// "+67.2317+014.6150+007.567/". Latitude and longitude may also be written as
// DDMM.M / DDDMM.M or DDMMSS.S / DDDMMSS.S. Header-only so the tests can include it.
#pragma once

#include <optional>

namespace pbvideo {

struct Iso6709 {
  double lat = 0, lon = 0;        // degrees
  std::optional<double> altitude;  // meters
};

namespace detail {

struct Number {
  double value = 0;  // signed
  int intDigits = 0;
  double intPart = 0, frac = 0;  // unsigned, frac in [0, 1)
};

// Signed decimal number ("+012.34"); digits are parsed by hand, not with the locale.
inline bool readNumber(const char*& s, Number& n) {
  if (*s != '+' && *s != '-') return false;
  const double sign = *s++ == '-' ? -1 : 1;
  n = {};
  for (; *s >= '0' && *s <= '9'; ++s, ++n.intDigits) n.intPart = n.intPart * 10 + (*s - '0');
  if (n.intDigits == 0) return false;
  if (*s == '.') {
    ++s;
    double scale = 0.1;
    for (; *s >= '0' && *s <= '9'; ++s, scale /= 10) n.frac += (*s - '0') * scale;
  }
  n.value = sign * (n.intPart + n.frac);
  return true;
}

// Degrees from D(DD), DDMM or DDMMSS (`degDigits` = 2 for latitude, 3 for longitude).
inline std::optional<double> toDegrees(const Number& n, int degDigits, double limit) {
  double deg;
  if (n.intDigits <= degDigits) {
    deg = n.intPart + n.frac;
  } else {
    const int extra = n.intDigits - degDigits;
    const auto ip = static_cast<long long>(n.intPart);
    if (extra == 2) {
      const double min = double(ip % 100) + n.frac;
      if (min >= 60) return std::nullopt;
      deg = double(ip / 100) + min / 60;
    } else if (extra == 4) {
      const double min = double(ip / 100 % 100), sec = double(ip % 100) + n.frac;
      if (min >= 60 || sec >= 60) return std::nullopt;
      deg = double(ip / 10000) + min / 60 + sec / 3600;
    } else {
      return std::nullopt;
    }
  }
  if (deg > limit) return std::nullopt;
  return n.value < 0 ? -deg : deg;
}

}  // namespace detail

inline std::optional<Iso6709> parseIso6709(const char* s) {
  if (!s) return std::nullopt;
  detail::Number la, lo;
  if (!detail::readNumber(s, la) || !detail::readNumber(s, lo)) return std::nullopt;
  Iso6709 r;
  const auto lat = detail::toDegrees(la, 2, 90), lon = detail::toDegrees(lo, 3, 180);
  if (!lat || !lon) return std::nullopt;
  r.lat = *lat;
  r.lon = *lon;
  if (*s == '+' || *s == '-') {
    detail::Number alt;
    if (!detail::readNumber(s, alt)) return std::nullopt;
    r.altitude = alt.value;
  }
  if (*s != '\0' && *s != '/' && *s != 'C') return std::nullopt;  // end, "/" or "CRS..."
  return r;
}

}  // namespace pbvideo
