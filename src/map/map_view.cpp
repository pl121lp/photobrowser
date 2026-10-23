#include "map/map_view.h"

#include <algorithm>
#include <cmath>

#include "map/mercator.h"

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
    starting_ = true;
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

namespace {
constexpr double kClickSlopPx = 4;
constexpr double kDotHitPx = 10;  // grid dot radius incl. ring (6) plus slack, at display scale 1
const std::vector<uint32_t> kNoPhotos;
}  // namespace

bool MapView::pointerDown(Vec2d px, double t) {
  if (!shown() || !panelRect(window_).contains(px)) return false;
  captured_ = true;
  pointer_.reset();
  pressAt_ = px;
  const auto dot = dotAt(px);
  pressPhotos_ = dot ? dotPhotos_[*dot] : std::vector<uint32_t>{};
  cam_.onPointerDown(toPanel(px), t);
  return true;
}

bool MapView::pointerMove(Vec2d px, double t) {
  if (!captured_) {  // hover: remembered, but the content gets the event too
    pointer_ = shown() && panelRect(window_).contains(px) ? std::optional(px) : std::nullopt;
    return false;
  }
  cam_.onPointerMove(toPanel(px), t);
  return true;
}

bool MapView::pointerUp(Vec2d px, double t) {
  if (!captured_) return false;
  captured_ = false;
  if (!pressPhotos_.empty() && length(px - pressAt_) < kClickSlopPx) clicked_ = std::move(pressPhotos_);
  pressPhotos_.clear();
  pointer_ = panelRect(window_).contains(px) ? std::optional(px) : std::nullopt;
  cam_.onPointerUp(toPanel(px), t);
  return true;
}

void MapView::pointerLeave() { pointer_.reset(); }

const std::vector<uint32_t>& MapView::dotPhotos(size_t dot) const {
  return dot < dotPhotos_.size() ? dotPhotos_[dot] : kNoPhotos;
}

const std::vector<uint32_t>& MapView::hoveredPhotos() const {
  return hoveredDot_ ? dotPhotos(*hoveredDot_) : kNoPhotos;
}

Vec2d MapView::dotScreen(size_t dot) const {
  Vec2d d = dots_[dot];
  d.x -= std::round(d.x - cam_.center.x);  // the copy of the world nearest the view
  const Rect p = panelRect(window_);
  return cam_.worldToScreen(d) + Vec2d{p.x0, p.y0};
}

std::optional<size_t> MapView::dotAt(Vec2d px) const {
  if (single_ || !shown()) return std::nullopt;
  std::optional<size_t> best;
  double bestD = kDotHitPx * uiScale_;
  for (size_t i = 0; i < dots_.size(); ++i) {
    const double d = length(dotScreen(i) - px);
    if (d <= bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

bool MapView::wheel(Vec2d px, double notches) {
  if (!shown() || !panelRect(window_).contains(px)) return false;
  cam_.onWheel(toPanel(px), notches);
  return true;
}

std::optional<MapCamera::View> MapView::fit() const {
  return MapCamera::fitView(dots_, cam_.viewport, cam_.uiScale, kPadding, kGridMaxZoom, settings_.photoZoom,
                            cam_.minZoom);
}

void MapView::updateSingle(double t, const Subject& s) {
  const std::optional<Vec2d> at = s.photoGps ? toWorld(*s.photoGps) : std::nullopt;
  const bool changed = !single_ || photo_ != s.photo || starting_;
  single_ = true;
  photo_ = s.photo;
  dots_.clear();
  dotPhotos_.clear();
  if (at) dots_.push_back(*at);
  if (!changed || !at) return;
  if (starting_) cam_.jumpTo(*at, kOpenZoom);
  cam_.flyTo(*at, settings_.photoZoom, t);
  pulsePending_ = true;
  pulseStart_ = -1e9;
}

void MapView::updateGrid(double t, const Subject& s) {
  if (single_) {  // back from a photo: fit the grid's photos once it rests
    fitted_.clear();
    fitPaused_ = false;
  }
  single_ = false;
  photo_.reset();
  pulsePending_ = false;
  // One dot per position, remembering the photos taken there.
  std::vector<std::pair<Vec2d, uint32_t>> located;
  for (size_t i = 0; i < s.visibleGps.size(); ++i) {
    if (auto w = toWorld(s.visibleGps[i])) located.emplace_back(*w, i < s.visiblePos.size() ? s.visiblePos[i] : 0);
  }
  std::sort(located.begin(), located.end(), [](const auto& a, const auto& b) {
    return a.first.x != b.first.x ? a.first.x < b.first.x : a.first.y != b.first.y ? a.first.y < b.first.y
                                                                                   : a.second < b.second;
  });
  dots_.clear();
  dotPhotos_.clear();
  for (const auto& [w, pos] : located) {
    if (dots_.empty() || dots_.back() != w) {
      dots_.push_back(w);
      dotPhotos_.emplace_back();
    }
    if (!s.visiblePos.empty()) dotPhotos_.back().push_back(pos);
  }

  if (starting_) {
    const auto v = fit();
    cam_.jumpTo(v ? v->center : Vec2d{0.5, 0.5}, v ? v->zoom : cam_.minZoom);
    fitted_ = dots_;
    fitPaused_ = false;
    return;
  }
  if (cam_.takeUserMoved()) {
    fitPaused_ = true;
    pausedDots_ = dots_;
  }
  if (fitPaused_ && dots_ != pausedDots_) fitPaused_ = false;
  if (s.gridMoving) lastGridMove_ = t;
  refitDue_ = !fitPaused_ && !dots_.empty() && dots_ != fitted_;
  if (refitDue_ && !s.gridMoving && t - lastGridMove_ >= kRestS) {
    if (const auto v = fit()) cam_.flyTo(v->center, v->zoom, t);
    fitted_ = dots_;
    refitDue_ = false;
  }
}

void MapView::update(double t, double dt, Vec2d window, double uiScale, const Subject& s, size_t tilesPending) {
  window_ = window;
  slide_ = std::clamp(slideTween_.value(t), 0.0, 1.0);
  cam_.viewport = {panelWidth(window), window.y};
  cam_.uiScale = uiScale;
  uiScale_ = uiScale;
  if (open_ && s.frozen && !starting_) {
    refitDue_ = false;  // dots and view stay as they are
  } else if (open_) {
    if (s.photo) {
      updateSingle(t, s);
    } else {
      updateGrid(t, s);
    }
    starting_ = false;
    noDots_ = dots_.empty();
  }
  cam_.update(t, dt);
  if (single_) cam_.takeUserMoved();  // manual moves are not sticky with a single photo
  if (pulsePending_ && cam_.idle()) {
    pulsePending_ = false;
    pulseStart_ = t;
  }

  // Linking the grid and the map (grid only): the dot under the pointer, else the dot of
  // the photo under the pointer.
  hoveredDot_ = pointer_ && !captured_ && open_ ? dotAt(*pointer_) : std::nullopt;
  highlightedDot_ = hoveredDot_;
  if (!highlightedDot_ && s.hoveredPhoto && !single_) {
    for (size_t i = 0; i < dotPhotos_.size() && !highlightedDot_; ++i) {
      if (std::find(dotPhotos_[i].begin(), dotPhotos_[i].end(), *s.hoveredPhoto) != dotPhotos_[i].end()) {
        highlightedDot_ = i;
      }
    }
  }

  // No internet: offline results and nothing good, once everything asked has failed or
  // after two failures (a dead network can take seconds per tile to time out).
  if (open_ && probing_ && probeOffline_ > 0 && (tilesPending == 0 || probeOffline_ >= 2)) {
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

std::optional<double> MapView::pulse(double t) const {
  const double e = t - pulseStart_;
  if (e < 0 || e >= kPulseS || !single_ || dots_.empty()) return std::nullopt;
  return e - std::floor(e);
}

std::string_view MapView::status() const {
  if (!noDots_) return {};
  return single_ ? "No location for this item" : "No locations among the visible photos";
}

bool MapView::animating(double t) const {
  return !slideTween_.done(t) || !cam_.idle() || pulsePending_ || pulse(t) || (open_ && refitDue_);
}

std::optional<MapNotice> MapView::takeNotice() {
  if (notices_.empty()) return std::nullopt;
  const MapNotice n = notices_.front();
  notices_.pop_front();
  return n;
}

}  // namespace pb
