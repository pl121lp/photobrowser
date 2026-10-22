#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pb {

// Per-user cache root: %LOCALAPPDATA% on Windows, $XDG_CACHE_HOME or ~/.cache elsewhere.
std::filesystem::path defaultCacheRoot();
// <cacheRoot>/photobrowse/<16 hex digits of a hash of the library's absolute path>
std::filesystem::path libraryCacheDir(const std::filesystem::path& cacheRoot,
                                      const std::filesystem::path& libraryAbs);

// Writes to a unique temp file next to `path`, then renames over it (atomic, safe
// for concurrent writers); creates parent directories.
bool atomicWriteFile(const std::filesystem::path& path, std::span<const uint8_t> data);
// Reads up to maxBytes starting at offset; nullopt if the file can't be opened.
std::optional<std::vector<uint8_t>> readFileRange(const std::filesystem::path& path, uint64_t offset,
                                                  uint64_t maxBytes);
std::optional<std::vector<uint8_t>> readWholeFile(const std::filesystem::path& path);
int64_t fileMtimeSeconds(const std::filesystem::path& path);
int64_t toUnixSeconds(std::filesystem::file_time_type t);

std::string toUtf8(const std::filesystem::path& p);  // generic ('/') separators
std::filesystem::path fromUtf8(const std::string& s);

// Lowers the calling thread's scheduling priority (background workers), so the
// render thread wins the CPU when all cores are busy decoding.
void lowerCurrentThreadPriority();

uint64_t fnv1a(const void* data, size_t n, uint64_t seed = 0xcbf29ce484222325ULL);

}  // namespace pb
