#include "library/png_handler.h"

#include <utility>

#include "imaging/png.h"
#include "imaging/resize.h"
#include "library/png_header.h"
#include "platform/paths.h"

namespace pb {

std::optional<MediaProbe> PngHandler::probe(const std::filesystem::path& abs) const {
  const auto head = readFileRange(abs, 0, 64 * 1024);
  if (!head) return std::nullopt;
  const auto info = parsePngHeader(*head);
  if (!info || info->width == 0 || info->height == 0) return std::nullopt;
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
  auto img = decodePng(*data);
  if (!img) return std::nullopt;
  const auto info = parsePngHeader(*data);
  if (info && info->exif && info->exif->orientation != 1) return applyOrientation(*img, info->exif->orientation);
  return img;
}

}  // namespace pb
