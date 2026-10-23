#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pb {

struct Options {
  std::optional<std::filesystem::path> photoDir;    // overrides the config file
  std::optional<std::filesystem::path> configFile;  // instead of the default location
  std::optional<std::filesystem::path> cacheDir;
  bool rebuildCache = false;
  std::string layout = "square-crop";
  bool bench = false;
  bool stats = false;  // start with the F3 stats display on
  bool windowed = false;  // start in a window despite [display] fullscreen
  std::optional<std::filesystem::path> snapshot;  // dev aid: save a frame as JPEG and exit
  std::vector<std::string> plugins;  // --plugin <name|path>, in order
  bool allPlugins = false;           // --all-plugins: also every plugin in the plugin folder
  bool help = false;
};

// Returns Options, or an error message for invalid arguments.
std::variant<Options, std::string> parseOptions(int argc, char** argv);
std::string usage();

}  // namespace pb
