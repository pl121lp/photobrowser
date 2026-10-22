#include "imaging/jpeg.h"

#include <turbojpeg.h>

#include <algorithm>

namespace pb {

namespace {

// One TurboJPEG handle per thread and direction.
struct Handles {
  tjhandle dec = tjInitDecompress();
  tjhandle enc = tjInitCompress();
  ~Handles() {
    if (dec) tjDestroy(dec);
    if (enc) tjDestroy(enc);
  }
};

Handles& handles() {
  thread_local Handles h;
  return h;
}

}  // namespace

std::optional<Image> decodeJpeg(std::span<const uint8_t> data, uint32_t minLongEdge) {
  tjhandle h = handles().dec;
  if (!h || data.empty()) return std::nullopt;
  int w = 0, ht = 0, subsamp = 0, colorspace = 0;
  if (tjDecompressHeader3(h, data.data(), static_cast<unsigned long>(data.size()), &w, &ht, &subsamp,
                          &colorspace) != 0) {
    return std::nullopt;
  }
  // Scales in increasing size order; pick the first that is large enough.
  static const tjscalingfactor kScales[] = {{1, 8}, {1, 4}, {1, 2}, {1, 1}};
  tjscalingfactor chosen{1, 1};
  if (minLongEdge > 0) {
    for (const auto& s : kScales) {
      if (uint32_t(std::max(TJSCALED(w, s), TJSCALED(ht, s))) >= minLongEdge) {
        chosen = s;
        break;
      }
    }
  }
  Image img;
  img.w = uint32_t(TJSCALED(w, chosen));
  img.h = uint32_t(TJSCALED(ht, chosen));
  img.rgba.resize(size_t(img.w) * img.h * 4);
  if (tjDecompress2(h, data.data(), static_cast<unsigned long>(data.size()), img.rgba.data(), int(img.w), 0,
                    int(img.h), TJPF_RGBA, TJFLAG_FASTDCT) != 0) {
    // Warnings (e.g. truncated data) still produce usable pixels.
    if (tjGetErrorCode(h) != TJERR_WARNING) return std::nullopt;
  }
  return img;
}

std::optional<std::vector<uint8_t>> encodeJpeg(const Image& img, int quality) {
  tjhandle h = handles().enc;
  if (!h || img.empty()) return std::nullopt;
  unsigned char* buf = nullptr;
  unsigned long size = 0;
  if (tjCompress2(h, img.rgba.data(), int(img.w), 0, int(img.h), TJPF_RGBA, &buf, &size, TJSAMP_420, quality,
                  TJFLAG_FASTDCT) != 0) {
    tjFree(buf);
    return std::nullopt;
  }
  std::vector<uint8_t> out(buf, buf + size);
  tjFree(buf);
  return out;
}

}  // namespace pb
