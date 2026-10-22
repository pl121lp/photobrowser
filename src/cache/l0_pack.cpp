#include "cache/l0_pack.h"

#include <cstring>

#include "core/levels.h"
#include "platform/paths.h"

namespace pb {

namespace {
constexpr char kMagic[8] = {'P', 'B', 'L', '0', 0, 0, 0, 1};
constexpr uint32_t kEnd = 0x21444E45;  // "END!"
constexpr uint32_t kSlot = kSlotSize[0];
constexpr size_t kBlock = size_t(kSlot) * kSlot * 4;
constexpr size_t kEntryBytes = 8 + 2 + 2 + kBlock;

template <class T>
void put(std::vector<uint8_t>& o, T v) {
  for (size_t i = 0; i < sizeof(T); ++i) o.push_back(uint8_t(uint64_t(v) >> (8 * i)));
}
template <class T>
T get(const uint8_t* p) {
  uint64_t v = 0;
  for (size_t i = 0; i < sizeof(T); ++i) v |= uint64_t(p[i]) << (8 * i);
  return T(v);
}
}  // namespace

bool writeL0Pack(const std::filesystem::path& path, const L0Entries& entries) {
  std::vector<uint8_t> out(kMagic, kMagic + sizeof kMagic);
  out.reserve(out.size() + 8 + entries.size() * kEntryBytes);
  put(out, uint32_t(entries.size()));
  for (const auto& [key, img] : entries) {
    if (img.w > kSlot || img.h > kSlot) return false;
    put(out, key);
    put(out, uint16_t(img.w));
    put(out, uint16_t(img.h));
    const size_t start = out.size();
    out.resize(start + kBlock, 0);
    for (uint32_t y = 0; y < img.h; ++y) {
      std::memcpy(&out[start + size_t(y) * kSlot * 4], &img.rgba[size_t(y) * img.w * 4], size_t(img.w) * 4);
    }
  }
  put(out, kEnd);
  return atomicWriteFile(path, out);
}

std::optional<L0Entries> readL0Pack(const std::filesystem::path& path) {
  auto data = readWholeFile(path);
  if (!data || data->size() < sizeof kMagic + 8 || std::memcmp(data->data(), kMagic, sizeof kMagic) != 0) {
    return std::nullopt;
  }
  const uint8_t* p = data->data() + sizeof kMagic;
  const uint32_t n = get<uint32_t>(p);
  p += 4;
  if (data->size() != sizeof kMagic + 4 + size_t(n) * kEntryBytes + 4) return std::nullopt;
  L0Entries out;
  out.reserve(n);
  for (uint32_t i = 0; i < n; ++i, p += kEntryBytes) {
    Image img;
    img.w = get<uint16_t>(p + 8);
    img.h = get<uint16_t>(p + 10);
    if (img.w > kSlot || img.h > kSlot) return std::nullopt;
    img.rgba.resize(size_t(img.w) * img.h * 4);
    for (uint32_t y = 0; y < img.h; ++y) {
      std::memcpy(&img.rgba[size_t(y) * img.w * 4], p + 12 + size_t(y) * kSlot * 4, size_t(img.w) * 4);
    }
    out.emplace_back(get<uint64_t>(p), std::move(img));
  }
  if (get<uint32_t>(p) != kEnd) return std::nullopt;
  return out;
}

}  // namespace pb
