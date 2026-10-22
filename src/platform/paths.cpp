#include "platform/paths.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace pb {

namespace fs = std::filesystem;

uint64_t fnv1a(const void* data, size_t n, uint64_t h) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

std::string toUtf8(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return {s.begin(), s.end()};
}

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

fs::path defaultCacheRoot() {
#ifdef _WIN32
  if (const char* v = std::getenv("LOCALAPPDATA"); v && *v) return fromUtf8(v);
#else
  if (const char* v = std::getenv("XDG_CACHE_HOME"); v && *v) return fs::path(v);
  if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".cache";
#endif
  return fs::temp_directory_path();
}

fs::path libraryCacheDir(const fs::path& cacheRoot, const fs::path& libraryAbs) {
  const std::string s = toUtf8(libraryAbs);
  char hex[17];
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(fnv1a(s.data(), s.size())));
  return cacheRoot / "photobrowse" / hex;
}

bool atomicWriteFile(const fs::path& path, std::span<const uint8_t> data) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  fs::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!f) return false;
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

std::optional<std::vector<uint8_t>> readFileRange(const fs::path& path, uint64_t offset, uint64_t maxBytes) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  f.seekg(0, std::ios::end);
  const uint64_t size = uint64_t(f.tellg());
  if (offset >= size) return std::vector<uint8_t>{};
  const uint64_t n = std::min(maxBytes, size - offset);
  std::vector<uint8_t> out(n);
  f.seekg(std::streamoff(offset));
  f.read(reinterpret_cast<char*>(out.data()), std::streamsize(n));
  out.resize(size_t(f.gcount()));
  return out;
}

std::optional<std::vector<uint8_t>> readWholeFile(const fs::path& path) {
  return readFileRange(path, 0, UINT64_MAX);
}

void lowerCurrentThreadPriority() {
#ifdef _WIN32
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#else
  // On Linux, nice values apply per thread.
  setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

int64_t toUnixSeconds(fs::file_time_type t) {
  using namespace std::chrono;
  return duration_cast<seconds>(file_clock::to_sys(t).time_since_epoch()).count();
}

int64_t fileMtimeSeconds(const fs::path& path) {
  std::error_code ec;
  auto t = fs::last_write_time(path, ec);
  return ec ? 0 : toUnixSeconds(t);
}

}  // namespace pb
