#pragma once
// Dokunmatik kalibrasyon matematiği (donanımdan bağımsız; bilgisayarda test edilir).
//
// Ham ölçüm (rx, ry) ekrana afin dönüşümle eşlenir:
//   x = a·rx + b·ry + c
//   y = d·rx + e·ry + f
// Bu, eksenlerin yer değiştirmesini, ters dönmesini, ölçeği, kaymayı ve panelin hafif
// dönüklüğünü/eğikliğini birlikte düzeltir. Katsayılar birden çok noktadan en küçük kareler
// yöntemiyle bulunur (koordinatlar ortalamaya göre merkezlenir; 2×2 denklem, sayısal olarak sağlam).
#include <math.h>
#include <stdint.h>

struct TouchCal {
  float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
  bool ok = false;
};

struct CalFit {
  float rms = 0;     // ortalama karesel sapma (piksel)
  float maxErr = 0;  // en büyük sapma (piksel)
};

static inline void calApply(const TouchCal& k, float rx, float ry, float& x, float& y) {
  x = k.a * rx + k.b * ry + k.c;
  y = k.d * rx + k.e * ry + k.f;
}

// n >= 3 nokta, aynı doğru üzerinde olmamalı. Başarısızsa false.
static bool calSolve(const int* rx, const int* ry, const int* sx, const int* sy, int n, TouchCal& out, CalFit* fit = nullptr) {
  if (n < 3) return false;
  double mx = 0, my = 0, mX = 0, mY = 0;
  for (int i = 0; i < n; i++) {
    mx += rx[i];
    my += ry[i];
    mX += sx[i];
    mY += sy[i];
  }
  mx /= n;
  my /= n;
  mX /= n;
  mY /= n;
  double suu = 0, svv = 0, suv = 0, suX = 0, svX = 0, suY = 0, svY = 0;
  for (int i = 0; i < n; i++) {
    double u = rx[i] - mx, v = ry[i] - my, X = sx[i] - mX, Y = sy[i] - mY;
    suu += u * u;
    svv += v * v;
    suv += u * v;
    suX += u * X;
    svX += v * X;
    suY += u * Y;
    svY += v * Y;
  }
  double det = suu * svv - suv * suv;
  // noktalar ham uzayda yeterince yayılmış olmalı (ör. hepsi aynı yere basılmışsa çöker)
  if (!(det > 1e-6 * (suu * svv + 1)) || suu < 1 || svv < 1) return false;
  double a = (suX * svv - svX * suv) / det, b = (svX * suu - suX * suv) / det;
  double d = (suY * svv - svY * suv) / det, e = (svY * suu - suY * suv) / det;
  TouchCal k;
  k.a = a;
  k.b = b;
  k.c = mX - a * mx - b * my;
  k.d = d;
  k.e = e;
  k.f = mY - d * mx - e * my;
  k.ok = isfinite(k.a) && isfinite(k.b) && isfinite(k.c) && isfinite(k.d) && isfinite(k.e) && isfinite(k.f);
  if (!k.ok) return false;
  if (fit) {
    double s2 = 0, mxE = 0;
    for (int i = 0; i < n; i++) {
      float x, y;
      calApply(k, rx[i], ry[i], x, y);
      double ex = x - sx[i], ey = y - sy[i], e2 = ex * ex + ey * ey;
      s2 += e2;
      if (e2 > mxE) mxE = e2;
    }
    fit->rms = sqrt(s2 / n);
    fit->maxErr = sqrt(mxE);
  }
  out = k;
  return true;
}

// Eski iki eksenli kayıttan (x1..x2 → 20..300, y1..y2 → 20..220, eksen takası) afin katsayılar
static TouchCal calFromLegacy(bool swapXY, int x1, int x2, int y1, int y2) {
  TouchCal k;
  if (x2 == x1 || y2 == y1) return k;
  float A = 280.f / (x2 - x1), C = 20.f - x1 * A;
  float B = 200.f / (y2 - y1), D = 20.f - y1 * B;
  if (!swapXY) {
    k.a = A, k.b = 0, k.c = C;
    k.d = 0, k.e = B, k.f = D;
  } else {
    k.a = 0, k.b = A, k.c = C;
    k.d = B, k.e = 0, k.f = D;
  }
  k.ok = true;
  return k;
}

// Afin katsayılardan eski biçime yaklaşık dönüş (eski bir sürüme dönülürse de doğru dokunsun diye)
static void calToLegacy(const TouchCal& k, bool& swapXY, int& x1, int& x2, int& y1, int& y2) {
  swapXY = fabsf(k.b) > fabsf(k.a);
  const float mid = 2048;
  if (!swapXY) {
    float ox = k.b * mid + k.c, oy = k.d * mid + k.f;
    x1 = lroundf((20 - ox) / k.a);
    x2 = lroundf((300 - ox) / k.a);
    y1 = lroundf((20 - oy) / k.e);
    y2 = lroundf((220 - oy) / k.e);
  } else {
    float ox = k.a * mid + k.c, oy = k.e * mid + k.f;
    x1 = lroundf((20 - ox) / k.b);
    x2 = lroundf((300 - ox) / k.b);
    y1 = lroundf((20 - oy) / k.d);
    y2 = lroundf((220 - oy) / k.d);
  }
}

// Küçük diziler için ortanca (yerinde sıralar)
static int calMedian(int* v, int n) {
  for (int i = 1; i < n; i++) {
    int t = v[i], j = i - 1;
    while (j >= 0 && v[j] > t) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = t;
  }
  return n ? v[n / 2] : 0;
}
