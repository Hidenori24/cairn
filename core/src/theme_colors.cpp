#include "roadmap/theme_colors.hpp"

namespace roadmap {

const ThemeColors& lightColors() {
  static const ThemeColors t = [] {
    ThemeColors x;
    x.bg = 0xf5f6f8;
    x.surface = 0xffffff;
    x.surface2 = 0xf4f5f7;
    x.surface3 = 0xe9ecf0;
    x.ink = 0x1a2230;
    x.muted = 0x667085;
    x.line = 0xe4e7ec;
    x.line2 = 0xcfd4dc;
    x.accent = 0x2563eb;
    x.accentInk = 0xffffff;
    x.accentBg = 0xe8f0ff;
    x.late = 0xd92d20;
    x.lateBg = 0xfdecea;
    x.tight = 0xb54708;
    x.tightBg = 0xfff1dc;
    x.tightEdge = 0xe08a1e;
    x.ok = 0x12805c;
    x.okBg = 0xe3f5ec;
    const unsigned c[8] = {0x7d8798, 0xb5667f, 0x5b86d6, 0x3f8f88, 0x8c78c6, 0xa38a4a, 0x4793b3, 0xa56db0};
    for (int i = 0; i < 8; ++i) x.c[i] = c[i];
    x.hover = {0x1a2230, .04f};
    x.selbg = {0x2563eb, .10f};
    x.rowline = {0x1a2230, .06f};
    x.wkend = {0x1a2230, .035f};
    x.holbg = {0x1a2230, .06f};
    x.arrow = 0x98a2b3;
    x.dark = false;
    return x;
  }();
  return t;
}

const ThemeColors& darkColors() {
  static const ThemeColors t = [] {
    ThemeColors x;
    x.bg = 0x0e1116;
    x.surface = 0x161b22;
    x.surface2 = 0x1b222b;
    x.surface3 = 0x252d38;
    x.ink = 0xe6e9ee;
    x.muted = 0x8b95a5;
    x.line = 0x262e39;
    x.line2 = 0x37414e;
    x.accent = 0x6ea0ff;
    x.accentInk = 0x0b1220;
    x.accentBg = 0x1a2a48;
    x.late = 0xff7b6f;
    x.lateBg = 0x3a1d1b;
    x.tight = 0xf0b34a;
    x.tightBg = 0x382a10;
    x.tightEdge = 0xf0b34a;
    x.ok = 0x58c987;
    x.okBg = 0x16301f;
    const unsigned c[8] = {0x8f9aae, 0xd0809b, 0x6c98ea, 0x4fb0a7, 0xa891e0, 0xc5a85c, 0x5db0d2, 0xc68bd0};
    for (int i = 0; i < 8; ++i) x.c[i] = c[i];
    x.hover = {0xe6e9ee, .05f};
    x.selbg = {0x6ea0ff, .16f};
    x.rowline = {0xe6e9ee, .06f};
    x.wkend = {0xe6e9ee, .035f};
    x.holbg = {0xe6e9ee, .07f};
    x.arrow = 0x667085;
    x.dark = true;
    return x;
  }();
  return t;
}

}  // namespace roadmap
