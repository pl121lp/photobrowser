#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace pb {

struct Options {
  std::optional<std::filesystem::path> photoDir;    // overrides the config file
  std::optional<std::filesystem::path> configFile;  // instead of the default location
  std::optional<std::filesystem::path> cacheDir;
  bool rebuildCache = false;
  std::string layout = "square-crop";
  bool bench = false;
  bool stats = false;  // start with the F3 stats display on
  std::optional<std::filesystem::path> snapshot;  // dev aid: save a frame as JPEG and exit
  bool help = false;
};

// Returns Options, or an error message for invalid arguments.
std::variant<Options, std::string> parseOptions(int argc, char** argv);
std::string usage();

}  // namespace pb
