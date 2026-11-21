#pragma once
// DAXEN VANE · Çizim uygulamasının saf hesapları (bilgisayarda sınanır; kullanımı apps/draw.h):
// nokta seyreltme, açıya ve ızgaraya yaslama, elips noktaları, yazının noktalara paketlenmesi,
// bir noktanın çizgiye uzaklığı.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <utility>
#include <vector>

// Ramer–Douglas–Peucker: düz çizgiden eps pikselden az sapan ara noktalar atılır, ilk ve son
// nokta kalır. xy: x,y çiftleri, yerinde sıkıştırılır. Döner: kalan nokta sayısı.
static int drSimplify(int16_t* xy, int n, float eps) {
  if (n < 3) return n;
  std::vector<uint8_t> keep(n, 0);
  keep[0] = keep[n - 1] = 1;
  std::vector<std::pair<int, int>> st;
  st.push_back({0, n - 1});
  while (!st.empty()) {
    std::pair<int, int> ab = st.back();
    st.pop_back();
    int a = ab.first, b = ab.second;
    float ax = xy[2 * a], ay = xy[2 * a + 1], dx = xy[2 * b] - ax, dy = xy[2 * b + 1] - ay;
    float l = sqrtf(dx * dx + dy * dy), best = -1;
    int bi = -1;
    for (int i = a + 1; i < b; i++) {
      float px = xy[2 * i] - ax, py = xy[2 * i + 1] - ay;
      float d = l > 0 ? fabsf(dy * px - dx * py) / l : sqrtf(px * px + py * py);
      if (d > best) best = d, bi = i;
    }
    if (bi >= 0 && best > eps) {
      keep[bi] = 1;
      st.push_back({a, bi});
      st.push_back({bi, b});
    }
  }
  int m = 0;
  for (int i = 0; i < n; i++)
    if (keep[i]) {
      xy[2 * m] = xy[2 * i];
      xy[2 * m + 1] = xy[2 * i + 1];
      m++;
    }
  return m;
}

// Noktanın [a,b] doğru parçasına uzaklığı
static float drSegDist(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
  float t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  float qx = ax + t * dx - px, qy = ay + t * dy - py;
  return sqrtf(qx * qx + qy * qy);
}

// Çizginin açısı 45°'nin katına 7°'den yakınsa tam o açıya getirilir (boy korunur)
static void drSnap45(int ax, int ay, int& bx, int& by) {
  float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
  if (l < 6) return;
  float a = atan2f(dy, dx), step = (float)M_PI / 4;
  float s = roundf(a / step) * step;
  if (fabsf(a - s) > 7.f * (float)M_PI / 180.f) return;
  bx = ax + (int)lroundf(cosf(s) * l);
  by = ay + (int)lroundf(sinf(s) * l);
}

// Izgaraya yaslama: v'yi origin + k·g'nin en yakınına getirir
static int drGridSnap(int v, int g, int origin) {
  int r = v - origin;
  int k = r >= 0 ? (r + g / 2) / g : -((-r + g / 2) / g);
  return origin + k * g;
}

// Elipsin çevresini kaç parçayla çizeceğimiz (kısa kenar 6 px civarı)
static int drOvalSegs(float rx, float ry) {
  float per = (float)M_PI * (3 * (rx + ry) - sqrtf((3 * rx + ry) * (rx + 3 * ry)));
  int n = (int)(per / 6);
  return n < 16 ? 16 : (n > 72 ? 72 : n);
}
static void drOvalPt(float cx, float cy, float rx, float ry, int i, int n, float& x, float& y) {
  float a = 2 * (float)M_PI * i / n;
  x = cx + rx * cosf(a);
  y = cy + ry * sinf(a);
}

// Yazı öğesi noktalara paketlenir: p[0] = konum, p[1] = (bayt sayısı, yazı tipi), sonra 4'er bayt
#define DR_TEXT_MAX 60
static int drTextPts(int len) { return 2 + (len + 3) / 4; }
static void drTextPack(int16_t* p, int x, int y, uint8_t font, const char* s, int len) {
  p[0] = x, p[1] = y, p[2] = len, p[3] = font;
  memset(p + 4, 0, ((len + 3) / 4) * 4);
  memcpy(p + 4, s, len);
}
// out en az DR_TEXT_MAX + 1 bayt; döner: bayt sayısı (bozuksa 0)
static int drTextUnpack(const int16_t* p, int n, char* out) {
  int len = p[2];
  if (n < 2 || len < 0 || len > DR_TEXT_MAX || drTextPts(len) > n) {
    out[0] = 0;
    return 0;
  }
  memcpy(out, p + 4, len);
  out[len] = 0;
  return len;
}
