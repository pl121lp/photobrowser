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

void MediaFormats::add(std::string ext, const MediaHandler* handler, std::string origin) {
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (ext.empty() || ext == ".jpg" || ext == ".jpeg" || !handler) return;
  if (std::any_of(handlers_.begin(), handlers_.end(), [&](const Entry& h) { return h.ext == ext; })) return;
  handlers_.push_back({std::move(ext), handler, std::move(origin)});
}

const MediaHandler* MediaFormats::find(std::string_view path) const {
  if (handlers_.empty()) return nullptr;
  const std::string e = lowerExtension(path);
  for (const Entry& h : handlers_) {
    if (h.ext == e) return h.handler;
  }
  return nullptr;
}

std::string_view MediaFormats::origin(std::string_view path) const {
  if (handlers_.empty()) return {};
  const std::string e = lowerExtension(path);
  for (const Entry& h : handlers_) {
    if (h.ext == e) return h.origin;
  }
  return {};
}

bool MediaFormats::accepts(std::string_view path) const {
  const std::string e = lowerExtension(path);
  return e == ".jpg" || e == ".jpeg" || find(path);
}

std::string MediaFormats::signature() const {
  std::vector<std::string> exts, origins;
  for (const Entry& h : handlers_) {
    exts.push_back(h.ext);
    if (!h.origin.empty()) origins.push_back(h.origin);
  }
  std::sort(exts.begin(), exts.end());
  std::sort(origins.begin(), origins.end());
  origins.erase(std::unique(origins.begin(), origins.end()), origins.end());
  std::string s;
  for (const auto& e : exts) s += (s.empty() ? "" : " ") + e;
  for (size_t i = 0; i < origins.size(); ++i) s += (i ? ", " : " (") + origins[i];
  if (!origins.empty()) s += ")";
  return s;
}

}  // namespace pb
