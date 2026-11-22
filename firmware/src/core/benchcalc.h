#pragma once
// CYD Infinity Engine · saf hesap (bilgisayarda sınanır). Kullanımı apps/bench.h, yöntem docs/benchmark.md.
//  - Mandelbrot satır çekirdekleri: float, Q4.28 sabit nokta (32 bit, 64 bit çarpım) ve double
//  - yumuşak renk (kaçış sayısının kesirli hâli) ve 256 girişli palet tablosu
//  - yakınlaşma yolu: hedef noktalar, karelik büyütme, hassasiyet katmanı, iterasyon sınırı
//  - kare süresi istatistikleri ve bellek parçalanması
// Hepsi deterministik: aynı ayarlarla her kart, her sürüm aynı kare dizisini hesaplar.
#include <math.h>
#include <stdint.h>
#include <stdio.h>

enum BnArith : uint8_t { BA_FLOAT, BA_FIXED, BA_DOUBLE, BA_N, BA_END = 0xFF };
static const char* const BA_NAME[BA_N] = {"float", "sabit nokta", "double"};

#if defined(__GNUC__) && !defined(HOST)
#define BN_HOT __attribute__((optimize("O2")))  // derleme -Os; sıcak döngüler hızda derlenir
#else
#define BN_HOT
#endif

// --- Yumuşak renk ---
// log2 yaklaşığı (bağıl hata < %0.5; renk için yeterli, kütüphane log2f'den çok hızlı)
static inline float bnLog2(float x) {
  union {
    float f;
    uint32_t i;
  } v = {x};
  float e = (float)(int)((v.i >> 23) & 255) - 128.f;
  v.i = (v.i & 0x007FFFFFu) | 0x3F800000u;
  float m = v.f;  // [1, 2)
  return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}
static const float BN_DENS = 4.f;  // iterasyon başına palet adımı (64 iterasyonda bir tam tur)
// mu = it + 1 − log2(log2 |z|) = it + 2 − log2(log2 |z|²); r2 > 256 olmalı
static inline uint8_t bnSmooth(int it, float r2, uint8_t off) {
  float mu = (float)it + 2.f - bnLog2(bnLog2(r2));
  return (uint8_t)((int)((mu + 4.f) * BN_DENS) + off);
}

// Ana kardioid ve 2. dönem diski: buradaki noktalar hiç kaçmaz, iterasyonsuz atlanır
static inline bool bnInterior(float x, float y) {
  float y2 = y * y, xq = x - 0.25f, q = xq * xq + y2;
  if (q * (q + xq) <= 0.25f * y2) return true;
  float x1 = x + 1.f;
  return x1 * x1 + y2 <= 0.0625f;
}

// --- Satır çekirdekleri ---
// n nokta hesaplanır; her biri rep kez yan yana yazılır (yarım çözünürlükte 2). out = nullptr: yalnız sayım.
struct BnRowArgs {
  uint16_t* out;
  int n, rep, maxIt;
  const uint16_t* lut;  // 256 renk
  uint16_t insideCol;
  uint8_t off;    // palet kaydırması (renk akışı)
  bool interior;  // kardioid/disk testi
};
struct BnRowOut {
  uint32_t iters = 0;   // yapılan iterasyon (z ← z² + c)
  uint32_t inside = 0;  // içeride kalan nokta
  uint32_t esc = 0;     // kaçan nokta
  uint32_t minIt = 0xFFFFFFFFu, maxEsc = 0;  // kaçanların en az / en çok iterasyonu
  void add(const BnRowOut& o) {
    iters += o.iters, inside += o.inside, esc += o.esc;
    if (o.minIt < minIt) minIt = o.minIt;
    if (o.maxEsc > maxEsc) maxEsc = o.maxEsc;
  }
};

static inline void bnPut(const BnRowArgs& a, uint16_t*& out, bool esc, int it, float r2, BnRowOut& o) {
  uint16_t c;
  if (esc) {
    o.esc++;
    if ((uint32_t)it < o.minIt) o.minIt = it;
    if ((uint32_t)it > o.maxEsc) o.maxEsc = it;
    c = a.lut ? a.lut[bnSmooth(it, r2, a.off)] : 0;
  } else {
    o.inside++;
    c = a.insideCol;
  }
  if (out)
    for (int k = 0; k < a.rep; k++) *out++ = c;
}

// float: kaçış yarıçapı 16 (|z|² > 256)
BN_HOT static void bnRowF(const BnRowArgs& a, float cr0, float dcr, float ci, BnRowOut& o) {
  uint16_t* out = a.out;
  const int mx = a.maxIt;
  uint32_t its = 0;
  for (int i = 0; i < a.n; i++) {
    float cr = cr0 + dcr * (float)i;
    float zr = 0, zi = 0, r2 = 0, i2 = 0;
    int it = 0;
    bool esc = false;
    if (!(a.interior && bnInterior(cr, ci))) {
      for (; it < mx; it++) {
        r2 = zr * zr;
        i2 = zi * zi;
        if (r2 + i2 > 256.f) {
          esc = true;
          break;
        }
        zi = (zr + zr) * zi + ci;
        zr = r2 - i2 + cr;
      }
    }
    its += it;
    bnPut(a, out, esc, it, r2 + i2, o);
  }
  o.iters += its;
}

// Q4.28 sabit nokta: z 32 bit, çarpımlar 64 bit (Xtensa mull + mulsh). Kaçış |z|² > 4 (değer aralığı
// ±8 taşmasın); renk için kaçtıktan sonra float ile |z|² > 256 olana kadar sürdürülür (sayıma girer).
// c: Q4.44 (64 bit) tutulur, piksel adımı yuvarlanmadan toplanır.
static const int BN_Q = 28;
static inline int64_t bnToQ44(double v) { return (int64_t)llround(v * 17592186044416.0); }  // 2^44
BN_HOT static void bnRowQ(const BnRowArgs& a, int64_t cr0, int64_t dcr, int64_t ci44, BnRowOut& o) {
  uint16_t* out = a.out;
  const int mx = a.maxIt;
  const int32_t ci = (int32_t)(ci44 >> 16);
  const float qs = 1.f / (float)(1 << BN_Q), cif = (float)ci * qs;
  const int64_t lim = (int64_t)4 << (2 * BN_Q);
  uint32_t its = 0;
  for (int i = 0; i < a.n; i++) {
    int32_t cr = (int32_t)((cr0 + dcr * i) >> 16);
    float crf = (float)cr * qs;
    int32_t zr = 0, zi = 0;
    int64_t r2 = 0, i2 = 0;
    int it = 0;
    bool esc = false;
    float m2 = 0;
    if (!(a.interior && bnInterior(crf, cif))) {
      for (; it < mx; it++) {
        r2 = (int64_t)zr * zr;
        i2 = (int64_t)zi * zi;
        if (r2 + i2 > lim) {
          esc = true;
          break;
        }
        int32_t nzr = (int32_t)((r2 - i2) >> BN_Q) + cr;
        zi = (int32_t)(((int64_t)zr * zi) >> (BN_Q - 1)) + ci;
        zr = nzr;
      }
      if (esc) {
        float fr = (float)zr * qs, fi = (float)zi * qs;
        m2 = fr * fr + fi * fi;
        for (int k = 0; k < 6 && m2 <= 256.f; k++, it++) {
          float t = fr * fr - fi * fi + crf;
          fi = (fr + fr) * fi + cif;
          fr = t;
          m2 = fr * fr + fi * fi;
        }
      }
    }
    its += it;
    bnPut(a, out, esc, it, m2 > 256.f ? m2 : 257.f, o);
  }
  o.iters += its;
}

// double: ESP32'de donanım desteği yok (yazılımla, float'tan onlarca kat yavaş); derin yakınlaşma için
BN_HOT static void bnRowD(const BnRowArgs& a, double cr0, double dcr, double ci, BnRowOut& o) {
  uint16_t* out = a.out;
  const int mx = a.maxIt;
  uint32_t its = 0;
  for (int i = 0; i < a.n; i++) {
    double cr = cr0 + dcr * i;
    double zr = 0, zi = 0, r2 = 0, i2 = 0;
    int it = 0;
    bool esc = false;
    if (!(a.interior && bnInterior((float)cr, (float)ci))) {
      for (; it < mx; it++) {
        r2 = zr * zr;
        i2 = zi * zi;
        if (r2 + i2 > 256.0) {
          esc = true;
          break;
        }
        zi = (zr + zr) * zi + ci;
        zr = r2 - i2 + cr;
      }
    }
    its += it;
    bnPut(a, out, esc, it, (float)(r2 + i2), o);
  }
  o.iters += its;
}

// Bir satır parçası, aritmetiğe göre. x0: parçanın ilk noktası (hesap ızgarasında), cols: ızgara genişliği
struct BnFrameGeo {
  double cx, cy, span;  // görüntünün ortası ve genişliği (karmaşık düzlem)
  int cols, rows;       // hesaplanan ızgara (yarım çözünürlükte ekranın yarısı)
  double px() const { return span / cols; }
  double re(int x) const { return cx + (x - cols * 0.5 + 0.5) * px(); }
  double im(int y) const { return cy - (y - rows * 0.5 + 0.5) * px(); }  // ekranda y aşağı
};
static void bnRow(uint8_t arith, const BnRowArgs& a, const BnFrameGeo& g, int x0, int y, BnRowOut& o) {
  double r0 = g.re(x0), d = g.px(), ci = g.im(y);
  if (arith == BA_FLOAT) bnRowF(a, (float)r0, (float)d, (float)ci, o);
  else if (arith == BA_FIXED) bnRowQ(a, bnToQ44(r0), bnToQ44(d), bnToQ44(ci), o);
  else bnRowD(a, r0, d, ci, o);
}

// --- Palet ---
struct BnPal {
  const char* name;
  uint8_t n;
  uint32_t stop[6];  // 0xRRGGBB, döngüsel (son durak ilkine döner)
};
static const BnPal BN_PALS[] = {
    {"Obsidyen", 5, {0x0A0C10, 0x3A4048, 0xC5C9CE, 0x2ED3E0, 0x0A0C10}},
    {"Buz", 5, {0x020814, 0x0B3D91, 0x1EC8FF, 0xE8FBFF, 0x020814}},
    {"Magma", 6, {0x050003, 0x5A0A12, 0xD2401C, 0xFFB238, 0xFFF4C8, 0x050003}},
    {"Zümrüt", 5, {0x020A08, 0x0D5C4A, 0x3FD9A0, 0xD8FFF0, 0x020A08}},
    {"Gri", 3, {0x000000, 0xE0E0E0, 0x000000}},
};
static const int BN_NPAL = sizeof(BN_PALS) / sizeof(BN_PALS[0]);
static inline uint16_t bnRgb(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c & 0xFF) >> 3));
}
// swap: bayt sırası ters (ekrana takassız gönderim için hazır)
static void bnBuildLut(uint16_t* lut, int pal, bool swap) {
  const BnPal& p = BN_PALS[pal % BN_NPAL];
  for (int i = 0; i < 256; i++) {
    float t = i * (p.n - 1) / 256.f;
    int s = (int)t;
    float f = t - s;
    uint32_t a = p.stop[s], b = p.stop[s + 1 < p.n ? s + 1 : s];
    auto ch = [&](int sh) { return (uint32_t)(((a >> sh) & 255) + (((int)((b >> sh) & 255) - (int)((a >> sh) & 255)) * f) + 0.5f); };
    uint16_t c = bnRgb(ch(16) << 16 | ch(8) << 8 | ch(0));
    lut[i] = swap ? (uint16_t)(c << 8 | c >> 8) : c;
  }
}

// --- Yakınlaşma yolu ---
// Sınır üzerindeki bilinen noktalar: kenar sonsuza kadar ayrıntılı kalır.
struct BnTarget {
  double x, y;
  const char* name;
};
static const BnTarget BN_TARGETS[] = {
    {-0.743643887037158704752191506114774, 0.131825904205311970493132056385139, "Denizatı vadisi"},
    {-0.10109636384562, 0.95628651080914, "Misiurewicz M4,1"},
    {-1.4011551890920506, 0.0, "Feigenbaum noktası"},
    {0.0, 1.0, "c = i dendriti"},
    {-0.77568377, 0.13646737, "Sarmal"},
};
static const int BN_NTARGET = sizeof(BN_TARGETS) / sizeof(BN_TARGETS[0]);
static const double BN_SPAN0 = 3.0;
static const float BN_ZOOM[3] = {0.985f, 0.97f, 0.94f};  // karelik büyütme: yavaş, orta, hızlı
static const int BN_BASEIT[4] = {48, 96, 192, 384};      // karmaşıklık: düşük, orta, yüksek, aşırı

// Hassasiyet katmanı: piksel adımı sayının çözünürlüğünden yeterince büyük mü?
// float: 24 bit; |c| ≈ 1 civarında adım 4e-7 (~7 ulp) altında görüntü kareleşir.
// Q4.28: 2^-28 ≈ 3,7e-9; adım 4e-8 (~11 birim) altına inilmez. double: 1e-13.
static uint8_t bnTier(double px, double cx, double cy, uint8_t shallow, bool allowDouble) {
  double m = fabs(cx) > fabs(cy) ? fabs(cx) : fabs(cy);
  if (m < 1) m = 1;
  if (px >= 4e-7 * m) return shallow;
  if (px >= 4e-8 && m < 3) return BA_FIXED;
  if (allowDouble && px >= 1e-13 * m) return BA_DOUBLE;
  return BA_END;
}
// İterasyon sınırı derinlikle büyür (sınırda ayrıntı kaybolmasın)
static int bnMaxIt(int base, int mul, double span) {
  double z = log2(BN_SPAN0 / span);
  if (z < 0) z = 0;
  double v = base * mul * (1.0 + z / 12.0);
  return v > 4096 ? 4096 : (int)v;
}

struct BnPathCfg {
  uint8_t zoom;     // 0..2
  uint8_t cplx;     // 0..3
  uint8_t shallow;  // BA_FLOAT ya da BA_FIXED
  bool extreme;     // aşırı yük: double katmanı açık, iterasyon ×2
  int cols;         // hesaplanan ızgara genişliği
};
struct BnStep {  // yolun bir karesi
  uint8_t target = 0;
  uint32_t k = 0;  // hedefteki kare sırası
  double span = BN_SPAN0;
  uint8_t arith = BA_FLOAT;
  int maxIt = 0;
};
static BnStep bnStepAt(const BnPathCfg& c, uint8_t target, uint32_t k) {
  BnStep s;
  s.target = target % BN_NTARGET;
  s.k = k;
  s.span = BN_SPAN0 * pow((double)BN_ZOOM[c.zoom % 3], (double)k);
  const BnTarget& t = BN_TARGETS[s.target];
  s.arith = bnTier(s.span / c.cols, t.x, t.y, c.shallow, c.extreme);
  s.maxIt = bnMaxIt(BN_BASEIT[c.cplx % 4], c.extreme ? 2 : 1, s.span);
  return s;
}
// Sonraki kare. flat: önceki kare tek renk (hassasiyet sınırı) — sonraki hedefe geçilir.
static BnStep bnNext(const BnPathCfg& c, const BnStep& s, bool flat) {
  BnStep n = bnStepAt(c, s.target, s.k + 1);
  if (flat || n.arith == BA_END) n = bnStepAt(c, s.target + 1, 0);
  return n;
}
// Kare tek renk mi: hemen hepsi içeride ya da kaçanların hepsi aynı adımda
static bool bnFlat(const BnRowOut& f) {
  uint32_t n = f.inside + f.esc;
  if (!n) return false;
  if (f.inside * 100 >= n * 97) return true;
  return f.inside == 0 && f.esc && f.maxEsc - f.minIt <= 1;
}

// --- Ölçüm ---
struct BnTimes {  // kare aralıkları (µs): son, en kısa, en uzun, toplam
  uint32_t n = 0, last = 0, mn = 0, mx = 0;
  uint64_t sum = 0;
  void add(uint32_t us) {
    if (!us) us = 1;
    last = us;
    if (!n || us < mn) mn = us;
    if (us > mx) mx = us;
    sum += us;
    n++;
  }
  float fpsCur() const { return last ? 1e6f / last : 0; }
  float fpsMax() const { return mn ? 1e6f / mn : 0; }
  float fpsMin() const { return mx ? 1e6f / mx : 0; }
  float msAvg() const { return n ? sum / 1000.f / n : 0; }
};
// Parçalanma: en büyük blok boş belleğin ne kadarı değil (yüzde)
static int bnFragPct(uint32_t freeB, uint32_t maxBlk) {
  if (!freeB || maxBlk >= freeB) return 0;
  return (int)(100 - (uint64_t)maxBlk * 100 / freeB);
}
// Sayı biçimi: 0,1 kesinlikle (Türkçe ondalık virgül)
static void bnFmt(char* b, int n, float v, int dec) {
  if (dec <= 0) snprintf(b, n, "%ld", (long)lroundf(v));
  else snprintf(b, n, "%.*f", dec, v);
  for (char* p = b; *p; p++)
    if (*p == '.') *p = ',';
}
