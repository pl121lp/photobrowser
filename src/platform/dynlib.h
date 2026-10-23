#pragma once

#include <filesystem>
#include <string>

namespace pb {

// Shared libraries (plugins). Returns nullptr and sets `error` on failure.
void* openLibrary(const std::filesystem::path& path, std::string& error);
void* librarySymbol(void* library, const char* name);
void closeLibrary(void* library);

// Platform file name of a shared library: libpb_<name>.so, pb_<name>.dll, libpb_<name>.dylib.
std::string sharedLibraryName(const std::string& name);

}  // namespace pb
