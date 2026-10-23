#include "library/media_format.h"

#include <algorithm>
#include <cctype>

namespace pb {

std::string lowerExtension(std::string_view path) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
  std::string e(path.substr(dot));
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return e;
}

const MediaFormats& MediaFormats::none() {
  static const MediaFormats empty;
  return empty;
}

void MediaFormats::add(std::string ext, const MediaHandler* handler) {
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (ext.empty() || ext == ".jpg" || ext == ".jpeg" || !handler) return;
  if (std::any_of(handlers_.begin(), handlers_.end(), [&](const auto& h) { return h.first == ext; })) return;
  handlers_.emplace_back(std::move(ext), handler);
}

const MediaHandler* MediaFormats::find(std::string_view path) const {
  if (handlers_.empty()) return nullptr;
  const std::string e = lowerExtension(path);
  for (const auto& [ext, h] : handlers_) {
    if (ext == e) return h;
  }
  return nullptr;
}

bool MediaFormats::accepts(std::string_view path) const {
  const std::string e = lowerExtension(path);
  return e == ".jpg" || e == ".jpeg" || find(path);
}

std::string MediaFormats::signature() const {
  std::vector<std::string> exts;
  for (const auto& h : handlers_) exts.push_back(h.first);
  std::sort(exts.begin(), exts.end());
  std::string s;
  for (const auto& e : exts) s += (s.empty() ? "" : " ") + e;
  return s;
}

}  // namespace pb
