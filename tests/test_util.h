#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace pbtest {

inline std::string photo(const std::string& name) { return std::string(PB_TEST_PHOTOS_DIR) + "/" + name; }

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

}  // namespace pbtest
