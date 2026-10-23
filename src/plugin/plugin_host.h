#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "library/media_format.h"

struct PbPlugin;
struct PbPlayer;

namespace pb {

// Audio output offered to plugins (SDL in the app, absent in tests).
class AudioBackend {
 public:
  class Stream {
   public:
    virtual ~Stream() = default;
    virtual void write(const float* samples, uint32_t frames) = 0;  // interleaved
    virtual double queuedSeconds() = 0;
    virtual void pause(bool paused) = 0;
    virtual void clear() = 0;
  };
  virtual ~AudioBackend() = default;
  virtual std::unique_ptr<Stream> open(int sampleRate, int channels) = 0;  // nullptr if unavailable
};

// A frame of live content; valid until the next call on its Player.
struct LiveFrame {
  uint32_t w = 0, h = 0, stride = 0;
  const uint8_t* rgba = nullptr;
  uint64_t serial = 0;  // changes when the pixels do
};

// Live content of one item (e.g. a playing video), opened while it is shown in
// whole-photo view. Main thread only; every call returns quickly.
class Player {
 public:
  Player(const PbPlugin& plugin, PbPlayer* player) : plugin_(plugin), player_(player) {}
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  void activate();                    // primary click on the item
  std::optional<LiveFrame> frame();   // nullopt = show the still image
  bool active();                      // changing by itself: keep redrawing

 private:
  const PbPlugin& plugin_;
  PbPlayer* player_;
};

// Loads plugins (shared libraries, see src/plugin/photobrowse_plugin.h) and exposes
// what they provide: media formats for the library and players for live content.
// Must outlive everything that uses its formats() or players.
class PluginHost {
 public:
  struct Info {
    std::string name, version, description;
    std::vector<std::string> extensions;
    std::filesystem::path path;
  };

  explicit PluginHost(AudioBackend* audio = nullptr);
  ~PluginHost();  // destroys and unloads plugins, last loaded first
  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  // Loads `nameOrPath`: a file path, or a plugin name looked up in searchDirs (see
  // pluginCandidates). Returns an error message on failure.
  std::optional<std::string> load(const std::string& nameOrPath, const std::vector<std::filesystem::path>& searchDirs);

  const std::vector<Info>& plugins() const { return infos_; }
  const MediaFormats& formats() const { return formats_; }
  // Player for a file, or nullptr if no loaded plugin offers live content for it.
  std::unique_ptr<Player> openPlayer(const std::filesystem::path& abs);

 private:
  struct Loaded;
  AudioBackend* audio_;
  std::vector<std::unique_ptr<Loaded>> loaded_;
  std::vector<Info> infos_;
  MediaFormats formats_;
};

// Files tried for --plugin <arg>: the path itself if it names a file (contains a
// directory separator or ends in a library extension), else
// <dir>/<sharedLibraryName(arg)> for each search dir.
std::vector<std::filesystem::path> pluginCandidates(const std::string& nameOrPath,
                                                    const std::vector<std::filesystem::path>& searchDirs);

}  // namespace pb
