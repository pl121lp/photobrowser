#include "app/options.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "layout/layout.h"

namespace pb {

std::string usage() {
  return "Usage: photobrowse [photo-folder] [options]\n"
         "\n"
         "  photo-folder          folder with JPEG and PNG photos (subfolders too with\n"
         "                        [library] recursive = on)\n"
         "                        (default: [library] folder in the config file, else ./photos)\n"
         "  --config <file>       settings file (default: ~/.config/photobrowse/config.ini,\n"
         "                        created with documented defaults on first run)\n"
         "  --cache-dir <dir>     thumbnail cache root (default: per-user cache directory)\n"
         "  --rebuild-cache       discard this library's cache and re-index\n"
         "  --layout <name>       start layout: square or group_year (default: [view] layout,\n"
         "                        else square); F4 cycles the layouts\n"
         "  --windowed            start in a window instead of fullscreen\n"
         "  --stats               start with the stats display on (same as pressing F3)\n"
         "  --bench               run a scripted pan/zoom benchmark, print frame times, exit\n"
         "  --plugin <name|path>  load a plugin, e.g. --plugin video (repeatable); a name is\n"
         "                        looked up in the plugins/ folder next to the program\n"
         "  --all-plugins         load every plugin in that folder (after any --plugin ones);\n"
         "                        one that fails to load is skipped with a warning\n"
         "  --filter <types>      leave these file types out, e.g. --filter png,mp4 (jpg also\n"
         "                        covers .jpeg); the cache keeps them, so no re-index\n"
         "  --snapshot <file.jpg> save the start view (or the end of --bench) as JPEG and exit\n"
         "  --help                show this help\n"
         "\n"
         "Plugins (built when their libraries are installed; see README):\n"
         "  heif                  HEIC/HEIF photos (e.g. from iPhones) in the grid, with date\n"
         "                        and GPS for the map (libheif + libde265)\n"
         "  video                 video files in the grid; click an open one to play (FFmpeg)\n"
         "  maptiles              map tiles for the map view (M): OpenStreetMap, cached on\n"
         "                        disk; settings in [plugin.maptiles] (libcurl)\n"
         "  Several at once:      --plugin heif --plugin video --plugin maptiles\n";
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
      const std::string_view name = canonicalLayoutName(*v);
      if (name.empty()) return "unknown layout '" + *v + "' (available: square, group_year)";
      o.layout = std::string(name);
    } else if (a == "--snapshot") {
      auto v = value();
      if (!v) return std::string("--snapshot needs a file name");
      o.snapshot = *v;
    } else if (a == "--plugin") {
      auto v = value();
      if (!v || v->empty()) return std::string("--plugin needs a plugin name or file");
      o.plugins.push_back(*v);
    } else if (a == "--filter") {
      auto v = value();
      if (!v || v->empty()) return std::string("--filter needs file types, e.g. --filter png,mp4");
      for (size_t start = 0; start <= v->size();) {
        const size_t comma = std::min(v->find(',', start), v->size());
        std::string t = v->substr(start, comma - start);
        std::erase_if(t, [](unsigned char c) { return std::isspace(c); });
        if (t.starts_with('.')) t.erase(0, 1);
        if (t.empty()) return "--filter: empty file type in '" + *v + "'";
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        o.filterTypes.push_back(std::move(t));
        start = comma + 1;
      }
    } else if (a == "--all-plugins") {
      o.allPlugins = true;
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
