#include "library/indexer.h"

#include <algorithm>
#include <cmath>

#include "imaging/jpeg.h"
#include "imaging/resize.h"
#include "library/exif.h"
#include "platform/paths.h"

namespace pb {

namespace fs = std::filesystem;

namespace {

constexpr uint64_t kHeadBytes = 64 * 1024;
constexpr uint64_t kMaxHeadBytes = 512 * 1024;
constexpr double kPreviewAspectTolerance = 0.02;

void setDims(PhotoRecord& rec, uint32_t w, uint32_t h) {
  const bool swap = rec.orientation >= 5 && rec.orientation <= 8;
  rec.width = swap ? h : w;
  rec.height = swap ? w : h;
}

// Upright L1 and L0 from an upright source image.
void makeSmallLevels(const Image& upright, Image& l0, Image& l1) {
  l1 = fitToLongEdge(upright, kSlotSize[1]);
  l0 = fitToLongEdge(l1, kSlotSize[0]);
}

}  // namespace

QuickResult quickIndex(const fs::path& root, const ScanEntry& e) {
  QuickResult q;
  PhotoRecord& rec = q.rec;
  const fs::path abs = root / fromUtf8(e.relPath);
  rec.relPath = e.relPath;
  rec.fileSize = e.size;
  rec.mtime = e.mtime;
  rec.takenAt = e.mtime;
  rec.key = cacheKey(toUtf8(abs), e.size, e.mtime);

  auto head = readFileRange(abs, 0, kHeadBytes);
  if (!head) {
    rec.flags |= kBroken;
    return q;
  }
  auto info = parseJpegHeader(*head);
  if (!info) {
    rec.flags |= kBroken;
    return q;
  }
  const bool previewBeyond = info->thumbLength > 0 && info->thumbOffset + info->thumbLength > head->size();
  if ((!info->sawSof || previewBeyond) && head->size() == kHeadBytes) {
    if (auto more = readFileRange(abs, 0, kMaxHeadBytes)) {
      head = std::move(more);
      info = parseJpegHeader(*head);
    }
  }

  rec.orientation = uint8_t(info->orientation);
  if (info->dateTimeOriginal) {
    if (auto t = parseExifDateTime(*info->dateTimeOriginal)) rec.takenAt = *t;
  }
  if (info->sawSof) setDims(rec, info->width, info->height);
  rec.thumbOffset = info->thumbOffset;
  rec.thumbLength = info->thumbLength;

  std::optional<Image> preview;
  if (info->thumbLength > 0 && info->thumbOffset + info->thumbLength <= head->size()) {
    preview = decodeJpeg(std::span(*head).subspan(info->thumbOffset, info->thumbLength));
  }
  if (preview) {
    Image upright = applyOrientation(*preview, rec.orientation);
    // Some cameras pad previews (e.g. 160x120 with bars); only trust matching shapes.
    const double pa = double(upright.w) / upright.h;
    if (!rec.width || std::abs(pa / rec.aspect() - 1) <= kPreviewAspectTolerance) {
      makeSmallLevels(upright, q.l0, q.l1);
      return q;
    }
  }
  rec.flags |= kNeedsDecodeL1;
  return q;
}

bool decodeL0L1FromOriginal(const fs::path& abs, PhotoRecord& rec, Image& l0, Image& l1) {
  auto bytes = readWholeFile(abs);
  if (!bytes) return false;
  auto img = decodeJpeg(*bytes, kSlotSize[1]);
  if (!img) return false;
  if (!rec.width || !rec.height) {
    if (auto info = parseJpegHeader(*bytes); info && info->sawSof) setDims(rec, info->width, info->height);
  }
  makeSmallLevels(applyOrientation(*img, rec.orientation), l0, l1);
  rec.flags &= uint8_t(~kNeedsDecodeL1);
  return true;
}

std::array<Image, kLevelCount> buildLazyLevels(const fs::path& abs, const PhotoRecord& rec, int highestLevel) {
  std::array<Image, kLevelCount> out;
  highestLevel = std::clamp(highestLevel, 2, kLevelCount - 1);
  auto bytes = readWholeFile(abs);
  if (!bytes) return out;
  auto img = decodeJpeg(*bytes, kSlotSize[highestLevel]);
  if (!img) return out;
  out[highestLevel] = fitToLongEdge(applyOrientation(*img, rec.orientation), kSlotSize[highestLevel]);
  for (int l = highestLevel - 1; l >= 2; --l) out[l] = fitToLongEdge(out[l + 1], kSlotSize[l]);
  return out;
}

std::optional<Image> decodeFull(const fs::path& abs, const PhotoRecord& rec, uint32_t minLongEdge) {
  auto bytes = readWholeFile(abs);
  if (!bytes) return std::nullopt;
  auto img = decodeJpeg(*bytes, minLongEdge);
  if (!img) return std::nullopt;
  return applyOrientation(*img, rec.orientation);
}

}  // namespace pb
