#pragma once

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
