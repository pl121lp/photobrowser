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
