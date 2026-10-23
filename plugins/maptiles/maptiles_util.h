// Helpers of the maptiles plugin that need neither the network nor the host
// (unit-tested directly, see tests/test_maptiles.cpp).
#pragma once

#include <curl/curl.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "plugin/photobrowse_plugin.h"

namespace pbmaps {

// {z} {x} {y} replaced by the tile, {key} by the API key.
std::string expandUrl(std::string_view tmpl, int z, int x, int y, std::string_view key);
// File extension of the template's path ("png", "jpg", ...), "img" if it has none.
std::string fileExtension(std::string_view tmpl);
// <cacheDir>/maptiles/<16 hex digits of a hash of tmpl>/<z>/<x>/<y>.<ext>
std::filesystem::path tilePath(const std::filesystem::path& cacheDir, std::string_view tmpl, int z, int x, int y);
// max-age from a Cache-Control header in raw response headers (case-insensitive).
std::optional<int64_t> maxAgeSeconds(std::string_view headers);
// Expiry time: now + maxAge, or now + 7 days when the server gave none.
int64_t expiryFor(std::optional<int64_t> maxAge, int64_t now);
// The outcome of a transfer as a PbTileResult.
PbTileResult mapTransfer(CURLcode code, long httpStatus);
// Deletes the least recently used tiles (oldest modification time) under `dir`, each
// with its ".exp" sidecar, until the tiles there take at most maxBytes.
void trimCache(const std::filesystem::path& dir, uint64_t maxBytes);

}  // namespace pbmaps
