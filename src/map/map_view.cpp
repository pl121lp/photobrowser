#include "map/map_view.h"

#include <algorithm>
#include <cmath>

namespace pb {

std::string_view mapNoticeText(MapNotice n) {
  switch (n) {
    case MapNotice::NeedsPlugin: return "Map needs a map tile plugin (start with --plugin; see README)";
    case MapNotice::NoInternet: return "Map unavailable: no internet connection";
    case MapNotice::Refused: return "Map tile server refused requests";
  }
  return {};
}

MapView::MapView(MapSettings s) : settings_(s) {}

void MapView::setOpen(bool open, double t) {
  open_ = open;
  slideTween_ = {slide_, open ? 1.0 : 0.0, t, kSlideS};
  captured_ = false;
  if (open) {
    probing_ = true;
    probeOffline_ = 0;
  }
}

void MapView::toggle(double t, const TileSource* source) {
  if (open_) {
    setOpen(false, t);
    return;
  }
  if (!source) {
    notices_.push_back(MapNotice::NeedsPlugin);
    return;
  }
  cam_.minZoom = 2;
  cam_.maxZoom = std::max(2, source->maxZoom());
  setOpen(true, t);
}

bool MapView::escape(double t) {
  if (!open_) return false;
  setOpen(false, t);
  return true;
}

double MapView::panelWidth(Vec2d window) const { return std::round(window.x * settings_.width); }

Rect MapView::contentRect(Vec2d window) const { return {0, 0, window.x - panelWidth(window) * slide_, window.y}; }

Rect MapView::panelRect(Vec2d window) const {
  const double x0 = window.x - panelWidth(window) * slide_;
  return {x0, 0, x0 + panelWidth(window), window.y};
}

Vec2d MapView::toPanel(Vec2d px) const {
  const Rect p = panelRect(window_);
  return {px.x - p.x0, px.y - p.y0};
}

bool MapView::pointerDown(Vec2d px, double t) {
  if (!shown() || !panelRect(window_).contains(px)) return false;
  captured_ = true;
  cam_.onPointerDown(toPanel(px), t);
  return true;
}

bool MapView::pointerMove(Vec2d px, double t) {
  if (!captured_) return false;
  cam_.onPointerMove(toPanel(px), t);
  return true;
}

bool MapView::pointerUp(Vec2d px, double t) {
  if (!captured_) return false;
  captured_ = false;
  cam_.onPointerUp(toPanel(px), t);
  return true;
}

bool MapView::wheel(Vec2d px, double notches) {
  if (!shown() || !panelRect(window_).contains(px)) return false;
  cam_.onWheel(toPanel(px), notches);
  return true;
}

void MapView::update(double t, double dt, Vec2d window, double uiScale, const Subject& /*s*/, size_t tilesPending) {
  window_ = window;
  slide_ = std::clamp(slideTween_.value(t), 0.0, 1.0);
  cam_.viewport = {panelWidth(window), window.y};
  cam_.uiScale = uiScale;
  cam_.update(t, dt);

  if (open_ && probing_ && probeOffline_ > 0 && tilesPending == 0) {
    probing_ = false;
    notices_.push_back(MapNotice::NoInternet);
    setOpen(false, t);
  }
}

void MapView::tileResults(std::span<const TileStatus> results) {
  for (const TileStatus s : results) {
    switch (s) {
      case TileStatus::Ok: probing_ = false; break;
      case TileStatus::Offline:
        if (probing_) {
          ++probeOffline_;
        } else if (!lostNoticed_) {
          lostNoticed_ = true;
          notices_.push_back(MapNotice::NoInternet);
        }
        break;
      case TileStatus::Refused:
        if (!refusedNoticed_) {
          refusedNoticed_ = true;
          notices_.push_back(MapNotice::Refused);
        }
        break;
      case TileStatus::Missing:
      case TileStatus::Error: break;
    }
  }
}

std::optional<MapNotice> MapView::takeNotice() {
  if (notices_.empty()) return std::nullopt;
  const MapNotice n = notices_.front();
  notices_.pop_front();
  return n;
}

}  // namespace pb
