#include "plugin/plugin_host.h"

#include <cstdio>
#include <cstring>

#include "platform/dynlib.h"
#include "platform/paths.h"
#include "plugin/photobrowse_plugin.h"

namespace pb {

namespace fs = std::filesystem;

namespace {

// The plugin's PbMediaInfo / decode_still as a MediaHandler.
class PluginMediaHandler final : public MediaHandler {
 public:
  explicit PluginMediaHandler(const PbPlugin& p) : p_(p) {}

  std::optional<MediaProbe> probe(const fs::path& abs) const override {
    if (!p_.probe) return std::nullopt;
    PbMediaInfo info{};
    if (!p_.probe(p_.self, toUtf8(abs).c_str(), &info)) return std::nullopt;
    return MediaProbe{info.width, info.height, info.taken_at};
  }

  std::optional<Image> decodeStill(const fs::path& abs, uint32_t minLongEdge) const override {
    if (!p_.decode_still) return std::nullopt;
    Image img;
    PbImageOut out{&img, [](void* ctx, uint32_t w, uint32_t h) -> uint8_t* {
                     auto* im = static_cast<Image*>(ctx);
                     if (!im->rgba.empty() || w == 0 || h == 0 || w > 65536 || h > 65536) return nullptr;
                     im->w = w;
                     im->h = h;
                     im->rgba.resize(size_t(w) * h * 4);
                     return im->rgba.data();
                   }};
    if (!p_.decode_still(p_.self, toUtf8(abs).c_str(), minLongEdge, &out) || img.empty()) return std::nullopt;
    return img;
  }

 private:
  const PbPlugin& p_;
};

AudioBackend::Stream* stream(PbAudioOut* out) { return reinterpret_cast<AudioBackend::Stream*>(out); }

bool looksLikePath(const std::string& s) {
  if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos) return true;
  const std::string e = lowerExtension(s);
  return e == ".so" || e == ".dll" || e == ".dylib";
}

}  // namespace

struct PluginHost::Loaded {
  void* library = nullptr;
  const PbPlugin* api = nullptr;
  PbHost host{};
  AudioBackend* audio = nullptr;
  std::string name;  // for log messages
  std::unique_ptr<PluginMediaHandler> media;

  ~Loaded() {
    if (api && api->destroy) api->destroy(api->self);
    if (library) closeLibrary(library);
  }
};

Player::~Player() {
  if (plugin_.player_close) plugin_.player_close(player_);
}

void Player::activate() {
  if (plugin_.player_event) plugin_.player_event(player_, PB_EVENT_ACTIVATE);
}

std::optional<LiveFrame> Player::frame() {
  PbFrame f{};
  if (!plugin_.player_frame || !plugin_.player_frame(player_, &f) || !f.rgba || !f.width || !f.height ||
      f.stride < f.width * 4 || f.stride % 4) {
    return std::nullopt;
  }
  return LiveFrame{f.width, f.height, f.stride, f.rgba, f.serial};
}

bool Player::active() { return plugin_.player_active && plugin_.player_active(player_); }

PluginHost::PluginHost(AudioBackend* audio) : audio_(audio) {}

PluginHost::~PluginHost() {
  while (!loaded_.empty()) loaded_.pop_back();
}

std::vector<fs::path> pluginCandidates(const std::string& nameOrPath, const std::vector<fs::path>& searchDirs) {
  if (looksLikePath(nameOrPath)) return {fromUtf8(nameOrPath)};
  std::vector<fs::path> out;
  for (const auto& dir : searchDirs) out.push_back(dir / sharedLibraryName(nameOrPath));
  return out;
}

std::optional<std::string> PluginHost::load(const std::string& nameOrPath, const std::vector<fs::path>& searchDirs) {
  const auto candidates = pluginCandidates(nameOrPath, searchDirs);
  fs::path path;
  for (const auto& c : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(c, ec)) {
      path = c;
      break;
    }
  }
  if (path.empty()) {
    std::string msg = "plugin '" + nameOrPath + "' not found (looked for";
    for (const auto& c : candidates) msg += " " + toUtf8(c);
    return msg + ")";
  }
  for (const Info& i : infos_) {
    if (std::error_code ec; fs::equivalent(i.path, path, ec)) return std::nullopt;  // already loaded
  }

  auto l = std::make_unique<Loaded>();
  l->audio = audio_;
  std::string error;
  l->library = openLibrary(path, error);
  if (!l->library) return "plugin " + toUtf8(path) + ": " + error;
  auto entry = reinterpret_cast<PbPluginEntry>(librarySymbol(l->library, PB_PLUGIN_ENTRY_NAME));
  if (!entry) return "plugin " + toUtf8(path) + ": no " PB_PLUGIN_ENTRY_NAME " function";

  l->name = toUtf8(path.stem());
  for (const char* prefix : {"libpb_", "pb_"}) {
    if (l->name.starts_with(prefix) && l->name.size() > std::strlen(prefix)) {
      l->name.erase(0, std::strlen(prefix));
      break;
    }
  }
  PbHost& h = l->host;
  h.abi_version = PB_PLUGIN_ABI_VERSION;
  h.struct_size = sizeof(PbHost);
  h.ctx = l.get();
  h.log = [](void* ctx, int level, const char* message) {
    const char* tag = level >= PB_LOG_ERROR ? "error: " : level == PB_LOG_WARNING ? "warning: " : "";
    std::fprintf(level >= PB_LOG_WARNING ? stderr : stdout, "plugin %s: %s%s\n",
                 static_cast<Loaded*>(ctx)->name.c_str(), tag, message ? message : "");
  };
  h.audio_open = [](void* ctx, int rate, int channels) -> PbAudioOut* {
    auto* self = static_cast<Loaded*>(ctx);
    if (!self->audio || rate <= 0 || channels <= 0) return nullptr;
    return reinterpret_cast<PbAudioOut*>(self->audio->open(rate, channels).release());
  };
  h.audio_write = [](PbAudioOut* out, const float* samples, uint32_t frames) {
    if (out && samples) stream(out)->write(samples, frames);
  };
  h.audio_queued_seconds = [](PbAudioOut* out) { return out ? stream(out)->queuedSeconds() : 0.0; };
  h.audio_pause = [](PbAudioOut* out, int paused) {
    if (out) stream(out)->pause(paused != 0);
  };
  h.audio_clear = [](PbAudioOut* out) {
    if (out) stream(out)->clear();
  };
  h.audio_close = [](PbAudioOut* out) { delete stream(out); };

  l->api = entry(&l->host);
  if (!l->api) return "plugin " + toUtf8(path) + " declined to load (incompatible version?)";
  const PbPlugin& p = *l->api;
  if (p.abi_version != PB_PLUGIN_ABI_VERSION || p.struct_size < sizeof(PbPlugin)) {
    const std::string msg = "plugin " + toUtf8(path) + " was built for plugin API version " +
                            std::to_string(p.abi_version) + ", this program uses " +
                            std::to_string(PB_PLUGIN_ABI_VERSION);
    l->api = nullptr;  // don't call destroy through a table we can't trust
    return msg;
  }

  Info info{p.name ? p.name : l->name, p.version ? p.version : "", p.description ? p.description : "", {}, path};
  if (p.name) l->name = p.name;
  l->media = std::make_unique<PluginMediaHandler>(p);
  for (const char* const* e = p.extensions; e && *e; ++e) {
    info.extensions.emplace_back(*e);
    formats_.add(*e, l->media.get());
  }
  infos_.push_back(std::move(info));
  loaded_.push_back(std::move(l));
  return std::nullopt;
}

std::unique_ptr<Player> PluginHost::openPlayer(const fs::path& abs) {
  const MediaHandler* h = formats_.find(toUtf8(abs.filename()));
  if (!h) return nullptr;
  for (const auto& l : loaded_) {
    if (l->media.get() != h) continue;
    const PbPlugin& p = *l->api;
    if (!p.player_open) return nullptr;
    PbPlayer* player = p.player_open(p.self, toUtf8(abs).c_str());
    return player ? std::make_unique<Player>(p, player) : nullptr;
  }
  return nullptr;
}

}  // namespace pb
