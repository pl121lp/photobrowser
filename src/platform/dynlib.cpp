#include "platform/dynlib.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace pb {

#ifdef _WIN32
void* openLibrary(const std::filesystem::path& path, std::string& error) {
  HMODULE h = LoadLibraryExW(path.wstring().c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!h) error = "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")";
  return reinterpret_cast<void*>(h);
}
void* librarySymbol(void* library, const char* name) {
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}
void closeLibrary(void* library) { FreeLibrary(static_cast<HMODULE>(library)); }
std::string sharedLibraryName(const std::string& name) { return "pb_" + name + ".dll"; }
#else
void* openLibrary(const std::filesystem::path& path, std::string& error) {
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    const char* e = dlerror();
    error = e ? e : "dlopen failed";
  }
  return h;
}
void* librarySymbol(void* library, const char* name) { return dlsym(library, name); }
void closeLibrary(void* library) { dlclose(library); }
#ifdef __APPLE__
std::string sharedLibraryName(const std::string& name) { return "libpb_" + name + ".dylib"; }
#else
std::string sharedLibraryName(const std::string& name) { return "libpb_" + name + ".so"; }
#endif
#endif

std::optional<std::string> pluginNameFromLibraryFile(const std::string& fileName) {
  const std::string pattern = sharedLibraryName("*");
  const std::string prefix = pattern.substr(0, pattern.find('*'));
  const std::string suffix = pattern.substr(pattern.find('*') + 1);
  if (fileName.size() <= prefix.size() + suffix.size() || !fileName.starts_with(prefix) ||
      !fileName.ends_with(suffix)) {
    return std::nullopt;
  }
  return fileName.substr(prefix.size(), fileName.size() - prefix.size() - suffix.size());
}

}  // namespace pb
