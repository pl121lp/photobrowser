#pragma once

#include <string>

#include "imaging/image.h"

namespace pb {

enum class TileStatus {
  Ok,
  Missing,  // no such tile: draw the ancestor, don't retry
  Offline,  // network unreachable: retry later
  Refused,  // the server blocked or rate-limited us: back off
  Error,    // anything else: treat as missing for this session
};

struct TileResult {
  TileStatus status = TileStatus::Error;
  Image image;  // 256x256 RGBA when Ok
};

// Where map tiles come from (a plugin, see src/plugin/plugin_host.h). Web Mercator
// XYZ tiles of 256 px.
class TileSource {
 public:
  virtual ~TileSource() = default;
  virtual TileResult get(int z, int x, int y) const = 0;  // worker threads; may block on the network
  virtual std::string attribution() const = 0;           // shown on the map, e.g. "© OpenStreetMap contributors"
  virtual int minZoom() const = 0;
  virtual int maxZoom() const = 0;
};

}  // namespace pb
