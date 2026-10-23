#pragma once

#include "plugin/plugin_host.h"

namespace pb {

// Plugin audio output through SDL3 audio streams (one device stream per player).
class SdlAudio final : public AudioBackend {
 public:
  ~SdlAudio() override;
  bool init();  // starts the SDL audio subsystem; false = no audio (plugins play silently)
  std::unique_ptr<Stream> open(int sampleRate, int channels) override;

 private:
  bool ready_ = false;
};

}  // namespace pb
