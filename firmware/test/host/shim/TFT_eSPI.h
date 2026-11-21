#pragma once
// Ev önizlemesi için TFT_eSPI benzeri: RAM'de 16 bitlik çerçeve, aynı adlı işlevler.
// Yumuşak yazı tipleri (.vlw) TFT_eSPI'deki gibi çözülür ve konumlanır.
#include "Arduino.h"

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define CL_DATUM 3
#define MC_DATUM 4
#define CC_DATUM 4
#define MR_DATUM 5
#define CR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8
#define L_BASELINE 9
#define C_BASELINE 10
#define R_BASELINE 11

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800

class TFT_eSPI {
 public:
  TFT_eSPI(int w = 240, int h = 320) : _iw(w), _ih(h) {}
  virtual ~TFT_eSPI() {}
  void init() { _alloc(_iw, _ih); }
  void setRotation(uint8_t r) {
    int w = r & 1 ? std::max(_iw, _ih) : std::min(_iw, _ih), h = r & 1 ? std::min(_iw, _ih) : std::max(_iw, _ih);
    _alloc(w, h);
  }
  void invertDisplay(bool) {}
  int16_t width() const { return _w; }
  int16_t height() const { return _h; }
  void startWrite() {}
  void endWrite() {}
  void setSwapBytes(bool s) { _swap = s; }
  bool getSwapBytes() { return _swap; }

  uint16_t* buf() { return _buf.data(); }

  virtual void drawPixel(int32_t x, int32_t y, uint32_t c) {
    if (x < 0 || y < 0 || x >= _w || y >= _h) return;
    _buf[y * _w + x] = (uint16_t)c;
  }
  virtual uint16_t readPixel(int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= _w || y >= _h) return 0;
    return _buf[y * _w + x];
  }
  void readRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        uint16_t c = readPixel(x + i, y + j);
        *data++ = (uint16_t)(c << 8 | c >> 8);  // TFT_eSPI gibi bayt sırası ters
      }
  }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) drawPixel(x + i, y + j, c);
  }
  void fillScreen(uint32_t c) { fillRect(0, 0, _w, _h, c); }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    drawFastHLine(x, y, w, c);
    drawFastHLine(x, y + h - 1, w, c);
    drawFastVLine(x, y, h, c);
    drawFastVLine(x + w - 1, y, h, c);
  }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
      drawPixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void fillCircle(int32_t cx, int32_t cy, int32_t r, uint32_t c) {
    for (int y = -r; y <= r; y++)
      for (int x = -r; x <= r; x++)
        if (x * x + y * y <= r * r + r) drawPixel(cx + x, cy + y, c);
  }
  void drawCircle(int32_t cx, int32_t cy, int32_t r, uint32_t c) {
    int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
    drawPixel(cx, cy + r, c); drawPixel(cx, cy - r, c); drawPixel(cx + r, cy, c); drawPixel(cx - r, cy, c);
    while (x < y) {
      if (f >= 0) { y--; ddy += 2; f += ddy; }
      x++; ddx += 2; f += ddx;
      drawPixel(cx + x, cy + y, c); drawPixel(cx - x, cy + y, c); drawPixel(cx + x, cy - y, c); drawPixel(cx - x, cy - y, c);
      drawPixel(cx + y, cy + x, c); drawPixel(cx - y, cy + x, c); drawPixel(cx + y, cy - x, c); drawPixel(cx - y, cy - x, c);
    }
  }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
    r = std::min<int32_t>(r, std::min(w, h) / 2);
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
        int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        if (dx && dy && dx * dx + dy * dy > r * r + r / 2) continue;
        drawPixel(x + i, y + j, c);
      }
  }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
    r = std::min<int32_t>(r, std::min(w, h) / 2);
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
        int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        bool in = !(dx && dy && dx * dx + dy * dy > r * r + r / 2);
        if (!in) continue;
        bool edge = i == 0 || j == 0 || i == w - 1 || j == h - 1;
        if (!edge && dx && dy) {
          int ex = dx + (i < r ? 1 : 1), ey = dy + 1;
          edge = (ex * ex + dy * dy > r * r + r / 2) || (dx * dx + ey * ey > r * r + r / 2);
        }
        if (edge) drawPixel(x + i, y + j, c);
      }
  }
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
    int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2}), miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    auto e = [](int ax, int ay, int bx, int by, int px, int py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
    for (int y = miny; y <= maxy; y++)
      for (int x = minx; x <= maxx; x++) {
        int a = e(x0, y0, x1, y1, x, y), b = e(x1, y1, x2, y2, x, y), d = e(x2, y2, x0, y0, x, y);
        if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) drawPixel(x, y, c);
      }
  }
  void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
    drawLine(x0, y0, x1, y1, c);
    drawLine(x1, y1, x2, y2, c);
    drawLine(x2, y2, x0, y0, c);
  }

  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
  uint16_t alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc) {
    uint16_t a = (alpha + 4) >> 3;
    uint32_t f = (fgc | (fgc << 16)) & 0x07E0F81F, b = (bgc | (bgc << 16)) & 0x07E0F81F;
    b += (((f - b) * a) >> 5);
    b &= 0x07E0F81F;
    return (uint16_t)(b | (b >> 16));
  }

  // --- Kenar yumuşatmalı çizimler (uzaklık alanı ile) ---
  void _cov(int x, int y, float cov, uint32_t fg, uint32_t bg) {
    if (cov <= 0.f) return;
    if (cov >= 1.f) { drawPixel(x, y, fg); return; }
    uint16_t b = bg > 0xFFFF ? readPixel(x, y) : (uint16_t)bg;
    drawPixel(x, y, alphaBlend((uint8_t)(cov * 255), (uint16_t)fg, b));
  }
  void fillSmoothCircle(int32_t cx, int32_t cy, int32_t r, uint32_t fg, uint32_t bg = 0x00FFFFFF) {
    for (int y = -r - 1; y <= r + 1; y++)
      for (int x = -r - 1; x <= r + 1; x++) {
        float d = sqrtf((float)(x * x + y * y));
        _cov(cx + x, cy + y, r + 0.5f - d, fg, bg);
      }
  }
  void drawSmoothCircle(int32_t cx, int32_t cy, int32_t r, uint32_t fg, uint32_t bg) { drawSmoothArc(cx, cy, r, r - 1, 0, 360, fg, bg); }
  void fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t fg, uint32_t bg = 0x00FFFFFF) {
    r = std::min<int32_t>(r, std::min(w, h) / 2);
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        float px = i + 0.5f, py = j + 0.5f;
        float qx = std::max(std::max(r - px, px - (w - r)), 0.f), qy = std::max(std::max(r - py, py - (h - r)), 0.f);
        float d = sqrtf(qx * qx + qy * qy);
        _cov(x + i, y + j, r + 0.5f - d - (r ? 0.f : -0.5f), fg, bg);
      }
  }
  void drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t bg = 0x00FFFFFF) {
    float rr = wd / 2.f;
    int x0 = (int)floorf(std::min(ax, bx) - rr - 1), x1 = (int)ceilf(std::max(ax, bx) + rr + 1);
    int y0 = (int)floorf(std::min(ay, by) - rr - 1), y1 = (int)ceilf(std::max(ay, by) + rr + 1);
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        float t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
        t = std::max(0.f, std::min(1.f, t));
        float qx = ax + t * dx - x, qy = ay + t * dy - y;
        _cov(x, y, rr + 0.5f - sqrtf(qx * qx + qy * qy), fg, bg);
      }
  }
  void drawWedgeLine(float ax, float ay, float bx, float by, float aw, float bw, uint32_t fg, uint32_t bg = 0x00FFFFFF) {
    drawWideLine(ax, ay, bx, by, (aw + bw) / 2, fg, bg);
  }
  // TFT_eSPI gibi: açı 0 aşağıda (saat 6), saat yönünde artar.
  void drawSmoothArc(int32_t cx, int32_t cy, int32_t r, int32_t ir, uint32_t a0, uint32_t a1, uint32_t fg, uint32_t bg, bool roundEnds = false) {
    for (int y = -r - 1; y <= r + 1; y++)
      for (int x = -r - 1; x <= r + 1; x++) {
        float d = sqrtf((float)(x * x + y * y));
        float cov = std::min(r + 0.5f - d, d - ir + 0.5f);
        if (cov <= 0) continue;
        float ang = atan2f((float)-x, (float)y) * 180.f / (float)M_PI;
        if (ang < 0) ang += 360.f;
        bool in = a0 <= a1 ? (ang >= a0 && ang <= a1) : (ang >= a0 || ang <= a1);
        if (a1 - a0 >= 360) in = true;
        if (!in) continue;
        _cov(cx + x, cy + y, std::min(cov, 1.f), fg, bg);
      }
  }

  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* d) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        uint16_t c = d[j * w + i];
        if (!_swap) c = (uint16_t)(c << 8 | c >> 8);  // TFT_eSPI: takas kapalıysa bellek sırası gider
        drawPixel(x + i, y + j, c);
      }
  }
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* d, uint16_t transp) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        uint16_t c = d[j * w + i];
        if (c == transp) continue;
        if (!_swap) c = (uint16_t)(c << 8 | c >> 8);
        drawPixel(x + i, y + j, c);
      }
  }

  // --- Yumuşak yazı tipi ---
  struct Glyph {
    uint32_t code, h, w, adv;
    int32_t dy, dx;
    const uint8_t* bmp;
  };
  void loadFont(const uint8_t* f) {
    _glyphs.clear();
    auto rd = [&](const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; };
    uint32_t n = rd(f);
    _ascent = rd(f + 16);
    _descent = rd(f + 20);
    const uint8_t* m = f + 24;
    const uint8_t* bmp = f + 24 + n * 28;
    _maxAscent = _ascent;
    _maxDescent = _descent;
    _space = (_ascent + _descent) / 4;
    for (uint32_t i = 0; i < n; i++, m += 28) {
      Glyph g{rd(m), rd(m + 4), rd(m + 8), rd(m + 12), (int32_t)rd(m + 16), (int32_t)rd(m + 20), bmp};
      bmp += g.w * g.h;
      if (g.dy > (int)_maxAscent && ((g.code > 0x20 && g.code < 0xA0) || g.code > 0xFF)) _maxAscent = g.dy;
      if ((int)g.h - g.dy > (int)_maxDescent && ((g.code > 0x20 && g.code < 0x7F) || g.code > 0xA0)) _maxDescent = g.h - g.dy;
      if (g.code == ' ') _space = g.adv;
      _glyphs.push_back(g);
    }
    _fontLoaded = true;
  }
  void unloadFont() { _fontLoaded = false; }
  void setTextColor(uint16_t fg) { _fg = _bg = fg; }
  void setTextColor(uint16_t fg, uint16_t bg, bool = false) { _fg = fg; _bg = bg; }
  void setTextDatum(uint8_t d) { _datum = d; }
  int16_t fontHeight() { return _maxAscent + _maxDescent; }
  static uint32_t _utf8(const char*& s) {
    uint8_t c = *s++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && *s) return ((c & 0x1F) << 6) | (*s++ & 0x3F);
    if ((c & 0xF0) == 0xE0 && s[0] && s[1]) { uint32_t u = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F); s += 2; return u; }
    if ((c & 0xF8) == 0xF0 && s[0] && s[1] && s[2]) { uint32_t u = ((c & 0x07) << 18) | ((s[0] & 0x3F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); s += 3; return u; }
    return c;
  }
  const Glyph* _find(uint32_t u) {
    for (auto& g : _glyphs)
      if (g.code == u) return &g;
    return nullptr;
  }
  int16_t textWidth(const String& s) { return textWidth(s.c_str()); }
  int16_t textWidth(const char* s) {
    if (!_fontLoaded) return strlen(s) * 6;
    int w = 0;
    while (*s) {
      uint32_t u = _utf8(s);
      const Glyph* g = _find(u);
      if (g) {
        if (w == 0 && g->dx < 0) w -= g->dx;
        if (*s) w += g->adv;
        else w += g->dx + g->w;
      } else {
        w += _space + 1;
      }
    }
    return w;
  }
  int16_t drawString(const String& s, int32_t x, int32_t y) { return drawString(s.c_str(), x, y); }
  int16_t drawString(const char* s, int32_t x, int32_t y) {
    if (!_fontLoaded) return 0;
    int cw = textWidth(s), ch = fontHeight();
    switch (_datum) {
      case TC_DATUM: x -= cw / 2; break;
      case TR_DATUM: x -= cw; break;
      case ML_DATUM: y -= ch / 2; break;
      case MC_DATUM: x -= cw / 2; y -= ch / 2; break;
      case MR_DATUM: x -= cw; y -= ch / 2; break;
      case BL_DATUM: y -= ch; break;
      case BC_DATUM: x -= cw / 2; y -= ch; break;
      case BR_DATUM: x -= cw; y -= ch; break;
      case L_BASELINE: y -= _maxAscent; break;
      case C_BASELINE: x -= cw / 2; y -= _maxAscent; break;
      case R_BASELINE: x -= cw; y -= _maxAscent; break;
    }
    int cx = x;
    bool first = true;
    while (*s) {
      uint32_t u = _utf8(s);
      const Glyph* g = _find(u);
      if (!g) {
        drawRect(cx, y + _maxAscent - _ascent, _space, _ascent, _fg);
        cx += _space + 1;
        continue;
      }
      if (first && g->dx < 0) cx -= g->dx;
      first = false;
      int gx = cx + g->dx, gy = y + _maxAscent - g->dy;
      for (uint32_t j = 0; j < g->h; j++)
        for (uint32_t i = 0; i < g->w; i++) {
          uint8_t a = g->bmp[j * g->w + i];
          if (!a) continue;
          uint16_t b = _fg == _bg ? readPixel(gx + i, gy + j) : _bg;
          drawPixel(gx + i, gy + j, a == 255 ? _fg : alphaBlend(a, _fg, b));
        }
      cx += g->adv;
    }
    return cw;
  }

 protected:
  void _alloc(int w, int h) {
    _w = w;
    _h = h;
    _buf.assign(w * h, 0);
  }
  int _iw, _ih, _w = 0, _h = 0;
  std::vector<uint16_t> _buf;
  bool _swap = false;
  std::vector<Glyph> _glyphs;
  uint32_t _ascent = 0, _descent = 0, _maxAscent = 0, _maxDescent = 0, _space = 4;
  bool _fontLoaded = false;
  uint16_t _fg = 0xFFFF, _bg = 0;
  uint8_t _datum = 0;
};

class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI* t) : TFT_eSPI(0, 0), _tft(t) {}
  void setColorDepth(int) {}
  void* createSprite(int16_t w, int16_t h) {
    _alloc(w, h);
    return _buf.data();
  }
  void deleteSprite() { _buf.clear(); _w = _h = 0; }
  bool created() { return !_buf.empty(); }
  void fillSprite(uint32_t c) { fillRect(0, 0, _w, _h, c); }
  void pushSprite(int32_t x, int32_t y) {
    for (int j = 0; j < _h; j++)
      for (int i = 0; i < _w; i++) _tft->drawPixel(x + i, y + j, _buf[j * _w + i]);
  }
  void pushSprite(int32_t x, int32_t y, uint16_t transp) {
    for (int j = 0; j < _h; j++)
      for (int i = 0; i < _w; i++)
        if (_buf[j * _w + i] != transp) _tft->drawPixel(x + i, y + j, _buf[j * _w + i]);
  }
  bool pushSprite(int32_t tx, int32_t ty, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
    for (int j = 0; j < sh; j++)
      for (int i = 0; i < sw; i++) _tft->drawPixel(tx + i, ty + j, readPixel(sx + i, sy + j));
    return true;
  }

 private:
  TFT_eSPI* _tft;
};
