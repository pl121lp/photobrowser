// PhotoBrowse video plugin: video files appear in the grid as their first frame with a
// play badge; opened in whole-photo view, a click plays / pauses (with sound).
// Built on FFmpeg (libavformat, libavcodec, libavutil, libswscale, libswresample).
// Talks to the application only through src/plugin/photobrowse_plugin.h.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "iso6709.h"

#include "plugin/photobrowse_plugin.h"

namespace {

const PbHost* g_host = nullptr;

void logMsg(int level, const std::string& msg) {
  if (g_host && g_host->log) g_host->log(g_host->ctx, level, msg.c_str());
}

// Player threads still running (they outlive player_close so it never blocks).
std::mutex g_threadsMutex;
std::condition_variable g_threadsDone;
int g_threads = 0;

// ---- Time ------------------------------------------------------------------------

int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + int64_t(doe) - 719468;
}

// "2026-03-21T14:13:40.000000Z" -> seconds since 1970 (UTC).
std::optional<int64_t> parseIsoUtc(const char* s) {
  int y, mo, d, h, mi, sec;
  if (!s || std::sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return std::nullopt;
  if (y < 1971 || mo < 1 || mo > 12 || d < 1 || d > 31) return std::nullopt;  // 1970/1904 = unset
  return daysFromCivil(y, unsigned(mo), unsigned(d)) * 86400 + h * 3600 + mi * 60 + sec;
}

// The library sorts photos by their EXIF wall-clock time; videos store UTC, so convert
// with this computer's time zone (right when it matches where the video was taken).
int64_t utcToLocalWallClock(int64_t utc) {
  const std::time_t t = std::time_t(utc);
  std::tm lt{};
#ifdef _WIN32
  if (localtime_s(&lt, &t) != 0) return utc;
#else
  if (!localtime_r(&t, &lt)) return utc;
#endif
  return daysFromCivil(lt.tm_year + 1900, unsigned(lt.tm_mon + 1), unsigned(lt.tm_mday)) * 86400 +
         lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
}

double steadySeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- FFmpeg helpers ----------------------------------------------------------------

struct FormatClose {
  void operator()(AVFormatContext* f) const { avformat_close_input(&f); }
};
struct CodecFree {
  void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct FrameFree {
  void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketFree {
  void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct SwsFree {
  void operator()(SwsContext* s) const { sws_freeContext(s); }
};
struct SwrFree {
  void operator()(SwrContext* s) const { swr_free(&s); }
};
using FormatPtr = std::unique_ptr<AVFormatContext, FormatClose>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecFree>;
using FramePtr = std::unique_ptr<AVFrame, FrameFree>;
using PacketPtr = std::unique_ptr<AVPacket, PacketFree>;

struct Source {
  FormatPtr fmt;
  int video = -1, audio = -1;
  int rotation = 0;  // clockwise degrees to display upright: 0, 90, 180, 270
  uint32_t width = 0, height = 0;  // coded frame size (before rotation), square pixels

  AVStream* videoStream() const { return fmt->streams[video]; }
  uint32_t displayW() const { return rotation % 180 ? height : width; }
  uint32_t displayH() const { return rotation % 180 ? width : height; }
};

std::optional<Source> openSource(const char* path) {
  AVFormatContext* raw = nullptr;
  if (avformat_open_input(&raw, path, nullptr, nullptr) < 0) return std::nullopt;
  Source s;
  s.fmt.reset(raw);
  if (avformat_find_stream_info(raw, nullptr) < 0) return std::nullopt;
  for (unsigned i = 0; i < raw->nb_streams; ++i) {
    const AVStream* st = raw->streams[i];
    const AVCodecParameters* par = st->codecpar;
    if (par->codec_type == AVMEDIA_TYPE_VIDEO && !(st->disposition & AV_DISPOSITION_ATTACHED_PIC) && s.video < 0 &&
        par->width > 0 && par->height > 0) {
      s.video = int(i);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO && s.audio < 0) {
      s.audio = int(i);
    }
  }
  if (s.video < 0) return std::nullopt;
  const AVCodecParameters* par = s.videoStream()->codecpar;
  s.width = uint32_t(par->width);
  s.height = uint32_t(par->height);
  const AVRational sar = par->sample_aspect_ratio;
  if (sar.num > 0 && sar.den > 0 && sar.num != sar.den) s.width = uint32_t(std::lround(s.width * av_q2d(sar)));
  if (const AVPacketSideData* sd =
          av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX)) {
    const double angle = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    if (!std::isnan(angle)) s.rotation = ((int(std::lround(-angle / 90.0)) * 90) % 360 + 360) % 360;
  }
  return s;
}

CodecPtr openDecoder(const AVStream* st, int threads) {
  const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!codec) return nullptr;
  CodecPtr c(avcodec_alloc_context3(codec));
  if (!c || avcodec_parameters_to_context(c.get(), st->codecpar) < 0) return nullptr;
  c->thread_count = threads;
  c->pkt_timebase = st->time_base;
  if (avcodec_open2(c.get(), codec, nullptr) < 0) return nullptr;
  return c;
}

// ---- Colour: decoded frame -> RGBA8 sRGB ---------------------------------------------

// HDR (HLG / PQ, BT.2020) is tone-mapped so SDR reference white (203 nits) stays white
// and highlights roll off smoothly; SDR video converts directly.
// libswscale may store whole SIMD blocks past the end of a row (its unscaled YUV->RGB
// path works in 16-pixel steps), so it always writes into this class's padded buffer,
// never straight into `out`: a width like 1080 used to overrun the heap by 32 bytes.
class ColorConverter {
 public:
  // Writes w x h RGBA (tightly packed) scaled from the frame.
  bool convert(const AVFrame* f, uint32_t w, uint32_t h, uint8_t* out, bool still) {
    const auto trc = f->color_trc;
    const bool hdr = trc == AVCOL_TRC_ARIB_STD_B67 || trc == AVCOL_TRC_SMPTE2084;
    const AVPixelFormat dst = hdr ? AV_PIX_FMT_RGB48 : AV_PIX_FMT_RGBA;
    const bool shrink = still && (uint32_t(f->width) > 2 * w || uint32_t(f->height) > 2 * h);
    sws_.reset(sws_getCachedContext(sws_.release(), f->width, f->height, AVPixelFormat(f->format), int(w), int(h),
                                    dst, shrink ? SWS_AREA : SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!sws_) return false;
    int cs = f->colorspace;
    if (cs == AVCOL_SPC_UNSPECIFIED || cs == AVCOL_SPC_RGB) cs = f->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    if (cs == AVCOL_SPC_BT2020_CL) cs = AVCOL_SPC_BT2020_NCL;
    sws_setColorspaceDetails(sws_.get(), sws_getCoefficients(cs), f->color_range == AVCOL_RANGE_JPEG,
                             sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
    const size_t stride = paddedWidth(w) * (hdr ? 6 : 4);  // bytes per scaled row
    scaled_.resize(stride * h + kSlack);
    uint8_t* data[4] = {scaled_.data(), nullptr, nullptr, nullptr};
    int lines[4] = {int(stride), 0, 0, 0};
    sws_scale(sws_.get(), f->data, f->linesize, 0, f->height, data, lines);
    if (!hdr) {
      for (uint32_t y = 0; y < h; ++y, out += size_t(w) * 4) {
        std::memcpy(out, scaled_.data() + y * stride, size_t(w) * 4);
        for (size_t i = 3; i < size_t(w) * 4; i += 4) out[i] = 255;
      }
      return true;
    }
    buildLuts(trc);
    const bool wide = f->color_primaries == AVCOL_PRI_BT2020;
    static constexpr float k2020to709[9] = {1.6605f, -0.5876f, -0.0728f, -0.1246f, 1.1329f,
                                            -0.0083f, -0.0182f, -0.1006f, 1.1187f};
    const float scale = float(kOutLut - 1) / kMaxLinear;
    for (uint32_t y = 0; y < h; ++y) {
      const auto* src = reinterpret_cast<const uint16_t*>(scaled_.data() + y * stride);
      for (uint32_t x = 0; x < w; ++x, src += 3, out += 4) {
        const float r = toLinear_[src[0] >> 6], g = toLinear_[src[1] >> 6], b = toLinear_[src[2] >> 6];
        float c[3] = {r, g, b};
        if (wide) {
          c[0] = k2020to709[0] * r + k2020to709[1] * g + k2020to709[2] * b;
          c[1] = k2020to709[3] * r + k2020to709[4] * g + k2020to709[5] * b;
          c[2] = k2020to709[6] * r + k2020to709[7] * g + k2020to709[8] * b;
        }
        for (int k = 0; k < 3; ++k) {
          const float v = std::clamp(c[k], 0.0f, kMaxLinear);
          out[k] = toSrgb_[size_t(v * scale)];
        }
        out[3] = 255;
      }
    }
    return true;
  }

 private:
  static constexpr size_t kInLut = 1024;   // 10-bit signal
  static constexpr size_t kOutLut = 65536;
  static constexpr float kMaxLinear = 50.0f;  // relative to SDR white (PQ peaks at 10000 / 203)
  static constexpr size_t kSlack = 256;       // bytes after the last row, for good measure

  static size_t paddedWidth(uint32_t w) { return (size_t(w) + 31) & ~size_t(31); }  // pixels

  void buildLuts(int trc) {
    if (lutTrc_ == trc) return;
    lutTrc_ = trc;
    toLinear_.resize(kInLut);
    for (size_t i = 0; i < kInLut; ++i) {
      const double e = double(i) / double(kInLut - 1);
      double nits;
      if (trc == AVCOL_TRC_ARIB_STD_B67) {
        // HLG inverse OETF, then the OOTF for a 1000-nit display (gamma 1.2).
        constexpr double a = 0.17883277, b = 0.28466892, c = 0.55991073;
        const double scene = e <= 0.5 ? e * e / 3.0 : (std::exp((e - c) / a) + b) / 12.0;
        nits = 1000.0 * std::pow(scene, 1.2);
      } else {
        // PQ (SMPTE ST 2084) EOTF.
        constexpr double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
        const double p = std::pow(e, 1.0 / m2);
        nits = 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
      }
      toLinear_[i] = float(nits / 203.0);
    }
    toSrgb_.resize(kOutLut);
    for (size_t i = 0; i < kOutLut; ++i) {
      const double x = double(i) / double(kOutLut - 1) * kMaxLinear;
      // Identity up to the knee, then an exponential shoulder approaching white.
      constexpr double knee = 0.75;
      const double y = x <= knee ? x : knee + (1 - knee) * (1 - std::exp(-(x - knee) / (1 - knee)));
      const double s = y <= 0.0031308 ? 12.92 * y : 1.055 * std::pow(y, 1 / 2.4) - 0.055;
      toSrgb_[i] = uint8_t(std::lround(std::clamp(s, 0.0, 1.0) * 255));
    }
  }

  std::unique_ptr<SwsContext, SwsFree> sws_;
  std::vector<uint8_t> scaled_;  // padded rows: RGBA, or RGB48 for HDR
  int lutTrc_ = -1;
  std::vector<float> toLinear_;
  std::vector<uint8_t> toSrgb_;
};

// Rotates RGBA w x h clockwise by `deg` (0/90/180/270) into dst.
void rotateRgba(const uint8_t* src, uint32_t w, uint32_t h, int deg, uint8_t* dst) {
  const auto* s = reinterpret_cast<const uint32_t*>(src);
  auto* d = reinterpret_cast<uint32_t*>(dst);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const uint32_t p = s[size_t(y) * w + x];
      switch (deg) {
        case 90: d[size_t(x) * h + (h - 1 - y)] = p; break;
        case 180: d[size_t(h - 1 - y) * w + (w - 1 - x)] = p; break;
        case 270: d[size_t(w - 1 - x) * h + y] = p; break;
        default: d[size_t(y) * w + x] = p; break;
      }
    }
  }
}

// Semi-transparent disc with a play triangle in the middle, anti-aliased.
void drawPlayBadge(uint8_t* rgba, uint32_t w, uint32_t h) {
  const double r = std::max(2.0, 0.14 * std::min(w, h));
  const double cx = w * 0.5, cy = h * 0.5;
  // Triangle pointing right, its centroid at the disc centre.
  const double ax = cx - 0.3 * r, ay = cy - 0.5 * r, bx = cx - 0.3 * r, by = cy + 0.5 * r, tx = cx + 0.55 * r, ty = cy;
  auto edge = [](double x0, double y0, double x1, double y1, double px, double py) {
    const double ex = x1 - x0, ey = y1 - y0, len = std::hypot(ex, ey);
    return ((px - x0) * ey - (py - y0) * ex) / len;  // < 0 on the inner side (clockwise on screen)
  };
  const int x0 = std::max(0, int(cx - r - 1)), x1 = std::min(int(w), int(cx + r + 2));
  const int y0 = std::max(0, int(cy - r - 1)), y1 = std::min(int(h), int(cy + r + 2));
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const double px = x + 0.5, py = y + 0.5;
      const double disc = std::clamp(r - std::hypot(px - cx, py - cy) + 0.5, 0.0, 1.0);
      if (disc <= 0) continue;
      const double dist = std::max({edge(ax, ay, tx, ty, px, py), edge(tx, ty, bx, by, px, py),
                                    edge(bx, by, ax, ay, px, py)});  // signed distance, < 0 inside
      const double tri = std::clamp(-dist + 0.5, 0.0, 1.0);
      uint8_t* p = rgba + (size_t(y) * w + x) * 4;
      for (int k = 0; k < 3; ++k) {
        double v = p[k] * (1 - 0.45 * disc);           // darken under the disc
        v = v + (240 - v) * tri * 0.92;                // white triangle
        p[k] = uint8_t(std::clamp(v, 0.0, 255.0));
      }
    }
  }
}

// Decoded upright size for a requested long edge (never larger than the video).
void fitSize(const Source& s, uint32_t longEdge, uint32_t& w, uint32_t& h) {
  const uint32_t natural = std::max(s.width, s.height);
  const double k = std::min(1.0, double(std::max(longEdge, 1u)) / natural);
  w = std::max(1u, uint32_t(std::lround(s.width * k)));
  h = std::max(1u, uint32_t(std::lround(s.height * k)));
}

// Converts a decoded frame to upright RGBA of the source's pre-rotation size w x h.
bool frameToRgba(ColorConverter& cc, const Source& s, const AVFrame* f, uint32_t w, uint32_t h,
                 std::vector<uint8_t>& scratch, uint8_t* out, bool still) {
  if (s.rotation == 0) return cc.convert(f, w, h, out, still);
  scratch.resize(size_t(w) * h * 4);
  if (!cc.convert(f, w, h, scratch.data(), still)) return false;
  rotateRgba(scratch.data(), w, h, s.rotation, out);
  return true;
}

// ---- Stills -----------------------------------------------------------------------------

int probe(void*, const char* path, PbMediaInfo* out) {
  auto s = openSource(path);
  if (!s) return 0;
  out->width = s->displayW();
  out->height = s->displayH();
  out->taken_at = 0;
  const AVDictionaryEntry* t = av_dict_get(s->fmt->metadata, "creation_time", nullptr, 0);
  if (!t) t = av_dict_get(s->videoStream()->metadata, "creation_time", nullptr, 0);
  if (t) {
    if (auto utc = parseIsoUtc(t->value)) out->taken_at = utcToLocalWallClock(*utc);
  }
  // "location" (Android, most MP4s; FFmpeg's name for the ©xyz atom) or Apple's key.
  const AVDictionaryEntry* loc = av_dict_get(s->fmt->metadata, "location", nullptr, 0);
  if (!loc) loc = av_dict_get(s->fmt->metadata, "com.apple.quicktime.location.ISO6709", nullptr, 0);
  if (loc) {
    if (auto p = pbvideo::parseIso6709(loc->value)) {
      out->has_gps = 1;
      out->latitude = p->lat;
      out->longitude = p->lon;
      out->has_altitude = p->altitude.has_value();
      out->altitude = p->altitude.value_or(0);
    }
  }
  return 1;
}

int decodeStill(void*, const char* path, uint32_t minLongEdge, PbImageOut* out) {
  auto s = openSource(path);
  if (!s) return 0;
  CodecPtr dec = openDecoder(s->videoStream(), 2);
  if (!dec) return 0;
  PacketPtr pkt(av_packet_alloc());
  FramePtr frame(av_frame_alloc());
  bool got = false, flushed = false;
  for (int packets = 0; !got && packets < 2000;) {
    const int r = avcodec_receive_frame(dec.get(), frame.get());
    if (r == 0) {
      got = true;
    } else if (r == AVERROR(EAGAIN) && !flushed) {
      if (av_read_frame(s->fmt.get(), pkt.get()) < 0) {
        avcodec_send_packet(dec.get(), nullptr);
        flushed = true;
        continue;
      }
      if (pkt->stream_index == s->video) {
        avcodec_send_packet(dec.get(), pkt.get());
        ++packets;
      }
      av_packet_unref(pkt.get());
    } else {
      break;
    }
  }
  if (!got) return 0;
  uint32_t w, h;
  fitSize(*s, minLongEdge, w, h);
  const uint32_t dw = s->rotation % 180 ? h : w, dh = s->rotation % 180 ? w : h;
  uint8_t* px = out->alloc(out->ctx, dw, dh);
  if (!px) return 0;
  ColorConverter cc;
  std::vector<uint8_t> scratch;
  if (!frameToRgba(cc, *s, frame.get(), w, h, scratch, px, true)) return 0;
  drawPlayBadge(px, dw, dh);
  return 1;
}

// ---- Playback ------------------------------------------------------------------------------

constexpr size_t kMaxQueuedFrames = 4;
constexpr size_t kMaxQueuedPackets = 1024;  // compressed; bounds memory if audio runs far ahead
constexpr double kAudioAhead = 0.5;         // seconds of decoded audio kept queued
constexpr uint32_t kMaxFrameEdge = 3840;

struct VideoFrame {
  double pts = 0;
  std::vector<uint8_t> rgba;
};

// Shared between the main thread (player_* calls) and the decode thread.
struct PlayState {
  std::mutex m;
  std::condition_variable cv;
  std::string path;
  bool stop = false, paused = false, restart = false;
  bool decodeDone = false, failed = false, ended = false;
  uint32_t w = 0, h = 0;  // upright frame size
  std::deque<VideoFrame> queue;
  std::vector<std::vector<uint8_t>> pool;  // recycled frame buffers
  VideoFrame shown;
  bool haveShown = false;
  uint64_t serial = 0;
  // Media clock: wall time since the first frame, minus pauses.
  bool clockStarted = false;
  double clockBase = 0, clockAnchor = 0;
  PbAudioOut* audio = nullptr;

  double clock() const {  // with m held
    if (!clockStarted) return -1e9;
    return paused ? clockBase : clockBase + (steadySeconds() - clockAnchor);
  }
};

}  // namespace

struct PbPlayer {
  std::shared_ptr<PlayState> st;
  bool started = false;
};

namespace {

// Decode thread: demuxes, decodes audio straight into the host's audio output and
// video into a small queue of RGBA frames that player_frame shows at their time.
void playLoop(std::shared_ptr<PlayState> st) {
  auto fail = [&](const char* why) {
    std::lock_guard lock(st->m);
    st->failed = true;
    logMsg(PB_LOG_WARNING, std::string(why) + ": " + st->path);
  };
  auto s = openSource(st->path.c_str());
  if (!s) return fail("cannot open video");
  CodecPtr vdec = openDecoder(s->videoStream(), 0);
  if (!vdec) return fail("no decoder for video");
  CodecPtr adec;
  std::unique_ptr<SwrContext, SwrFree> swr;
  int outChannels = 0, outRate = 0;
  if (s->audio >= 0 && (adec = openDecoder(s->fmt->streams[s->audio], 1))) {
    outChannels = std::min(2, std::max(1, adec->ch_layout.nb_channels));
    outRate = adec->sample_rate;
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, outChannels);
    SwrContext* raw = nullptr;
    if (swr_alloc_set_opts2(&raw, &outLayout, AV_SAMPLE_FMT_FLT, outRate, &adec->ch_layout, adec->sample_fmt,
                            adec->sample_rate, 0, nullptr) == 0 &&
        swr_init(raw) == 0) {
      swr.reset(raw);
      PbAudioOut* audio = g_host->audio_open(g_host->ctx, outRate, outChannels);
      std::lock_guard lock(st->m);
      st->audio = audio;
    } else {
      swr_free(&raw);
    }
  }

  uint32_t w, h;
  fitSize(*s, kMaxFrameEdge, w, h);
  {
    std::lock_guard lock(st->m);
    st->w = s->rotation % 180 ? h : w;
    st->h = s->rotation % 180 ? w : h;
  }
  const double timeBase = av_q2d(s->videoStream()->time_base);
  ColorConverter cc;
  std::vector<uint8_t> scratch;
  std::vector<float> samples;
  std::deque<PacketPtr> videoPackets;
  PacketPtr pkt(av_packet_alloc());
  FramePtr frame(av_frame_alloc()), aframe(av_frame_alloc());
  bool demuxEof = false, videoFlushed = false, videoEof = false;

  auto writeAudio = [&](const AVFrame* f) {
    const int cap = swr_get_out_samples(swr.get(), f->nb_samples);
    if (cap <= 0) return;
    samples.resize(size_t(cap) * outChannels);
    uint8_t* outPtr = reinterpret_cast<uint8_t*>(samples.data());
    const int n = swr_convert(swr.get(), &outPtr, cap, const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
    if (n > 0 && st->audio) g_host->audio_write(st->audio, samples.data(), uint32_t(n));
  };
  auto decodeAudio = [&](const AVPacket* p) {
    if (!swr || avcodec_send_packet(adec.get(), p) < 0) return;
    while (avcodec_receive_frame(adec.get(), aframe.get()) == 0) writeAudio(aframe.get());
  };

  while (true) {
    size_t queued;
    bool paused;
    {
      std::unique_lock lock(st->m);
      if (st->stop) break;
      if (st->restart) {
        st->restart = false;
        av_seek_frame(s->fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(vdec.get());
        if (adec) avcodec_flush_buffers(adec.get());
        videoPackets.clear();
        for (auto& f : st->queue) st->pool.push_back(std::move(f.rgba));
        st->queue.clear();
        if (st->audio) {
          g_host->audio_pause(st->audio, 1);  // resumes with the first frame
          g_host->audio_clear(st->audio);
        }
        st->clockStarted = false;
        st->decodeDone = st->ended = false;
        demuxEof = videoFlushed = videoEof = false;
      }
      queued = st->queue.size();
      paused = st->paused;
    }
    bool worked = false;

    // Video: decode one frame if the queue has room.
    if (!videoEof && queued < kMaxQueuedFrames) {
      const int r = avcodec_receive_frame(vdec.get(), frame.get());
      if (r == 0) {
        VideoFrame vf;
        {
          std::lock_guard lock(st->m);
          if (!st->pool.empty()) {
            vf.rgba = std::move(st->pool.back());
            st->pool.pop_back();
          }
        }
        vf.rgba.resize(size_t(w) * h * 4);
        const int64_t ts = frame->best_effort_timestamp;
        vf.pts = ts == AV_NOPTS_VALUE ? 0 : ts * timeBase;
        if (frameToRgba(cc, *s, frame.get(), w, h, scratch, vf.rgba.data(), false)) {
          std::lock_guard lock(st->m);
          if (!st->clockStarted) {
            // The clock starts with the first frame so opening the file costs no frames.
            st->clockStarted = true;
            st->clockBase = vf.pts;
            st->clockAnchor = steadySeconds();
            if (st->audio && !st->paused) g_host->audio_pause(st->audio, 0);
          }
          st->queue.push_back(std::move(vf));
        }
        av_frame_unref(frame.get());
        worked = true;
      } else if (r == AVERROR(EAGAIN)) {
        if (!videoPackets.empty()) {
          avcodec_send_packet(vdec.get(), videoPackets.front().get());
          videoPackets.pop_front();
          worked = true;
        } else if (demuxEof && !videoFlushed) {
          avcodec_send_packet(vdec.get(), nullptr);
          videoFlushed = true;
          worked = true;
        }
      } else {
        videoEof = true;
        std::lock_guard lock(st->m);
        st->decodeDone = true;
      }
    }

    // Demux: keep video packets ready and the audio output a little ahead.
    const bool audioWants = st->audio && g_host->audio_queued_seconds(st->audio) < kAudioAhead;
    if (!demuxEof && videoPackets.size() < kMaxQueuedPackets &&
        (videoPackets.size() < 2 || (audioWants && !paused))) {
      if (av_read_frame(s->fmt.get(), pkt.get()) < 0) {
        demuxEof = true;
        if (adec) decodeAudio(nullptr);
      } else if (pkt->stream_index == s->video) {
        PacketPtr copy(av_packet_alloc());
        av_packet_move_ref(copy.get(), pkt.get());
        videoPackets.push_back(std::move(copy));
      } else if (pkt->stream_index == s->audio) {
        decodeAudio(pkt.get());
      }
      av_packet_unref(pkt.get());
      worked = true;
    }

    if (!worked) {
      std::unique_lock lock(st->m);
      st->cv.wait_for(lock, std::chrono::milliseconds(10));
    }
  }

  std::lock_guard lock(st->m);
  if (st->audio) g_host->audio_close(st->audio);
  st->audio = nullptr;
}

PbPlayer* playerOpen(void*, const char* path) {
  auto* p = new PbPlayer;
  p->st = std::make_shared<PlayState>();
  p->st->path = path;
  return p;  // nothing is read until the first click
}

void playerClose(PbPlayer* p) {
  {
    std::lock_guard lock(p->st->m);
    p->st->stop = true;
  }
  p->st->cv.notify_all();
  delete p;  // the decode thread holds its own reference and exits by itself
}

void playerEvent(PbPlayer* p, int event) {
  if (event != PB_EVENT_ACTIVATE) return;
  PlayState& st = *p->st;
  if (!p->started) {
    p->started = true;
    {
      std::lock_guard lock(g_threadsMutex);
      ++g_threads;
    }
    std::thread([st = p->st] {
      playLoop(st);
      std::lock_guard lock(g_threadsMutex);
      --g_threads;
      g_threadsDone.notify_all();
    }).detach();
    return;
  }
  {
    std::lock_guard lock(st.m);
    if (st.failed) return;
    if (st.ended) {
      st.restart = true;  // play again from the start
      st.paused = false;
    } else if (st.paused) {
      st.paused = false;
      st.clockAnchor = steadySeconds();
      if (st.audio && st.clockStarted) g_host->audio_pause(st.audio, 0);
    } else {
      st.clockBase = st.clock();
      st.paused = true;
      if (st.audio) g_host->audio_pause(st.audio, 1);
    }
  }
  st.cv.notify_all();
}

int playerFrame(PbPlayer* p, PbFrame* out) {
  if (!p->started) return 0;
  PlayState& st = *p->st;
  bool popped = false;
  {
    std::lock_guard lock(st.m);
    const double now = st.clock();
    // Paused: the picture freezes at once, even if frames due earlier decode late.
    while (!st.queue.empty() && st.queue.front().pts <= now && !(st.paused && st.haveShown)) {
      if (st.haveShown) st.pool.push_back(std::move(st.shown.rgba));
      st.shown = std::move(st.queue.front());
      st.queue.pop_front();
      st.haveShown = true;
      ++st.serial;
      popped = true;
    }
    if (st.decodeDone && st.queue.empty()) st.ended = true;
    if (!st.haveShown) return 0;
    *out = PbFrame{st.w, st.h, st.w * 4, st.shown.rgba.data(), st.serial};
  }
  if (popped) st.cv.notify_all();  // room in the queue
  return 1;
}

int playerActive(PbPlayer* p) {
  if (!p->started) return 0;
  std::lock_guard lock(p->st->m);
  return !p->st->paused && !p->st->ended && !p->st->failed;
}

void destroy(void*) {
  std::unique_lock lock(g_threadsMutex);
  g_threadsDone.wait(lock, [] { return g_threads == 0; });
}

const char* const kExtensions[] = {".mp4", ".mov", ".m4v", ".mkv", ".webm", ".avi", ".3gp", nullptr};

const PbPlugin kPlugin = {
    PB_PLUGIN_ABI_VERSION,
    sizeof(PbPlugin),
    "video",
    "1.1",
    "Video files (FFmpeg): first frame in the grid, click to play / pause in whole-photo view",
    nullptr,
    kExtensions,
    probe,
    decodeStill,
    playerOpen,
    playerClose,
    playerEvent,
    playerFrame,
    playerActive,
    destroy,
    nullptr,  // no map tiles
    0,
    0,
    nullptr,
};

}  // namespace

extern "C" PB_PLUGIN_EXPORT const PbPlugin* pb_plugin_entry(const PbHost* host) {
  if (!host || host->abi_version != PB_PLUGIN_ABI_VERSION || host->struct_size < sizeof(PbHost)) return nullptr;
  g_host = host;
  av_log_set_level(AV_LOG_FATAL);
  return &kPlugin;
}
