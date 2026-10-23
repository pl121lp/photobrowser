#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pb {

// Shared libraries (plugins). Returns nullptr and sets `error` on failure.
void* openLibrary(const std::filesystem::path& path, std::string& error);
void* librarySymbol(void* library, const char* name);
void closeLibrary(void* library);

// Platform file name of a shared library: libpb_<name>.so, pb_<name>.dll, libpb_<name>.dylib.
std::string sharedLibraryName(const std::string& name);
// The inverse: "video" for "libpb_video.so" (per platform); nullopt for other file names.
std::optional<std::string> pluginNameFromLibraryFile(const std::string& fileName);

}  // namespace pb
