#include "library/exif.h"

#include <cctype>
#include <utility>

namespace pb {

namespace {

// Bounds-checked reader over a TIFF block with selectable endianness.
class TiffReader {
 public:
  TiffReader(std::span<const uint8_t> d, bool bigEndian) : d_(d), be_(bigEndian) {}

  std::optional<uint16_t> u16(size_t off) const {
    if (off + 2 > d_.size()) return std::nullopt;
    return be_ ? uint16_t(d_[off] << 8 | d_[off + 1]) : uint16_t(d_[off + 1] << 8 | d_[off]);
  }
  std::optional<uint32_t> u32(size_t off) const {
    auto a = u16(off), b = u16(off + 2);
    if (!a || !b) return std::nullopt;
    return be_ ? (uint32_t(*a) << 16 | *b) : (uint32_t(*b) << 16 | *a);
  }
  std::span<const uint8_t> bytes(size_t off, size_t n) const {
    if (off > d_.size() || n > d_.size() - off) return {};
    return d_.subspan(off, n);
  }

 private:
  std::span<const uint8_t> d_;
  bool be_;
};

constexpr uint16_t kTagOrientation = 0x0112;
constexpr uint16_t kTagExifIfd = 0x8769;
constexpr uint16_t kTagGpsIfd = 0x8825;
constexpr uint16_t kGpsLatRef = 1, kGpsLat = 2, kGpsLonRef = 3, kGpsLon = 4, kGpsAltRef = 5, kGpsAlt = 6;
constexpr uint16_t kTagDateTimeOriginal = 0x9003;
constexpr uint16_t kTagThumbOffset = 0x0201;
constexpr uint16_t kTagThumbLength = 0x0202;
constexpr uint16_t kMaxEntries = 512;

// Calls fn(tag, type, count, valueFieldOffset) per entry; returns the next-IFD offset.
template <class Fn>
std::optional<uint32_t> walkIfd(const TiffReader& r, uint32_t ifd, Fn&& fn) {
  auto n = r.u16(ifd);
  if (!n || *n > kMaxEntries) return std::nullopt;
  for (uint16_t i = 0; i < *n; ++i) {
    const size_t e = size_t(ifd) + 2 + size_t(i) * 12;
    auto tag = r.u16(e), type = r.u16(e + 2);
    auto count = r.u32(e + 4);
    if (!tag || !type || !count) return std::nullopt;
    fn(*tag, *type, *count, e + 8);
  }
  return r.u32(size_t(ifd) + 2 + size_t(*n) * 12);
}

std::optional<uint32_t> scalar(const TiffReader& r, uint16_t type, size_t valueOff) {
  if (type == 3) {
    if (auto v = r.u16(valueOff)) return *v;
  } else if (type == 4) {
    return r.u32(valueOff);
  }
  return std::nullopt;
}

// `count` unsigned RATIONALs (type 5) at the offset stored in the value field.
bool rationals(const TiffReader& r, uint16_t type, uint32_t count, size_t valueOff, double (&out)[3]) {
  if (type != 5 || count < 1 || count > 3) return false;
  auto off = r.u32(valueOff);
  if (!off) return false;
  for (uint32_t i = 0; i < count; ++i) {
    auto num = r.u32(size_t(*off) + 8 * i), den = r.u32(size_t(*off) + 8 * i + 4);
    if (!num || !den) return false;
    out[i] = *den ? double(*num) / *den : 0;
  }
  return true;
}

// Degrees from a degrees/minutes/seconds triple.
std::optional<double> dms(const TiffReader& r, uint16_t type, uint32_t count, size_t valueOff) {
  double v[3] = {};
  if (count != 3 || !rationals(r, type, count, valueOff, v)) return std::nullopt;
  return v[0] + v[1] / 60 + v[2] / 3600;
}

void parseGps(const TiffReader& r, uint32_t ifd, ExifInfo& info) {
  std::optional<double> lat, lon, alt;
  char latRef = 'N', lonRef = 'E';
  bool belowSea = false;
  walkIfd(r, ifd, [&](uint16_t tag, uint16_t type, uint32_t count, size_t v) {
    // Refs are 2-byte ASCII and AltitudeRef one BYTE, so they sit in the value field.
    auto first = r.bytes(v, 1);
    if (tag == kGpsLatRef && type == 2 && !first.empty()) latRef = char(first[0]);
    if (tag == kGpsLonRef && type == 2 && !first.empty()) lonRef = char(first[0]);
    if (tag == kGpsAltRef && type == 1 && !first.empty()) belowSea = first[0] == 1;
    if (tag == kGpsLat) lat = dms(r, type, count, v);
    if (tag == kGpsLon) lon = dms(r, type, count, v);
    if (tag == kGpsAlt) {
      if (double a[3] = {}; count == 1 && rationals(r, type, count, v, a)) alt = a[0];
    }
  });
  if (!lat || !lon || *lat > 90 || *lon > 180) return;
  GeoPos g{latRef == 'S' ? -*lat : *lat, lonRef == 'W' ? -*lon : *lon, std::nullopt};
  if (alt) g.altitude = belowSea ? -*alt : *alt;
  info.gps = g;
}

// Fills `info` from a TIFF block; returns false if the header is not TIFF. When `thumb` is
// given, it receives the IFD1 preview JPEG {offset, length} relative to the block start.
bool parseTiff(std::span<const uint8_t> tiff, ExifInfo& info, std::pair<uint32_t, uint32_t>* thumb) {
  if (tiff.size() < 8) return false;
  bool be;
  if (tiff[0] == 'M' && tiff[1] == 'M') {
    be = true;
  } else if (tiff[0] == 'I' && tiff[1] == 'I') {
    be = false;
  } else {
    return false;
  }
  TiffReader r(tiff, be);
  if (r.u16(2) != 42) return false;
  auto ifd0 = r.u32(4);
  if (!ifd0) return false;

  std::optional<uint32_t> exifIfd, gpsIfd;
  auto ifd1 = walkIfd(r, *ifd0, [&](uint16_t tag, uint16_t type, uint32_t, size_t v) {
    if (tag == kTagOrientation) {
      if (auto o = scalar(r, type, v); o && *o >= 1 && *o <= 8) info.orientation = int(*o);
    } else if (tag == kTagExifIfd) {
      exifIfd = scalar(r, type, v);
    } else if (tag == kTagGpsIfd) {
      gpsIfd = scalar(r, type, v);
    }
  });
  if (gpsIfd && *gpsIfd != *ifd0) parseGps(r, *gpsIfd, info);

  if (exifIfd && *exifIfd != *ifd0) {
    walkIfd(r, *exifIfd, [&](uint16_t tag, uint16_t type, uint32_t count, size_t v) {
      if (tag != kTagDateTimeOriginal || type != 2 || count < 19) return;
      auto off = r.u32(v);
      if (!off) return;
      auto s = r.bytes(*off, 19);
      if (s.size() == 19) info.dateTimeOriginal = std::string(s.begin(), s.end());
    });
  }

  if (thumb && ifd1 && *ifd1 != 0 && *ifd1 != *ifd0) {
    std::optional<uint32_t> off, len;
    walkIfd(r, *ifd1, [&](uint16_t tag, uint16_t type, uint32_t, size_t v) {
      if (tag == kTagThumbOffset) off = scalar(r, type, v);
      if (tag == kTagThumbLength) len = scalar(r, type, v);
    });
    if (off && len && *off > 0 && *len > 0) *thumb = {*off, *len};
  }
  return true;
}

void parseExif(std::span<const uint8_t> tiff, uint64_t tiffFileOffset, JpegHeaderInfo& info) {
  ExifInfo e;
  std::pair<uint32_t, uint32_t> thumb{0, 0};
  if (!parseTiff(tiff, e, &thumb)) return;
  info.orientation = e.orientation;
  if (e.dateTimeOriginal) info.dateTimeOriginal = std::move(e.dateTimeOriginal);
  if (e.gps) info.gps = e.gps;
  if (thumb.second > 0) {
    info.thumbOffset = tiffFileOffset + thumb.first;
    info.thumbLength = thumb.second;
  }
}

bool isSof(uint8_t m) {
  return (m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC;
}

}  // namespace

std::optional<ExifInfo> parseExifBlock(std::span<const uint8_t> tiff) {
  ExifInfo info;
  if (!parseTiff(tiff, info, nullptr)) return std::nullopt;
  return info;
}

std::optional<JpegHeaderInfo> parseJpegHeader(std::span<const uint8_t> d) {
  if (d.size() < 2 || d[0] != 0xFF || d[1] != 0xD8) return std::nullopt;
  JpegHeaderInfo info;
  size_t i = 2;
  while (i + 4 <= d.size()) {
    if (d[i] != 0xFF) break;  // lost sync: corrupt
    uint8_t m = d[i + 1];
    if (m == 0xFF) {  // fill byte
      ++i;
      continue;
    }
    if (m == 0x01 || (m >= 0xD0 && m <= 0xD8)) {
      i += 2;
      continue;
    }
    if (m == 0xDA || m == 0xD9) break;  // start of scan / end of image
    const size_t len = size_t(d[i + 2]) << 8 | d[i + 3];
    if (len < 2) break;
    const size_t body = i + 4, bodyLen = len - 2;
    if (body + bodyLen > d.size()) break;  // truncated segment
    auto seg = d.subspan(body, bodyLen);
    if (m == 0xE1 && bodyLen > 6 && std::equal(seg.begin(), seg.begin() + 6, "Exif\0\0")) {
      if (!info.dateTimeOriginal && info.thumbLength == 0) parseExif(seg.subspan(6), body + 6, info);
    } else if (isSof(m) && bodyLen >= 5) {
      info.height = uint32_t(seg[1]) << 8 | seg[2];
      info.width = uint32_t(seg[3]) << 8 | seg[4];
      info.sawSof = true;
    }
    i = body + bodyLen;
  }
  return info;
}

std::optional<int64_t> parseExifDateTime(std::string_view s) {
  if (s.size() < 19) return std::nullopt;
  auto num = [&](size_t pos, size_t n) -> std::optional<int> {
    int v = 0;
    for (size_t k = pos; k < pos + n; ++k) {
      if (!std::isdigit(static_cast<unsigned char>(s[k]))) return std::nullopt;
      v = v * 10 + (s[k] - '0');
    }
    return v;
  };
  auto Y = num(0, 4), M = num(5, 2), D = num(8, 2), h = num(11, 2), mi = num(14, 2), sec = num(17, 2);
  if (!Y || !M || !D || !h || !mi || !sec) return std::nullopt;
  if (*Y < 1800 || *M < 1 || *M > 12 || *D < 1 || *D > 31 || *h > 23 || *mi > 59 || *sec > 60) {
    return std::nullopt;
  }
  // days_from_civil (Howard Hinnant)
  const int y = *Y - (*M <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = (153 * unsigned(*M + (*M > 2 ? -3 : 9)) + 2) / 5 + unsigned(*D) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = int64_t(era) * 146097 + int64_t(doe) - 719468;
  return days * 86400 + *h * 3600 + *mi * 60 + *sec;
}

}  // namespace pb
