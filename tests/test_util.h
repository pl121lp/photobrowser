#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace pbtest {

// A fixture photo by file name, found anywhere under photos/ (which may be organised in
// subfolders). Throws if it is missing, so the test fails instead of reading nothing.
inline std::string photo(const std::string& name) {
  static const std::map<std::string, std::string> byName = [] {
    std::map<std::string, std::string> m;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(PB_TEST_PHOTOS_DIR, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file()) m.emplace(it->path().filename().string(), it->path().string());
    }
    return m;
  }();
  const auto it = byName.find(name);
  if (it == byName.end()) throw std::runtime_error("test photo not found under " PB_TEST_PHOTOS_DIR ": " + name);
  return it->second;
}

// The folder holding fixture photo `name`.
inline std::filesystem::path photoDir(const std::string& name) {
  return std::filesystem::path(photo(name)).parent_path();
}

inline std::vector<uint8_t> readFile(const std::string& path, size_t maxBytes = SIZE_MAX) {
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> data;
  char buf[65536];
  while (data.size() < maxBytes) {
    f.read(buf, sizeof buf);
    if (f.gcount() <= 0) break;
    data.insert(data.end(), buf, buf + f.gcount());
  }
  if (data.size() > maxBytes) data.resize(maxBytes);
  return data;
}

// Big-endian TIFF block with DateTimeOriginal "2026:06:16 21:56:20" and GPS 33 deg 52' 30" S,
// 151 deg 12' 36" W, 122.5 m below sea level.
inline std::vector<uint8_t> makeDateGpsTiff() {
  std::vector<uint8_t> t = {'M', 'M', 0, 42};
  auto be16 = [&](uint16_t x) { t.push_back(uint8_t(x >> 8)); t.push_back(uint8_t(x)); };
  auto be32 = [&](uint32_t x) { be16(uint16_t(x >> 16)); be16(uint16_t(x)); };
  auto entry = [&](uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
    be16(tag); be16(type); be32(count); be32(value);
  };
  auto rat = [&](uint32_t n, uint32_t d) { be32(n); be32(d); };
  be32(8);
  be16(2);
  entry(0x8769, 4, 1, 38);  // ExifIFD
  entry(0x8825, 4, 1, 76);  // GPS IFD
  be32(0);
  be16(1);  // ExifIFD at 38
  entry(0x9003, 2, 20, 56);
  be32(0);
  for (char c : std::string("2026:06:16 21:56:20")) t.push_back(uint8_t(c));
  t.push_back(0);  // date at 56..76
  be16(6);         // GPS IFD at 76
  entry(1, 2, 2, uint32_t('S') << 24);
  entry(2, 5, 3, 154);
  entry(3, 2, 2, uint32_t('W') << 24);
  entry(4, 5, 3, 178);
  entry(5, 1, 1, 1u << 24);
  entry(6, 5, 1, 202);
  be32(0);
  rat(33, 1); rat(52, 1); rat(3000, 100);
  rat(151, 1); rat(12, 1); rat(36, 1);
  rat(1225, 10);
  return t;
}

// A valid 8-bit RGBA PNG (rgba holds w*h*4 bytes): IHDR, optional eXIf (a bare TIFF block),
// one IDAT with a zlib stream of stored deflate blocks, IEND; real CRC-32s and Adler-32.
inline std::vector<uint8_t> makePng(uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba,
                                    const std::vector<uint8_t>& exifTiff = {}) {
  auto be32 = [](std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s));
  };
  auto crc32 = [](const uint8_t* d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
      c ^= d[i];
      for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
  };
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td.data(), td.size()));
  };
  std::vector<uint8_t> ihdr;
  be32(ihdr, w);
  be32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk("IHDR", ihdr);
  if (!exifTiff.empty()) chunk("eXIf", exifTiff);
  std::vector<uint8_t> raw;  // filter byte 0 per row
  for (uint32_t y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  do {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(n));
    z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n));
    z.push_back(uint8_t(~n >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  } while (pos < raw.size());
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  be32(z, (b << 16) | a);
  chunk("IDAT", z);
  chunk("IEND", {});
  return out;
}

}  // namespace pbtest

#include <atomic>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#include <filesystem>

namespace pbtest {

// Fresh empty directory under the system temp dir; removed on destruction.
struct TempDir {
  std::filesystem::path path;
  TempDir() {
    static std::atomic<int> counter{0};
    path = std::filesystem::temp_directory_path() /
           ("pb_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

inline void writeFile(const std::filesystem::path& p, const std::string& content) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << content;
}

}  // namespace pbtest
