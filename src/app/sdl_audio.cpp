#include "app/sdl_audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

namespace pb {

namespace {

class SdlStream final : public AudioBackend::Stream {
 public:
  SdlStream(SDL_AudioStream* s, int rate, int channels)
      : s_(s), frameBytes_(channels * 4), bytesPerSecond_(double(rate) * channels * 4) {}
  ~SdlStream() override { SDL_DestroyAudioStream(s_); }
  void write(const float* samples, uint32_t frames) override {
    SDL_PutAudioStreamData(s_, samples, int(frames) * frameBytes_);
  }
  double queuedSeconds() override { return std::max(0, SDL_GetAudioStreamQueued(s_)) / bytesPerSecond_; }
  void pause(bool paused) override {
    if (paused) {
      SDL_PauseAudioStreamDevice(s_);
    } else {
      SDL_ResumeAudioStreamDevice(s_);
    }
  }
  void clear() override { SDL_ClearAudioStream(s_); }

 private:
  SDL_AudioStream* s_;
  int frameBytes_;
  double bytesPerSecond_;
};

}  // namespace

SdlAudio::~SdlAudio() {
  if (ready_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SdlAudio::init() {
  ready_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
  if (!ready_) std::fprintf(stderr, "photobrowse: no audio (%s)\n", SDL_GetError());
  return ready_;
}

std::unique_ptr<AudioBackend::Stream> SdlAudio::open(int sampleRate, int channels) {
  if (!ready_) return nullptr;
  const SDL_AudioSpec spec{SDL_AUDIO_F32, channels, sampleRate};
  SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!s) return nullptr;  // the device starts paused
  return std::make_unique<SdlStream>(s, sampleRate, channels);
}

}  // namespace pb
