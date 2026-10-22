#include "ui/icons.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace roadmap::ui {

namespace {
const std::map<std::string, const char*>& paths() {
  static const std::map<std::string, const char*> m = {
      {"plus", "M12 5v14M5 12h14"},
      {"minus", "M5 12h14"},
      {"undo", "M9 14 4 9l5-5M4 9h10a6 6 0 0 1 0 12h-3"},
      {"redo", "M15 14l5-5-5-5M20 9H10a6 6 0 0 0 0 12h3"},
      {"search", "M11 19a8 8 0 1 1 0-16 8 8 0 0 1 0 16zM21 21l-4.3-4.3"},
      {"down", "M6 9l6 6 6-6"},
      {"right", "M9 6l6 6-6 6"},
      {"up", "M18 15l-6-6-6 6"},
      {"more", "M5 12h.01M12 12h.01M19 12h.01"},
      {"x", "M18 6 6 18M6 6l12 12"},
      {"check", "M20 6 9 17l-5-5"},
      {"gantt", "M3 6h9M8 12h12M14 18h7"},
      {"board", "M4 4h5v16H4zM15 4h5v10h-5z"},
      {"list", "M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"},
      {"sliders", "M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1M15 4v4M7 10v4M17 16v4"},
      {"user", "M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2M12 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z"},
      {"message", "M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"},
      {"trash", "M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6M10 11v6M14 11v6"},
      {"indent", "M3 6h18M11 12h10M3 18h18M3 9l3 3-3 3"},
      {"outdent", "M3 6h18M11 12h10M3 18h18M6 9l-3 3 3 3"},
      {"arrowUp", "M12 19V5M5 12l7-7 7 7"},
      {"arrowDown", "M12 5v14M19 12l-7 7-7-7"},
      {"folder", "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"},
      {"download", "M12 3v12M7 10l5 5 5-5M5 21h14"},
      {"upload", "M12 21V9M7 14l5-5 5 5M5 3h14"},
      {"link", "M10 13a5 5 0 0 0 7 0l3-3a5 5 0 0 0-7-7l-1 1M14 11a5 5 0 0 0-7 0l-3 3a5 5 0 0 0 7 7l1-1"},
      {"calendar", "M3 5h18v16H3zM3 10h18M8 3v4M16 3v4"},
      {"copy", "M9 9h11v11H9zM5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1"},
      {"help", "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM9.1 9a3 3 0 0 1 5.8 1c0 2-3 3-3 3M12 17h.01"},
      {"fit", "M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7"},
      {"target", "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8z"},
      {"filter", "M3 5h18l-7 8v6l-4 2v-8z"},
      {"grip", "M9 5h.01M9 12h.01M9 19h.01M15 5h.01M15 12h.01M15 19h.01"},
      {"cmd", "M9 6a3 3 0 1 0-3 3h12a3 3 0 1 0-3-3v12a3 3 0 1 0 3-3H6a3 3 0 1 0 3 3z"},
      {"warn", "M12 3 2 21h20zM12 10v5M12 18h.01"},
      {"pencil", "M4 20h4L19 9l-4-4L4 16zM13 7l4 4"},
      {"flag", "M5 21V4M5 4h12l-2 4 2 4H5"},
      {"chart", "M4 20V10M10 20V4M16 20v-8M22 20H2"},
      {"paperclip", "m21.44 11.05-9.19 9.19a6 6 0 0 1-8.49-8.49l8.57-8.57A4 4 0 1 1 18 8.84l-8.59 8.57a2 2 0 0 1-2.83-2.83l8.49-8.48"},
      {"globe", "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM2 12h20M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"},
      {"extlink", "M15 3h6v6M10 14 21 3M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"},
      {"file", "M14.5 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7.5zM14 2v6h6"},
      {"gitpr", "M21 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0zM9 6a3 3 0 1 1-6 0 3 3 0 0 1 6 0zM13 6h3a2 2 0 0 1 2 2v7M6 9v12"},
      {"gitissue", "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 12h.01"},
      {"image", "M3 3h18v18H3zM8.5 10a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3zM21 15l-5-5L5 21"},
      {"lock", "M4 11h16v10H4zM8 11V7a4 4 0 0 1 8 0v4"},
  };
  return m;
}

struct Poly {
  std::vector<ImVec2> pts;
  bool closed = false;
};

// SVG の楕円弧（端点の表現）を折れ線にする
void arcTo(std::vector<ImVec2>& out, ImVec2 p0, float rx, float ry, float phiDeg, bool large, bool sweep, ImVec2 p1) {
  if (rx == 0 || ry == 0) {
    out.push_back(p1);
    return;
  }
  const float phi = phiDeg * 3.14159265f / 180.f;
  const float cs = std::cos(phi), sn = std::sin(phi);
  const float dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2;
  const float x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
  rx = std::fabs(rx);
  ry = std::fabs(ry);
  const float lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
  if (lam > 1) {
    rx *= std::sqrt(lam);
    ry *= std::sqrt(lam);
  }
  float num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  if (num < 0) num = 0;
  float co = std::sqrt(num / (rx * rx * y1 * y1 + ry * ry * x1 * x1));
  if (large == sweep) co = -co;
  const float cx1 = co * rx * y1 / ry, cy1 = -co * ry * x1 / rx;
  const float cx = cs * cx1 - sn * cy1 + (p0.x + p1.x) / 2, cy = sn * cx1 + cs * cy1 + (p0.y + p1.y) / 2;
  const auto ang = [](float ux, float uy, float vx, float vy) {
    const float d = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    float a = std::acos(std::fmax(-1.f, std::fmin(1.f, (ux * vx + uy * vy) / d)));
    if (ux * vy - uy * vx < 0) a = -a;
    return a;
  };
  const float t1 = ang(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
  float dt = ang((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
  if (!sweep && dt > 0) dt -= 2 * 3.14159265f;
  if (sweep && dt < 0) dt += 2 * 3.14159265f;
  const int n = std::max(4, static_cast<int>(std::fabs(dt) / (3.14159265f / 12)));
  for (int i = 1; i <= n; ++i) {
    const float t = t1 + dt * static_cast<float>(i) / static_cast<float>(n);
    out.push_back(ImVec2(cx + rx * std::cos(t) * cs - ry * std::sin(t) * sn, cy + rx * std::cos(t) * sn + ry * std::sin(t) * cs));
  }
}

std::vector<Poly> parse(const char* d) {
  std::vector<Poly> polys;
  ImVec2 cur(0, 0), start(0, 0);
  char cmd = 0;
  const char* p = d;
  const auto skip = [&]() {
    while (*p == ' ' || *p == ',') ++p;
  };
  const auto num = [&]() {
    skip();
    char* end = nullptr;
    const float v = std::strtof(p, &end);
    p = end;
    return v;
  };
  const auto flag = [&]() {
    skip();
    const bool v = *p == '1';
    ++p;
    return v;
  };
  const auto hasNum = [&]() {
    skip();
    return *p == '-' || *p == '.' || std::isdigit(static_cast<unsigned char>(*p));
  };
  while (*p) {
    skip();
    if (!*p) break;
    if (std::isalpha(static_cast<unsigned char>(*p))) cmd = *p++;
    const bool rel = std::islower(static_cast<unsigned char>(cmd));
    switch (std::tolower(static_cast<unsigned char>(cmd))) {
      case 'm': {
        float x = num(), y = num();
        cur = rel ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
        start = cur;
        polys.push_back({{cur}, false});
        cmd = rel ? 'l' : 'L';  // 続く座標は直線
        break;
      }
      case 'l': {
        float x = num(), y = num();
        cur = rel ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
        polys.back().pts.push_back(cur);
        break;
      }
      case 'h': {
        float x = num();
        cur.x = rel ? cur.x + x : x;
        polys.back().pts.push_back(cur);
        break;
      }
      case 'v': {
        float y = num();
        cur.y = rel ? cur.y + y : y;
        polys.back().pts.push_back(cur);
        break;
      }
      case 'a': {
        float rx = num(), ry = num(), rot = num();
        bool la = flag(), sw = flag();
        float x = num(), y = num();
        ImVec2 to = rel ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
        arcTo(polys.back().pts, cur, rx, ry, rot, la, sw, to);
        cur = to;
        break;
      }
      case 'z':
        if (!polys.empty()) polys.back().closed = true;
        cur = start;
        break;
      default:
        ++p;
        break;
    }
    if (std::tolower(static_cast<unsigned char>(cmd)) == 'z' && hasNum()) cmd = 'L';
  }
  return polys;
}

const std::vector<Poly>& cached(const char* name) {
  static std::map<std::string, std::vector<Poly>> cache;
  auto it = cache.find(name);
  if (it != cache.end()) return it->second;
  const auto& m = paths();
  auto pit = m.find(name);
  return cache.emplace(name, pit == m.end() ? std::vector<Poly>{} : parse(pit->second)).first->second;
}
}  // namespace

bool hasIcon(const char* name) { return paths().count(name) > 0; }

void drawIcon(ImDrawList* dl, const char* name, ImVec2 tl, float size, ImU32 col, float stroke) {
  const float k = size / 24.f;
  const float th = std::fmax(1.0f, stroke * k);
  for (const auto& poly : cached(name)) {
    std::vector<ImVec2> pts;
    pts.reserve(poly.pts.size());
    for (const auto& q : poly.pts) pts.push_back(ImVec2(tl.x + q.x * k, tl.y + q.y * k));
    if (pts.size() == 2 && std::fabs(pts[0].x - pts[1].x) < 0.5f && std::fabs(pts[0].y - pts[1].y) < 0.5f) {
      dl->AddCircleFilled(pts[0], th * 0.7f, col);  // 点（"h.01"）
      continue;
    }
    if (pts.size() >= 2)
      dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, poly.closed ? ImDrawFlags_Closed : ImDrawFlags_None, th);
  }
}

}  // namespace roadmap::ui
