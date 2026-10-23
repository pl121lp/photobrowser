#include "app/options.h"

#include <string_view>

#include "layout/layout.h"

namespace pb {

std::string usage() {
  return "Usage: photobrowse [photo-folder] [options]\n"
         "\n"
         "  photo-folder          folder with JPEG photos (subfolders too with\n"
         "                        [library] recursive = on)\n"
         "                        (default: [library] folder in the config file, else ./photos)\n"
         "  --config <file>       settings file (default: ~/.config/photobrowse/config.ini,\n"
         "                        created with documented defaults on first run)\n"
         "  --cache-dir <dir>     thumbnail cache root (default: per-user cache directory)\n"
         "  --rebuild-cache       discard this library's cache and re-index\n"
         "  --layout <name>       layout mode: square-crop (default)\n"
         "  --windowed            start in a window instead of fullscreen\n"
         "  --stats               start with the stats display on (same as pressing F3)\n"
         "  --bench               run a scripted pan/zoom benchmark, print frame times, exit\n"
         "  --plugin <name|path>  load a plugin, e.g. --plugin video (repeatable); a name is\n"
         "                        looked up in the plugins/ folder next to the program\n"
         "  --snapshot <file.jpg> save the start view (or the end of --bench) as JPEG and exit\n"
         "  --help                show this help\n";
}

std::variant<Options, std::string> parseOptions(int argc, char** argv) {
  Options o;
  bool haveFolder = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    auto value = [&]() -> std::optional<std::string> {
      if (i + 1 >= argc) return std::nullopt;
      return std::string(argv[++i]);
    };
    if (a == "--help" || a == "-h") {
      o.help = true;
    } else if (a == "--cache-dir") {
      auto v = value();
      if (!v) return std::string("--cache-dir needs a directory");
      o.cacheDir = *v;
    } else if (a == "--config") {
      auto v = value();
      if (!v) return std::string("--config needs a file");
      o.configFile = *v;
    } else if (a == "--rebuild-cache") {
      o.rebuildCache = true;
    } else if (a == "--layout") {
      auto v = value();
      if (!v) return std::string("--layout needs a name");
      if (!makeLayout(*v)) return "unknown layout '" + *v + "' (available: square-crop)";
      o.layout = *v;
    } else if (a == "--snapshot") {
      auto v = value();
      if (!v) return std::string("--snapshot needs a file name");
      o.snapshot = *v;
    } else if (a == "--plugin") {
      auto v = value();
      if (!v || v->empty()) return std::string("--plugin needs a plugin name or file");
      o.plugins.push_back(*v);
    } else if (a == "--windowed") {
      o.windowed = true;
    } else if (a == "--stats") {
      o.stats = true;
    } else if (a == "--bench") {
      o.bench = true;
    } else if (a.starts_with("-")) {
      return "unknown option " + std::string(a) + "\n\n" + usage();
    } else if (!haveFolder) {
      o.photoDir = std::string(a);
      haveFolder = true;
    } else {
      return "only one photo folder can be given (got '" + std::string(a) + "')";
    }
  }
  return o;
}

}  // namespace pb
