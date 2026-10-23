#include "library/png_handler.h"

#include <algorithm>
#include <span>
#include <utility>

#include "imaging/png.h"
#include "imaging/resize.h"
#include "library/png_header.h"
#include "platform/paths.h"

namespace pb {

namespace {
constexpr size_t kHeaderBytes = 64 * 1024;  // the prefix both probe and decode parse
}

std::optional<MediaProbe> PngHandler::probe(const std::filesystem::path& abs) const {
  const auto head = readFileRange(abs, 0, kHeaderBytes);
  if (!head) return std::nullopt;
  const auto info = parsePngHeader(*head);
  if (!info || info->width == 0 || info->height == 0 ||
      uint64_t(info->width) * info->height > kMaxPngPixels)
    return std::nullopt;
  MediaProbe p;
  p.width = info->width;
  p.height = info->height;
  if (info->exif) {
    if (info->exif->orientation >= 5 && info->exif->orientation <= 8) std::swap(p.width, p.height);
    if (info->exif->dateTimeOriginal) {
      if (const auto t = parseExifDateTime(*info->exif->dateTimeOriginal)) p.takenAt = *t;
    }
    p.gps = info->exif->gps;
  }
  return p;
}

std::optional<Image> PngHandler::decodeStill(const std::filesystem::path& abs, uint32_t) const {
  const auto data = readWholeFile(abs);
  if (!data) return std::nullopt;
  // Same prefix as probe, so both agree on the orientation (and on the size limit).
  const auto info = parsePngHeader(std::span<const uint8_t>(*data).first(std::min(data->size(), kHeaderBytes)));
  if (info && uint64_t(info->width) * info->height > kMaxPngPixels) return std::nullopt;
  auto img = decodePng(*data);
  if (!img) return std::nullopt;
  if (info && info->exif && info->exif->orientation != 1) return applyOrientation(*img, info->exif->orientation);
  return img;
}

}  // namespace pb
