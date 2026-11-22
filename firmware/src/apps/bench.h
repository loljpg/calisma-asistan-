#pragma once
// CYD Infinity Engine (Uygulamalar → Infinity): kartın işlemcisini, ekranını ve belleğini ölçen kıyas
// motoru. Ana görüntü sonsuz yakınlaşan Mandelbrot kümesi; her nokta kartta, o an hesaplanır.
//  - İki işçi görev (çekirdek 0 ve 1) satır bantlarını sırayla alıp ara belleğe hesaplar; arayüz görevi
//    (çekirdek 1) hazır bantları sırasıyla ekrana yollar. Hesap süresi ve ekran aktarımı ayrı ölçülür.
//    İşçiler bir kare önden gidebilir (aktarım sürerken sonraki kare hesaplanır).
//  - Maks. performans: yarım çözünürlük (2×2), float ya da sabit nokta. Aşırı yük: tam çözünürlük, iki kat
//    iterasyon, derinde double, denetimli bellek baskısı ve sürekli izleme; bellek kritikleşirse, bir işçi
//    biterse ya da 20 sn ilerlemezse test güvenle durur.
//  - Testler: işlemci (tek ve çift çekirdek, üç aritmetik), ekran (doldurma, dikdörtgen, piksel, görüntü,
//    kısmi güncelleme), bellek (ayırma, yazma/okuma, doğrulama, parçalanma, toparlanma).
// Bellek yalnızca hesap sürerken ayrılır; boştayken ve kapanınca hepsi geri verilir. Test sürerken bulutun
// canlı akışı duraklatılır (netHold), sonra kendiliğinden döner. Yöntem ve sınırlar: docs/benchmark.md
#include "../os/shell.h"
#include "../core/benchcalc.h"
#include <stdarg.h>
#ifndef HOST
#include <esp_heap_caps.h>
#endif

static void openInReader(const String& path);  // os/registry.h

#define BN_DIR "/daxen/kiyas"
#ifdef SPI_FREQUENCY
#define BN_SPI_REQ SPI_FREQUENCY
#else
#define BN_SPI_REQ 55000000
#endif
static const int BN_W = 320, BN_FY = HDR_H, BN_SY = 206;
static const int BN_VH[2] = {176, 112};  // fraktal yüksekliği: tam ekran, pano
static const int BN_NSLOT = 4, BN_STACK = 3072, BN_SEG = 32, BN_BALLN = 64;
static const uint16_t BN_ACC = rgbn(0x2E, 0xD3, 0xE0);  // tek vurgu: turkuaz
static const uint16_t BN_WARN = rgbn(0xE0, 0x8A, 0x3C);

// --- Ayarlar (NVS "bench") ---
struct BnCfg {
  uint8_t mode = 0, cplx = 1, zoom = 1, pal = 0, arith = 0, dur = 1, view = 0, mem = 1;
};
static BnCfg bnCfg;
static const char* const BN_MODE[2] = {"Maks. performans", "Aşırı yük"};
static const char* const BN_MODE_S[2] = {"MAKS", "AŞIRI"};
static const char* const BN_CPLX[4] = {"Düşük", "Orta", "Yüksek", "Aşırı"};
static const char* const BN_ZOOMN[3] = {"Yavaş", "Orta", "Hızlı"};
static const char* const BN_DURN[4] = {"30 sn", "60 sn", "3 dk", "Sürekli"};
static const uint16_t BN_DURS[4] = {30, 60, 180, 0};
static const char* const BN_VIEWN[2] = {"Tam ekran", "Pano"};
static const uint8_t BN_MEMKB[3] = {32, 64, 96};

#ifndef HOST
static portMUX_TYPE bnMux = portMUX_INITIALIZER_UNLOCKED;
#define BN_LOCK() portENTER_CRITICAL(&bnMux)
#define BN_UNLOCK() portEXIT_CRITICAL(&bnMux)
#else
#define BN_LOCK() \
  do {            \
  } while (0)
#define BN_UNLOCK() \
  do {              \
  } while (0)
#endif

// --- Durum ---
enum : uint8_t { SL_FREE, SL_BUSY, SL_READY };
struct BnSlot {
  uint16_t* px = nullptr;  // BN_W × bh, bayt sırası ekranınki
  volatile uint8_t st = SL_FREE;
  uint16_t band = 0;
  uint32_t gen = 0, us = 0;
  BnRowOut res;
};
struct BnJob {  // bir kare (ya da ölçüm yükü)
  uint32_t gen = 0;
  BnFrameGeo geo{0, 0, BN_SPAN0, 160, 88};
  uint8_t arith = BA_FLOAT, step = 2, off = 0;
  int maxIt = 64, nBands = 0, brow = 4;  // brow: bant başına hesaplanan satır
  bool interior = true, discard = false;
  BnStep path;
};
enum BnState : uint8_t { BS_IDLE, BS_STILL, BS_RUN, BS_SUITE };
enum BnPage : uint8_t { BP_LIVE, BP_SUITE, BP_RESULT };
enum BnStop : uint8_t { BST_NONE, BST_USER, BST_TIME, BST_MEM, BST_TASK, BST_STALL, BST_ALLOC, BST_CLOSE };

struct BnRun {  // fraktal testi ölçümleri
  uint32_t t0 = 0, lastFrame = 0, lastTick = 0, pausedMs = 0, pauses = 0;
  BnTimes ft;
  uint64_t compUs = 0, xferUs = 0, pxComp = 0, pxScreen = 0, iters = 0;
  uint32_t freeStart = 0, minFree = 0xFFFFFFFF, minBlk = 0xFFFFFFFF, maxFrag = 0, clipped = 0, uiUs = 0;
  float maxMits = 0;
  uint64_t winIters = 0;
  uint32_t winT = 0, lastMon = 0, lastUi = 0, beatSeen[2] = {0, 0}, beatAt = 0;
  int itLo = 0, itHi = 0;
  uint8_t stop = BST_NONE, deepest = 0, targets = 1;
  BnStep last;
  double minSpan = BN_SPAN0;
};

struct BnCtx {
  BnSlot slot[BN_NSLOT];
  int nSlot = 0, bh = 8, wmax = 0;
  uint16_t lut[256];
  // işçiler
  int pid[2] = {-1, -1};
  volatile bool quit = false;
  bool oneShot = false;
  volatile uint32_t beat[2] = {0, 0};
  uint32_t busyUs[2] = {0, 0}, bands[2] = {0, 0}, startUs[2] = {0, 0}, endUs[2] = {0, 0}, spawnUs = 0;
  uint64_t iters[2] = {0, 0};
  // kare hattı: işçiler cg'yi, arayüz pg'yi işler (pg ≤ cg ≤ pg+1)
  BnJob job[2];
  volatile uint32_t cg = 0, pg = 0;
  volatile int next = 0;
  volatile bool pendOk = false;
  int pushNext = 0;
  BnRowOut acc;
  uint32_t accComp = 0, accXfer = 0;
  bool flat[2] = {false, false};  // kare tek renk miydi (kuşak tek/çift)
  BnStep flatPath[2];
  // bellek baskısı (aşırı yük)
  void* ball[BN_BALLN];
  int nBall = 0;
  uint32_t ballAllocs = 0, ballFails = 0, ballMaxUs = 0;
  uint64_t ballUs = 0;
  BnRun r;
};
static BnCtx* B = nullptr;
static BnState bnSt = BS_IDLE;
static BnPage bnPage = BP_LIVE;
static uint8_t bnStripPg = 0, bnResPg = 0;
static bool bnLoaded = false, bnHaveFrame = false, bnWantStill = false;
static String bnErr;                 // bellek yetmediyse nedeni (fraktal alanında yazılır)
static uint32_t bnErrAt = 0;
static std::vector<String> bnRep;    // son rapor: "#Başlık" ya da "etiket\tdeğer"
static String bnRepPath, bnRepTitle;
static BnStep bnView;  // gösterilen görüntü (durunca orada kalır)

static bool bnBusy() { return bnSt != BS_IDLE; }  // os/membudget.h
static int bnVh() { return BN_VH[bnCfg.view & 1]; }
static bool bnExtreme() { return bnCfg.mode == 1; }

static String bnS(const char* fmt, ...);
static String bnMSS(uint32_t s) { return bnS("%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60)); }
static String bnSpanX(double span) {  // büyütme: ×1,2e5
  double z = BN_SPAN0 / span;
  if (z < 1000) return bnS(z < 10 ? "×%.1f" : "×%.0f", z);
  return bnS("×%.1e", z);
}

static uint32_t bnPsram() {
#ifndef HOST
  return ESP.getPsramSize();
#else
  return 0;
#endif
}

// --- Ayarların kaydı ---
static void bnLoad() {
  if (bnLoaded) return;
  bnLoaded = true;
  Lock l;
  prefs.begin("bench", true);
  uint8_t* f[] = {&bnCfg.mode, &bnCfg.cplx, &bnCfg.zoom, &bnCfg.pal, &bnCfg.arith, &bnCfg.dur, &bnCfg.view, &bnCfg.mem};
  const char* k[] = {"mode", "cplx", "zoom", "pal", "arith", "dur", "view", "mem"};
  const uint8_t mx[] = {2, 4, 3, (uint8_t)BN_NPAL, 2, 4, 2, 3};
  for (int i = 0; i < 8; i++) *f[i] = std::min<uint8_t>(prefs.getUChar(k[i], *f[i]), mx[i] - 1);
  prefs.end();
}
static void bnSave() {
  Lock l;
  prefs.begin("bench", false);
  prefs.putUChar("mode", bnCfg.mode);
  prefs.putUChar("cplx", bnCfg.cplx);
  prefs.putUChar("zoom", bnCfg.zoom);
  prefs.putUChar("pal", bnCfg.pal);
  prefs.putUChar("arith", bnCfg.arith);
  prefs.putUChar("dur", bnCfg.dur);
  prefs.putUChar("view", bnCfg.view);
  prefs.putUChar("mem", bnCfg.mem);
  prefs.end();
}

static BnPathCfg bnPathCfg() {
  BnPathCfg c;
  c.zoom = bnCfg.zoom;
  c.cplx = bnCfg.cplx;
  c.shallow = bnCfg.arith ? BA_FIXED : BA_FLOAT;
  c.extreme = bnExtreme();
  c.cols = bnExtreme() ? BN_W : BN_W / 2;
  return c;
}

// ============================================================================
// Bellek: bant ara bellekleri ve işçi yığınları (yalnızca hesap sürerken)
// ============================================================================
struct BnPlan {
  int slots, bh, workers;
};
static const BnPlan BN_PLANS[] = {{4, 8, 2}, {3, 8, 2}, {2, 8, 1}, {2, 4, 1}};
static uint32_t bnPlanNeed(const BnPlan& p) { return (uint32_t)p.slots * BN_W * p.bh * 2 + p.workers * (BN_STACK + 400); }

static void bnFreeSlots() {
  if (!B) return;
  for (auto& s : B->slot) {
    free(s.px);
    s.px = nullptr;
    s.st = SL_FREE;
  }
  B->nSlot = 0;
}
static bool bnStopWorkers();
static bool bnAllocSlots(String* why) {
  if (!bnStopWorkers()) {  // önceki hesap bitmeden ara belleklere dokunulmaz
    if (why) *why = "Önceki hesap henüz bitmedi, birazdan yeniden dene";
    return false;
  }
  bnFreeSlots();
  for (auto& p : BN_PLANS) {
    uint32_t blk = std::max<uint32_t>(BN_W * p.bh * 2, BN_STACK);
    if (!memAsk(MJ_BENCH, bnPlanNeed(p), blk, why)) continue;
    bool ok = true;
    for (int i = 0; i < p.slots && ok; i++) ok = (B->slot[i].px = (uint16_t*)malloc(BN_W * p.bh * 2)) != nullptr;
    if (!ok) {
      bnFreeSlots();
      continue;
    }
    B->nSlot = p.slots, B->bh = p.bh, B->wmax = p.workers;
    return true;
  }
  return false;
}

// ============================================================================
// İşçiler
// ============================================================================
static inline void bnYield(uint32_t& lastY) {
#ifndef HOST
  uint32_t now = micros();
  if (now - lastY > 25000) {  // IDLE görevleri de çalışsın (görev bekçisi, silinen görevlerin belleği)
    vTaskDelay(1);
    lastY = micros();
  }
#else
  (void)lastY;
#endif
}

// Bir bant alır ve hesaplar; iş yoksa false. Hem işçi görevlerden hem (önizlemede) arayüzden çağrılır.
static bool bnWorkStep(int w, uint32_t& lastY) {
  BnCtx& c = *B;
  int s = -1, band = 0;
  uint32_t gen = 0;
  BN_LOCK();
  if (c.next >= c.job[c.cg & 1].nBands && c.cg == c.pg && c.pendOk) {  // sonraki kare hazır: önden başla
    c.cg = c.cg + 1;
    c.next = 0;
    c.pendOk = false;
  }
  if (c.next < c.job[c.cg & 1].nBands) {
    for (int i = 0; i < c.nSlot; i++)
      if (c.slot[i].st == SL_FREE) {
        s = i;
        break;
      }
    if (s >= 0) {
      band = c.next;
      c.next = band + 1;
      gen = c.cg;
      c.slot[s].st = SL_BUSY;
      c.slot[s].band = band;
      c.slot[s].gen = gen;
    }
  }
  BN_UNLOCK();
  if (s < 0) return false;
  const BnJob& j = c.job[gen & 1];
  BnSlot& sl = c.slot[s];
  BnRowOut res;
  uint32_t t0 = micros(), slept = 0;
  BnRowArgs a{nullptr, 0, j.step, j.maxIt, c.lut, 0, j.off, j.interior};
  for (int r = 0; r < j.brow && !c.quit; r++) {
    int gy = band * j.brow + r;
    if (gy >= j.geo.rows) break;
    uint16_t* line = sl.px + r * j.step * BN_W;
    for (int x0 = 0; x0 < j.geo.cols && !c.quit; x0 += BN_SEG) {
      a.out = j.discard ? nullptr : line + x0 * j.step;
      a.n = std::min(BN_SEG, j.geo.cols - x0);
      bnRow(j.arith, a, j.geo, x0, gy, res);
      c.beat[w] = c.beat[w] + 1;
      uint32_t y0 = micros();
      bnYield(lastY);
      slept += micros() - y0;
    }
    if (j.step == 2 && !j.discard) memcpy(line + BN_W, line, BN_W * 2);
  }
  sl.us = micros() - t0 - slept;
  sl.res = res;
  c.busyUs[w] += sl.us;
  c.iters[w] += res.iters;
  c.bands[w]++;
  BN_LOCK();
  sl.st = j.discard ? SL_FREE : SL_READY;
  BN_UNLOCK();
  return true;
}

static bool bnAllClaimed() { return B->next >= B->job[B->cg & 1].nBands && !B->pendOk; }

static void bnWorker(Proc* p) {
  int w = p->name == "kiyas-0" ? 0 : 1;
  BnCtx& c = *B;
  c.startUs[w] = micros();
  uint32_t lastY = micros();
  while (!p->killReq && !c.quit) {
    if (bnWorkStep(w, lastY)) continue;
    if (c.oneShot && bnAllClaimed()) break;
#ifdef HOST
    break;
#else
    vTaskDelay(1);
    lastY = micros();
#endif
  }
  c.endUs[w] = micros();
}

// mask: bit 0 çekirdek 0, bit 1 çekirdek 1
static bool bnSpawn(uint8_t mask) {
  B->quit = false;
  B->spawnUs = micros();
  for (int w = 0; w < 2; w++) {
    B->pid[w] = -1;
    B->startUs[w] = B->endUs[w] = 0;
  }
  for (int w = 0; w < 2; w++) {
    if (!(mask & (1 << w))) continue;
#ifndef HOST
    B->pid[w] = kSpawn(w ? "kiyas-1" : "kiyas-0", bnWorker, BN_STACK, true, K_PROC_PRIO, w);
    if (B->pid[w] < 0) return false;
#else
    if (B->oneShot) {  // önizleme: görev yok, eşzamanlı çalışır
      Proc p;
      p.name = w ? "kiyas-1" : "kiyas-0";
      bnWorker(&p);
    }
#endif
  }
  return true;
}
static bool bnWorkersAlive() {
  if (!B) return false;
  for (int w = 0; w < 2; w++)
    if (B->pid[w] >= 0 && kAlive(B->pid[w])) return true;
  return false;
}
// İşçileri durdurur; bitmeyen kalırsa false (bellek bırakılmaz, sonra yeniden denenir)
static bool bnStopWorkers() {
  if (!B) return true;
  if (bnWorkersAlive()) {
    B->quit = true;
    for (int w = 0; w < 2; w++)
      if (B->pid[w] >= 0) kWait(B->pid[w], 1500);
    if (bnWorkersAlive()) return false;
  }
  B->pid[0] = B->pid[1] = -1;
  B->quit = false;
  return true;
}

// ============================================================================
// Kare hattı
// ============================================================================
static void bnMakeJob(BnJob& j, const BnStep& s, uint32_t gen) {
  bool ex = bnExtreme();
  const BnTarget& t = BN_TARGETS[s.target];
  j.gen = gen;
  j.step = ex ? 1 : 2;
  j.geo = BnFrameGeo{t.x, t.y, s.span, BN_W / j.step, bnVh() / j.step};
  j.arith = s.arith == BA_END ? BA_DOUBLE : s.arith;
  j.maxIt = s.maxIt;
  j.brow = B->bh / j.step;
  j.nBands = (bnVh() + B->bh - 1) / B->bh;
  j.interior = true;
  j.discard = false;
  j.off = (uint8_t)(s.k * 3);  // renk akışı: yolun karesine bağlı (durgun görüntü de aynı renkte)
  j.path = s;
}
static void bnPipeReset(const BnStep& s) {
  BnCtx& c = *B;
  for (auto& sl : c.slot) sl.st = SL_FREE;
  c.cg = c.pg = 0;
  c.next = 0;
  c.pendOk = false;
  c.pushNext = 0;
  c.acc = BnRowOut();
  c.accComp = c.accXfer = 0;
  c.flat[0] = c.flat[1] = false;
  bnMakeJob(c.job[0], s, 0);
}

static bool bnCtxNew() {
  if (B) return true;
  B = new (std::nothrow) BnCtx();
  return B != nullptr;
}
// Hesap bitti: işçiler durur, ara bellekler geri verilir (bağlam açık kalır)
static void bnRelease() {
  if (!B) return;
  if (!bnStopWorkers()) return;
  for (int i = 0; i < B->nBall; i++) free(B->ball[i]);
  B->nBall = 0;
  bnFreeSlots();
  netHold &= ~HOLD_BENCH;
}

// Ekran aktarımı. clip: bildirim balonu altta açıkken üstüne yazılmaz.
static uint32_t bnPushBand(const BnSlot& sl, int y, int rows) {
  uint32_t t0 = micros();
  tft.setSwapBytes(false);  // renk tablosu zaten ekranın bayt sırasında
  tft.pushImage(0, y, BN_W, rows, sl.px);
  tft.setSwapBytes(true);
  return micros() - t0;
}

static void bnFrameDone();
// Hazır bantları sırasıyla yollar (en çok budget µs)
static bool bnPushReady(uint32_t budget) {
  BnCtx& c = *B;
  bool any = false;
  uint32_t t0 = micros();
  int clipY = toastUntil ? 176 : SCR_H;
  while (micros() - t0 <= budget || !any) {
    int s = -1;
    BN_LOCK();
    for (int i = 0; i < c.nSlot; i++)
      if (c.slot[i].st == SL_READY && c.slot[i].gen == c.pg && c.slot[i].band == c.pushNext) s = i;
    BN_UNLOCK();
    if (s < 0) break;
    BnSlot& sl = c.slot[s];
    const BnJob& j = c.job[c.pg & 1];
    int y = BN_FY + sl.band * c.bh, rows = std::min(c.bh, BN_FY + bnVh() - y);
    if (y + rows > clipY) {
      rows = std::max(0, clipY - y);
      c.r.clipped++;
    }
    if (rows > 0) c.accXfer += bnPushBand(sl, y, rows);
    c.acc.add(sl.res);
    c.accComp += sl.us;
    BN_LOCK();
    sl.st = SL_FREE;
    BN_UNLOCK();
    c.pushNext++;
    any = true;
    if (c.pushNext >= j.nBands) {
      bnFrameDone();
      BN_LOCK();
      c.pg = c.pg + 1;
      c.pushNext = 0;
      BN_UNLOCK();
      break;  // kare başına bir tur (önizleme de)
    }
  }
  return any;
}

static void bnFinish(uint8_t why);
static void bnFrameDone() {
  BnCtx& c = *B;
  const BnJob& j = c.job[c.pg & 1];
  bnView = j.path;
  bnHaveFrame = true;
  c.flat[c.pg & 1] = bnFlat(c.acc);
  c.flatPath[c.pg & 1] = j.path;
  if (bnSt == BS_RUN) {
    BnRun& r = c.r;
    uint32_t now = micros();
    r.ft.add(now - r.lastFrame);
    r.lastFrame = now;
    r.compUs += c.accComp;
    r.xferUs += c.accXfer;
    r.pxComp += (uint64_t)j.geo.cols * j.geo.rows;
    r.pxScreen += (uint64_t)BN_W * bnVh();
    r.iters += c.acc.iters;
    r.last = j.path;
    if (j.path.span < r.minSpan) r.minSpan = j.path.span, r.deepest = j.arith;
    if (j.path.k == 0 && r.ft.n > 1) r.targets++;
    if (!r.itLo || j.maxIt < r.itLo) r.itLo = j.maxIt;
    if (j.maxIt > r.itHi) r.itHi = j.maxIt;
  }
  c.acc = BnRowOut();
  c.accComp = c.accXfer = 0;
}

// Sonraki kare (arayüz hazırlar; işçiler bir öncekini bitirince başlar). Yol zamanlamadan bağımsızdır:
// n. karenin hedefi, n−2. karenin tek renk olup olmadığına göre seçilir (hat hangi hızda akarsa aksın
// aynı kare dizisi çıkar).
static BnStep bnPathFor(uint32_t n, const BnStep& prev) {
  BnCtx& c = *B;
  bool flat = n >= 2 && c.flat[n & 1] && c.flatPath[n & 1].target == prev.target;
  return bnNext(bnPathCfg(), prev, flat);
}
static void bnPrepNext() {
  BnCtx& c = *B;
  if (bnSt != BS_RUN || c.pendOk) return;
  if (c.cg == c.pg) {  // işçiler bu karede: sonrakini hazırla, bitirince geçsinler
    uint32_t n = c.cg + 1;
    bnMakeJob(c.job[n & 1], bnPathFor(n, c.job[c.cg & 1].path), n);
    BN_LOCK();
    c.pendOk = true;
    BN_UNLOCK();
  } else if (c.cg + 1 == c.pg) {  // kare bitti, sonraki hazır değildi: hat burada yeniden başlar
    uint32_t n = c.pg;
    bnMakeJob(c.job[n & 1], bnPathFor(n, c.job[c.cg & 1].path), n);
    BN_LOCK();
    c.cg = n;
    c.next = 0;
    BN_UNLOCK();
  }
}

// ============================================================================
// Başlat / durdur
// ============================================================================
static void bnDrawStrip(bool full);
static void bnDrawLive(bool full);
static void bnShowResult();

// Durgun görüntü: geçerli yerin bir karesi (açılışta, pencere kapanınca)
static void bnStill() {
  bnWantStill = false;
  if (bnSt != BS_IDLE || bnPage != BP_LIVE) return;
  if (!bnCtxNew()) return;
  bnErr = "";
  if (!bnAllocSlots(&bnErr)) {
    bnErrAt = millis();
    bnWantStill = true;  // birkaç saniye sonra yeniden denenir
    bnDrawLive(false);
    return;
  }
  bnBuildLut(B->lut, bnCfg.pal, true);
  BnPathCfg pc = bnPathCfg();
  BnStep s = bnStepAt(pc, bnView.target, bnView.k);
  if (s.arith == BA_END) s = bnStepAt(pc, bnView.target, 0);
  bnPipeReset(s);
  B->oneShot = false;
  bnSt = BS_STILL;
#ifndef HOST
  if (!bnSpawn(B->wmax == 2 ? 3 : 2)) {
    bnSt = BS_IDLE;
    bnRelease();
  }
#endif
}

static void bnStart() {
  if (bnSt == BS_STILL) bnRelease();
  bnSt = BS_IDLE;
  if (!bnCtxNew()) {
    toast("Bellek yetersiz");
    return;
  }
  bnErr = "";
  if (!bnAllocSlots(&bnErr)) {
    alertBox("Başlatılamadı", bnErr);
    return;
  }
  netHold |= HOLD_BENCH;  // ölçüm sırasında bulut akışı yok (bellek ve çekirdek 0 serbest)
  bnBuildLut(B->lut, bnCfg.pal, true);
  BnCtx& c = *B;
  c.r = BnRun();
  for (int w = 0; w < 2; w++) c.busyUs[w] = c.bands[w] = 0, c.iters[w] = 0, c.beat[w] = 0;
  c.ballAllocs = c.ballFails = c.ballMaxUs = 0;
  c.ballUs = 0;
  bnPipeReset(bnStepAt(bnPathCfg(), 0, 0));
  c.oneShot = false;
  BnRun& r = c.r;
  r.t0 = millis();
  r.lastTick = r.lastMon = r.winT = r.beatAt = r.t0;
  r.lastFrame = micros();
  r.freeStart = sysHeapFree();
  bnSt = BS_RUN;
#ifndef HOST
  if (!bnSpawn(c.wmax == 2 ? 3 : 2)) {
    bnFinish(BST_TASK);
    return;
  }
#endif
  bnDrawStrip(true);
}

static void bnStopRun(uint8_t why) {
  if (bnSt == BS_RUN) bnFinish(why);
}

// ============================================================================
// Ölçümler: denetim (her 250 ms) ve bellek baskısı
// ============================================================================
static void bnMemSample(BnRun& r) {
  uint32_t fr = sysHeapFree(), mb = sysHeapMaxAlloc();
  if (fr < r.minFree) r.minFree = fr;
  if (mb < r.minBlk) r.minBlk = mb;
  uint32_t fp = bnFragPct(fr, mb);
  if (fp > r.maxFrag) r.maxFrag = fp;
}

// Aşırı yük: boş bellek tabana 12 KB kalana dek 1 KB'lık bloklar tutulur; her denetimde ikisi bırakılıp
// yeniden alınır (ayırıcı sürekli çalışır). Boş bellek tabana 6 KB yaklaşırsa bloklar hemen bırakılır.
static void bnBallast() {
  BnCtx& c = *B;
  uint32_t fr = sysHeapFree();
  if (fr < MEM_FLOOR + 6 * 1024) {
    for (int k = 0; k < 8 && c.nBall; k++) free(c.ball[--c.nBall]);
    return;
  }
  for (int k = 0; k < 2 && c.nBall; k++) {
    int i = (c.ballAllocs + k * 7) % c.nBall;
    free(c.ball[i]);
    c.ball[i] = c.ball[--c.nBall];
  }
  for (int k = 0; k < 6 && c.nBall < BN_BALLN && sysHeapFree() > MEM_FLOOR + 12 * 1024 + 1024; k++) {
    uint32_t t0 = micros();
    void* p = malloc(1024);
    uint32_t us = micros() - t0;
    c.ballAllocs++;
    if (!p) {
      c.ballFails++;
      break;
    }
    memset(p, 0x5A, 1024);  // gerçekten dokunulan bellek
    c.ballUs += us;
    if (us > c.ballMaxUs) c.ballMaxUs = us;
    c.ball[c.nBall++] = p;
#ifdef HOST
    if (c.nBall >= 8) break;  // önizlemede boş bellek sabit görünür
#endif
  }
}

static void bnMonitor() {
  BnCtx& c = *B;
  BnRun& r = c.r;
  uint32_t ms = millis();
  if (ms - r.lastMon < 250) return;
  r.lastMon = ms;
  bnMemSample(r);
  if (bnExtreme()) bnBallast();
  uint32_t fr = sysHeapFree();
  if (fr < MEM_FLOOR + 2 * 1024) {
    bnFinish(BST_MEM);
    return;
  }
#ifndef HOST
  for (int w = 0; w < 2; w++)
    if (B->pid[w] >= 0 && !kAlive(B->pid[w])) {
      bnFinish(BST_TASK);
      return;
    }
#endif
  bool moved = false;
  for (int w = 0; w < 2; w++)
    if (c.beat[w] != r.beatSeen[w]) moved = true, r.beatSeen[w] = c.beat[w];
  if (moved) r.beatAt = ms;
  else if (ms - r.beatAt > 20000) {
    bnFinish(BST_STALL);
    return;
  }
  if (ms - r.winT >= 1000) {  // 1 sn pencerede en yüksek iterasyon hızı
    uint64_t it = c.iters[0] + c.iters[1];
    float mits = (it - r.winIters) / 1000.f / (ms - r.winT);
    if (mits > r.maxMits) r.maxMits = mits;
    r.winIters = it;
    r.winT = ms;
  }
  uint32_t dur = BN_DURS[bnCfg.dur];
  if (dur && ms - r.t0 - r.pausedMs >= dur * 1000UL) bnFinish(BST_TIME);
}

// ============================================================================
// Rapor
// ============================================================================
static void bnRepH(const char* t) { bnRep.push_back(String("#") + t); }
// "etiket\tdeğer"; değer printf biçiminde, sayılarda ondalık virgül
static void bnKV(const char* k, const char* fmt, ...) {
  char b[120];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  for (char* p = b + 1; *p; p++)
    if (*p == '.' && p[-1] >= '0' && p[-1] <= '9' && p[1] >= '0' && p[1] <= '9') *p = ',';
  bnRep.push_back(String(k) + "\t" + b);
}
static void bnRepCommon() {
  bnKV("Kart", "%s", sysChipText().c_str());
  bnKV("İşlemci saati", "%lu MHz (ö)", (unsigned long)ESP.getCpuFreqMHz());
  uint32_t ps = bnPsram();
  if (ps) bnKV("PSRAM", "%.0f KB (ö)", ps / 1024.f);
  else bnKV("PSRAM", "yok (ö: 0 bayt)");
  bnKV("Yazılım", "%s · yapı %d", FW_VARIANT, (int)FW_BUILD);
}

static String bnSaveRep() {
  if (!sdOk || bnRep.empty()) return "";
  if (!SD.exists(BN_DIR)) SD.mkdir(BN_DIR);
  struct tm t;
  char nm[64];
  if (localNow(t))
    snprintf(nm, sizeof nm, BN_DIR "/%04d%02d%02d-%02d%02d%02d.txt", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
             t.tm_sec);
  else
    snprintf(nm, sizeof nm, BN_DIR "/kiyas-%lu.txt", (unsigned long)(millis() / 1000));
  File f = SD.open(nm, FILE_WRITE);
  if (!f) return "";
  f.print("CYD INFINITY ENGINE · ");
  f.println(bnRepTitle);
  f.println("(ö: ölçüldü · h: ölçülenden hesaplandı · k: kuramsal; yöntem: docs/benchmark.md)");
  for (auto& l : bnRep) {
    if (l.startsWith("#")) {
      f.println("\n== " + l.substring(1) + " ==");
      continue;
    }
    int tab = l.indexOf('\t');
    f.println(tab < 0 ? l : l.substring(0, tab) + ": " + l.substring(tab + 1));
  }
  f.close();
  return String(nm);
}

static void bnSerialRep() {
#ifndef HOST
  Serial.println("[kiyas] " + bnRepTitle);
  for (auto& l : bnRep) {
    String s = l;
    s.replace("\t", ": ");
    Serial.println("[kiyas] " + s);
  }
#endif
}

static const char* bnStopText(uint8_t w) {
  switch (w) {
    case BST_USER: return "kullanıcı durdurdu";
    case BST_TIME: return "süre doldu, sorunsuz";
    case BST_MEM: return "güvenlik: bellek kritik";
    case BST_TASK: return "güvenlik: işçi görev bitti";
    case BST_STALL: return "güvenlik: 20 sn ilerleme yok";
    case BST_ALLOC: return "bellek ayrılamadı";
    case BST_CLOSE: return "uygulama kapandı";
  }
  return "-";
}

static void bnRunReport() {
  BnCtx& c = *B;
  BnRun& r = c.r;
  float sec = std::max<uint32_t>(1, millis() - r.t0 - r.pausedMs) / 1000.f, n = std::max<uint32_t>(1, r.ft.n);
  bool ex = bnExtreme();
  bnRep.clear();
  bnRepTitle = "Fraktal testi · " + String(BN_MODE[bnCfg.mode]);
  bnRepH("Fraktal testi");
  bnKV("Mod", "%s", BN_MODE[bnCfg.mode]);
  bnKV("Ayarlar", "%s karmaşıklık · %s yakınlaşma · %s · %s", BN_CPLX[bnCfg.cplx], BN_ZOOMN[bnCfg.zoom],
       BA_NAME[bnCfg.arith ? BA_FIXED : BA_FLOAT], BN_VIEWN[bnCfg.view & 1]);
  bnKV("Çözünürlük", "%d×%d nokta → %d×%d ekran", ex ? BN_W : BN_W / 2, ex ? bnVh() : bnVh() / 2, BN_W, bnVh());
  bnKV("Süre", "%.1f sn (ö) · %lu duraklama düşüldü", sec, (unsigned long)r.pauses);
  bnKV("Tamamlanan kare", "%lu (ö)", (unsigned long)r.ft.n);
  bnKV("FPS ort / en az / en çok", "%.2f / %.2f / %.2f (ö)", r.ft.n / sec, r.ft.fpsMin(), r.ft.fpsMax());
  bnKV("Kare süresi ort", "%.1f ms (ö)", r.ft.msAvg());
  bnKV("Hesap / kare", "%.1f ms (ö, işçilerin toplamı)", r.compUs / 1000.f / n);
  bnKV("Ekran aktarımı / kare", "%.2f ms (ö)", r.xferUs / 1000.f / n);
  bnKV("Hesaplanan nokta", "%.3f Mpx/s (ö)", r.pxComp / 1e6f / sec);
  bnKV("Ekrana yazılan", "%.3f Mpx/s (ö)", r.pxScreen / 1e6f / sec);
  bnKV("İterasyon ort / en çok", "%.2f / %.2f Mit/s (ö; en çok: 1 sn pencere)", r.iters / 1e6f / sec, r.maxMits);
  uint32_t b0 = c.bands[0], b1 = c.bands[1], bt = std::max<uint32_t>(1, b0 + b1);
  bnKV("Bant payı çekirdek 0 / 1", "%lu %% / %lu %% (ö)", (unsigned long)(b0 * 100 / bt), (unsigned long)(b1 * 100 / bt));
  bnKV("İşçi", "%d görev · %d bant × %d satır", c.wmax, c.nSlot, c.bh);
  bnKV("En derin", "%s · %s", bnSpanX(r.minSpan).c_str(), BA_NAME[r.deepest < BA_N ? r.deepest : 0]);
  bnKV("Son hedef", "%s · %u hedef gezildi", BN_TARGETS[r.last.target].name, (unsigned)r.targets);
  bnKV("İterasyon sınırı", "%d → %d", r.itLo, r.itHi);
  bnRepH("Bellek");
  uint32_t mf = r.minFree == 0xFFFFFFFF ? r.freeStart : r.minFree, mb = r.minBlk == 0xFFFFFFFF ? sysHeapMaxAlloc() : r.minBlk;
  bnKV("Boş bellek başta / en az", "%.1f / %.1f KB (ö)", r.freeStart / 1024.f, mf / 1024.f);
  bnKV("En büyük blok en az", "%.1f KB (ö)", mb / 1024.f);
  bnKV("Parçalanma en çok", "%lu %% (h)", (unsigned long)r.maxFrag);
  if (ex) {
    uint32_t ok = std::max<uint32_t>(1, c.ballAllocs - c.ballFails);
    bnKV("Bellek baskısı", "%lu ayırma, %lu başarısız, ort %.1f µs, en çok %lu µs (ö)", (unsigned long)c.ballAllocs,
         (unsigned long)c.ballFails, (float)c.ballUs / ok, (unsigned long)c.ballMaxUs);
  }
  bnRepH("Kararlılık");
  bnKV("Sonuç", "%s", bnStopText(r.stop));
  bnKV("Görev bekçisi", "açık (işçiler en geç 25 ms'de bir boşa çıkar)");
  if (r.clipped) bnKV("Bildirim altında kalan bant", "%lu (aktarım ölçümü kısa)", (unsigned long)r.clipped);
  bnKV("Arayüz çizimi", "%.1f ms/sn (ö, ölçüm yükü)", r.uiUs / 1000.f / sec);
  bnRepCommon();
}

static void bnFinish(uint8_t why) {
  if (!B) return;
  bool wasRun = bnSt == BS_RUN;
  B->r.stop = why;
  bnSt = BS_IDLE;
  bnRelease();
  if (!wasRun) return;
  bnRunReport();
  bnRepPath = bnSaveRep();
  bnSerialRep();
  if (why != BST_CLOSE) bnShowResult();
}

// ============================================================================
// Testler: işlemci, ekran, bellek (adım adım; her turda en çok ~60 ms)
// ============================================================================
enum BnSuite : uint8_t { SU_CPU = 1, SU_TFT = 2, SU_RAM = 4 };
struct BnCpuRes {
  uint32_t us[3] = {0, 0, 0};  // çekirdek 1, çekirdek 0, ikisi
  uint64_t it[3] = {0, 0, 0};
  uint32_t imb = 0, lat = 0;   // iki çekirdekte bitiş farkı, görev başlama gecikmesi
};
struct BnWork {
  double cx, cy, span;
  int w, h, maxIt;
};
static const BnWork BN_CPU_WORK[BA_N] = {
    {-0.65, 0.0, 3.0, 320, 240, 256},  // float
    {-0.65, 0.0, 3.0, 320, 240, 256},  // sabit nokta
    {-0.65, 0.0, 3.0, 160, 120, 128},  // double (yazılımla; küçük yük)
};
struct BnSuiteSt {
  uint8_t todo = 0, cur = 0, phase = 0;
  int step = 0, i = 0;
  bool running = false;  // CPU adımı: işçiler çalışıyor
  BnCpuRes cpu[BA_N];
  float lockNs = 0;
  // ekran
  uint32_t spiHz = 0;
  uint64_t us = 0, px = 0;
  uint32_t ops = 0, lcg = 1, mn = 0, mx = 0;
  float fill = 0, fillFps = 0, rect = 0, rectN = 0, pix = 0, push = 0, part = 0, partUs = 0;
  // bellek
  uint32_t f0 = 0, b0 = 0, m0 = 0, blocks = 0, lim = 0, safe = 0, aMax = 0, bad = 0, after = 0, fb0 = 0, fb1 = 0, fb2 = 0;
  uint32_t lb1 = 0, lb2 = 0, fragN = 0, freeBlk0 = 0, freeBlk1 = 0;
  uint64_t aUs = 0, wUs = 0, rUs = 0, fUs = 0;
  bool memDone = false;
  void* p[32];
  int np = 0;
  uint16_t* buf = nullptr;
};
static BnSuiteSt* S = nullptr;

static uint32_t bnLcg(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return s >> 8;
}

static uint32_t bnFreeBlocks() {
#ifndef HOST
  multi_heap_info_t in;
  heap_caps_get_info(&in, MALLOC_CAP_8BIT);
  return in.free_blocks;
#else
  return 0;
#endif
}

static void bnDrawSuite(bool full);
static void bnSuiteReport();

static void bnSuiteStart(uint8_t todo) {
  bnStopRun(BST_USER);
  if (bnSt == BS_STILL) bnRelease();
  bnSt = BS_IDLE;
  if (!bnCtxNew()) return;
  delete S;
  S = new (std::nothrow) BnSuiteSt();
  if (!S) {
    toast("Bellek yetersiz");
    return;
  }
  netHold |= HOLD_BENCH;
  S->todo = todo;
  S->cur = (todo & SU_CPU) ? SU_CPU : (todo & SU_RAM) ? SU_RAM : SU_TFT;
  bnSt = BS_SUITE;
  bnPage = BP_SUITE;
  bnDrawSuite(true);
}

static void bnSuiteNext() {
  S->phase = 0, S->step = 0, S->i = 0;
  if (S->cur == SU_CPU && (S->todo & SU_RAM)) S->cur = SU_RAM;
  else if (S->cur != SU_TFT && (S->todo & SU_TFT)) S->cur = SU_TFT;
  else S->cur = 0;
  if (S->cur) bnDrawSuite(true);
}

// --- İşlemci: her aritmetik için çekirdek 1, çekirdek 0, ikisi; sabit yük, ekran durgun ---
static void bnCpuTick() {
  BnCtx& c = *B;
  int ar = S->step / 3, cfg = S->step % 3;
  if (S->step == 0 && S->phase == 0) {  // kilit maliyeti (portMUX giriş + çıkış)
#ifndef HOST
    static volatile uint32_t probe = 0;
    uint32_t t0 = micros();
    for (int i = 0; i < 20000; i++) {
      BN_LOCK();
      probe = probe + 1;
      BN_UNLOCK();
    }
    S->lockNs = (micros() - t0) * 1000.f / 20000;
#endif
    S->phase = 1;
  }
  if (S->step >= 3 * BA_N) {
    bnSuiteNext();
    return;
  }
  if (!S->running) {
    const BnWork& wk = BN_CPU_WORK[ar];
    for (auto& sl : c.slot) sl.st = SL_FREE;
    c.cg = c.pg = 0;
    c.next = 0;
    c.pendOk = false;
    BnJob& j = c.job[0];
    j.geo = BnFrameGeo{wk.cx, wk.cy, wk.span, wk.w, wk.h};
    j.arith = ar;
    j.step = 1;
    j.maxIt = wk.maxIt;
    j.brow = 8;
    j.nBands = (wk.h + 7) / 8;
    j.interior = false;  // saf iterasyon hızı: içerik testi yok
    j.discard = true;
    j.off = 0;
    for (int w = 0; w < 2; w++) c.iters[w] = 0, c.busyUs[w] = 0, c.bands[w] = 0;
    c.oneShot = true;
    bnDrawSuite(false);
    S->running = true;
    uint8_t mask = cfg == 0 ? 2 : cfg == 1 ? 1 : 3;
    if (cfg == 2 && c.wmax < 2) {  // bellek tek işçiye yetti: çift çekirdek ölçülmez (raporda yazar)
      S->running = false;
      S->step++;
      return;
    }
    if (!bnSpawn(mask)) {
      S->running = false;
      bnStopWorkers();
      S->step = 3 * BA_N;
    }
    return;
  }
  if (bnWorkersAlive()) return;  // ölçüm sürüyor (arayüz hiçbir şey çizmez)
  S->running = false;
  BnCpuRes& r = S->cpu[ar];
  uint32_t end = 0, beg = c.spawnUs;
  for (int w = 0; w < 2; w++)
    if (c.endUs[w]) end = std::max(end, c.endUs[w] - beg);
  r.us[cfg] = std::max<uint32_t>(end, 1);
  r.it[cfg] = c.iters[0] + c.iters[1];
  if (cfg == 2 && c.endUs[0] && c.endUs[1]) {
    r.imb = (uint32_t)abs((int32_t)(c.endUs[0] - c.endUs[1]));
    r.lat = std::max(c.startUs[0] - beg, c.startUs[1] - beg);
  }
  S->step++;
  bnDrawSuite(false);
}

// --- Ekran ---
static void bnTftTick() {
  uint32_t t0 = micros();
  uint16_t* buf = B->slot[0].px;  // 320 × bh
  int bpx = BN_W * B->bh;
  if (S->phase == 0) {  // hazırlık: degrade (ekran bayt sırasında)
    for (int i = 0; i < bpx; i++) {
      uint16_t c = rgbn((i % BN_W) * 255 / BN_W, (i / BN_W) * 30, 128);
      buf[i] = (uint16_t)(c << 8 | c >> 8);
    }
    S->lcg = 12345;
    S->us = S->px = 0, S->ops = 0, S->mn = 0xFFFFFFFF, S->mx = 0;
    S->phase = 1;
#ifndef HOST
    tft.fillScreen(C_BG);  // ilk işlem: SPI saati yazmaca girsin
    S->spiHz = spiClockDivToFrequency(TFT_eSPI::getSPIinstance().getClockDivider());
#endif
  }
  while (micros() - t0 < 60000) {
    uint32_t a = micros();
    switch (S->phase) {
      case 1:  // tam ekran doldurma ×10
        tft.fillScreen(S->i & 1 ? grayn(0x18) : grayn(0x30));
        break;
      case 2: {  // dikdörtgen ×300
        int w = 16 + bnLcg(S->lcg) % 81, h = 16 + bnLcg(S->lcg) % 81;
        int x = bnLcg(S->lcg) % (SCR_W - w), y = bnLcg(S->lcg) % (SCR_H - h);
        a = micros();
        tft.fillRect(x, y, w, h, (uint16_t)bnLcg(S->lcg));
        S->px += w * h;
        break;
      }
      case 3: {  // tek piksel ×20000 (200'lük gruplar)
        uint16_t xs[200], ys[200];
        for (int k = 0; k < 200; k++) xs[k] = bnLcg(S->lcg) % SCR_W, ys[k] = bnLcg(S->lcg) % SCR_H;
        a = micros();
        tft.startWrite();
        for (int k = 0; k < 200; k++) tft.drawPixel(xs[k], ys[k], BN_ACC);
        tft.endWrite();
        S->px += 200;
        break;
      }
      case 4: {  // görüntü aktarımı: 320×bh bantlarla tam ekran ×4
        int y = (S->i * B->bh) % SCR_H;
        tft.setSwapBytes(false);
        tft.pushImage(0, y, BN_W, std::min(B->bh, SCR_H - y), buf);
        tft.setSwapBytes(true);
        S->px += BN_W * std::min(B->bh, SCR_H - y);
        break;
      }
      case 5: {  // kısmi güncelleme: 64×(bpx/64) parça ×100
        int ph = bpx / 64, x = bnLcg(S->lcg) % (SCR_W - 64), y = bnLcg(S->lcg) % (SCR_H - ph);
        a = micros();
        tft.setSwapBytes(false);
        tft.pushImage(x, y, 64, ph, buf);
        tft.setSwapBytes(true);
        S->px += 64 * ph;
        break;
      }
    }
    uint32_t us = micros() - a;
    S->us += us;
    S->ops++;
    if (us < S->mn) S->mn = us;
    if (us > S->mx) S->mx = us;
    S->i++;
    static const int N[6] = {0, 10, 300, 100, 0, 100};
    int n = S->phase == 4 ? 4 * ((SCR_H + B->bh - 1) / B->bh) : N[S->phase];
    if (S->i >= n) {
      float sec = std::max<uint64_t>(S->us, 1) / 1e6f;
      switch (S->phase) {
        case 1:
          S->fill = (float)SCR_W * SCR_H * S->ops / 1e6f / sec;
          S->fillFps = S->ops / sec;
          break;
        case 2:
          S->rect = S->px / 1e6f / sec;
          S->rectN = S->ops / sec;
          break;
        case 3: S->pix = S->px / 1e6f / sec; break;
        case 4: S->push = S->px / 1e6f / sec; break;
        case 5:
          S->part = S->px / 1e6f / sec;
          S->partUs = (float)S->us / S->ops;
          break;
      }
      S->phase++;
      S->i = 0, S->us = 0, S->px = 0, S->ops = 0, S->mn = 0xFFFFFFFF, S->mx = 0;
      if (S->phase >= 6) {
        bnSuiteNext();
        shellRedraw(true);
        return;
      }
    }
  }
}

// --- Bellek: anlık görüntü, denetimli ayırma (yaz, doğrula, bırak), parçalanma, toparlanma ---
static void bnRamTick() {
  uint32_t t0 = micros();
  switch (S->phase) {
    case 0: {
      S->f0 = sysHeapFree(), S->b0 = sysHeapMaxAlloc(), S->m0 = sysHeapMin(), S->freeBlk0 = bnFreeBlocks();
      uint32_t floorB = MEM_FLOOR + 16 * 1024;
      S->safe = S->f0 > floorB ? S->f0 - floorB : 0;
      S->lim = std::min<uint32_t>(BN_MEMKB[bnCfg.mem] * 1024u, S->safe);
      S->lim = std::min<uint32_t>(S->lim, 32 * 4096);
      S->np = 0;
      S->phase = 1;
      break;
    }
    case 1:  // 4 KB'lık bloklar: sınıra ya da güvenli sınıra kadar
      while (micros() - t0 < 40000) {
        if ((uint32_t)(S->np + 1) * 4096 > S->lim || S->np >= 32 || sysHeapFree() < MEM_FLOOR + 16 * 1024 + 4096) {
          S->phase = 2;
          break;
        }
        uint32_t a = micros();
        void* p = malloc(4096);
        uint32_t us = micros() - a;
        if (!p) {
          S->phase = 2;
          break;
        }
        S->aUs += us;
        S->aMax = std::max(S->aMax, us);
        a = micros();
        memset(p, 0xA5 ^ S->np, 4096);
        S->wUs += micros() - a;
        S->p[S->np++] = p;
      }
      if (S->phase == 2) S->blocks = S->np, S->fb1 = sysHeapFree(), S->lb1 = sysHeapMaxAlloc();
      break;
    case 2: {  // doğrulama (32 bit okuma)
      uint32_t a = micros();
      for (int k = 0; k < S->np; k++) {
        uint8_t b = 0xA5 ^ k;
        uint32_t want = b * 0x01010101u;
        const volatile uint32_t* w = (const volatile uint32_t*)S->p[k];
        for (int i = 0; i < 1024; i++)
          if (w[i] != want) S->bad++;
      }
      S->rUs = micros() - a;
      a = micros();
      for (int k = 0; k < S->np; k++) free(S->p[k]);
      S->fUs = micros() - a;
      S->np = 0;
      S->after = sysHeapFree();
      S->phase = 3;
      break;
    }
    case 3: {  // parçalanma: 24 × 1 KB, tekler bırakılır
      bool ok = sysHeapFree() > MEM_FLOOR + 16 * 1024 + 24 * 1024;
      S->np = 0;
      for (int k = 0; ok && k < 24; k++) {
        void* p = malloc(1024);
        if (!p) break;
        S->p[S->np++] = p;
      }
      S->fragN = S->np;
      for (int k = 1; k < S->np; k += 2) free(S->p[k]), S->p[k] = nullptr;
      S->lb2 = sysHeapMaxAlloc();
      S->freeBlk1 = bnFreeBlocks();
      for (int k = 0; k < S->np; k += 2) free(S->p[k]);
      S->np = 0;
      S->fb2 = sysHeapFree();
      S->phase = 4;
      break;
    }
    default:
      S->memDone = true;
      bnSuiteNext();
      return;
  }
  bnDrawSuite(false);
}

static void bnSuiteReport() {
  bnRep.clear();
  uint8_t t = S->todo;
  bnRepTitle = t == (SU_CPU | SU_TFT | SU_RAM) ? "Tam test" : t == SU_CPU ? "İşlemci testi" : t == SU_TFT ? "Ekran testi" : "Bellek testi";
  if (t & SU_CPU) {
    bnRepH("İşlemci (Mandelbrot, sabit yük)");
    bnKV("Yük", "320×240 nokta, sınır 256 (float, sabit); double 160×120, sınır 128; (-0.65; 0), genişlik 3");
    static const char* const CFG[3] = {"çekirdek 1", "çekirdek 0", "iki çekirdek"};
    char k[40];
    for (int a = 0; a < BA_N; a++) {
      const BnCpuRes& r = S->cpu[a];
      for (int i = 0; i < 3; i++) {
        snprintf(k, sizeof k, "%s · %s", BA_NAME[a], CFG[i]);
        if (!r.us[i]) {
          if (i == 2 && r.us[0]) bnKV(k, "ölçülmedi (bellek tek işçiye yetti)");
          continue;
        }
        float ms = r.us[i] / 1000.f, mits = r.it[i] / (float)r.us[i];
        if (i == 2 && r.us[0]) {
          float sp = (float)r.us[0] / r.us[2];
          bnKV(k, "%.1f ms · %.2f Mit/s · ×%.2f, verim %d %% (ö)", ms, mits, sp, (int)lroundf(sp * 50));
        } else {
          bnKV(k, "%.1f ms · %.2f Mit/s (ö)", ms, mits);
        }
      }
      if (r.us[2] && r.lat) {
        snprintf(k, sizeof k, "%s · eşzamanlama", BA_NAME[a]);
        bnKV(k, "başlama %lu µs, bitiş farkı %.1f ms (ö)", (unsigned long)r.lat, r.imb / 1000.f);
      }
    }
    if (S->lockNs > 0) bnKV("Kilit (portMUX) giriş + çıkış", "%.0f ns (ö)", S->lockNs);
    const BnCpuRes &f = S->cpu[BA_FLOAT], &q = S->cpu[BA_FIXED], &d = S->cpu[BA_DOUBLE];
    if (f.us[0] && q.us[0]) bnKV("Sabit nokta / float", "×%.2f hız (h, aynı yük)", (float)f.us[0] / q.us[0]);
    if (f.us[0] && d.us[0]) bnKV("double / float", "×%.3f Mit/s (h)", (d.it[0] / (float)d.us[0]) / std::max(f.it[0] / (float)f.us[0], 1e-6f));
  }
  if (t & SU_RAM) {
    bnRepH("Bellek");
    bnKV("Boş / en büyük blok", "%.1f / %.1f KB (ö)", S->f0 / 1024.f, S->b0 / 1024.f);
    bnKV("Açılıştan beri en az boş", "%.1f KB (ö)", S->m0 / 1024.f);
    bnKV("Parçalanma", "%d %% (h) · %lu boş blok (ö)", bnFragPct(S->f0, S->b0), (unsigned long)S->freeBlk0);
    uint32_t ps = bnPsram();
    if (ps) bnKV("PSRAM", "%.0f KB (ö)", ps / 1024.f);
    else bnKV("PSRAM", "yok (ö)");
    bnKV("Ayırma sınırı", "%.0f KB (ayar %u KB; güvenli %.0f KB)", S->lim / 1024.f, (unsigned)BN_MEMKB[bnCfg.mem], S->safe / 1024.f);
    bnKV("Ayrılan", "%lu × 4 KB = %.0f KB · sonra boş %.1f KB (ö)", (unsigned long)S->blocks, S->blocks * 4.f, S->fb1 / 1024.f);
    if (S->blocks) {
      float mb = S->blocks * 4096 / 1048576.f;
      bnKV("Ayırma süresi ort / en çok", "%.1f / %lu µs (ö)", (float)S->aUs / S->blocks, (unsigned long)S->aMax);
      bnKV("Yazma / okuma", "%.1f / %.1f MB/s (ö)", mb * 1e6f / std::max<uint64_t>(S->wUs, 1), mb * 1e6f / std::max<uint64_t>(S->rUs, 1));
      bnKV("Doğrulama", "%lu hatalı sözcük (ö)", (unsigned long)S->bad);
      bnKV("Geri verme", "%.1f µs/blok · boş bellek farkı %ld bayt (ö)", (float)S->fUs / S->blocks, (long)((int32_t)(S->after - S->f0)));
    }
    if (S->fragN) {
      bnKV("Parçalama", "%lu × 1 KB, yarısı bırakıldı: en büyük blok %.1f KB, %lu boş blok (ö)", (unsigned long)S->fragN,
           S->lb2 / 1024.f, (unsigned long)S->freeBlk1);
      bnKV("Toparlanma", "boş %.1f KB (başta %.1f), en büyük blok %.1f KB (ö)", S->fb2 / 1024.f, S->f0 / 1024.f, sysHeapMaxAlloc() / 1024.f);
    } else {
      bnKV("Parçalama", "atlandı (güvenli pay yok)");
    }
  }
  if (t & SU_TFT) {
    bnRepH("Ekran");
    if (S->spiHz) {
      bnKV("SPI saati", "%.1f MHz (ö, yazmaçtan; istenen %lu MHz)", S->spiHz / 1e6f, (unsigned long)(BN_SPI_REQ / 1000000));
      bnKV("Kuramsal en çok", "%.2f Mpx/s (k: 16 bit/piksel, ek yük yok)", S->spiHz / 16e6f);
    }
    bnKV("Tam ekran doldurma", "%.2f Mpx/s · %.1f kare/s (ö)", S->fill, S->fillFps);
    bnKV("Dikdörtgen (300 adet)", "%.2f Mpx/s · %.0f adet/s (ö)", S->rect, S->rectN);
    bnKV("Tek piksel (20000)", "%.3f Mpx/s (ö)", S->pix);
    bnKV("Görüntü, 320 genişlik bant", "%.2f Mpx/s · %.2f MB/s (ö)", S->push, S->push * 2);
    if (S->spiHz) bnKV("Aktarım verimi", "%d %% (h)", (int)lroundf(S->push * 16e8f / S->spiHz));
    bnKV("Kısmi 64 genişlik parça", "%.2f Mpx/s · %.0f µs/adet (ö)", S->part, S->partUs);
    bnKV("DMA", "kullanılmıyor (gerekçe: docs/benchmark.md)");
  }
  bnRepCommon();
}

static void bnSuiteEnd(bool aborted, bool redraw = true) {
  if (!S) return;
  if (S->running) bnStopWorkers();
  for (int k = 0; k < S->np; k++) free(S->p[k]);
  S->np = 0;
  bnSt = BS_IDLE;
  bnRelease();
  if (!aborted) {
    bnSuiteReport();
    bnRepPath = bnSaveRep();
    bnSerialRep();
  }
  delete S;
  S = nullptr;
  if (!redraw) return;
  if (aborted) {
    bnPage = BP_LIVE;
    shellRedraw(true);
  } else {
    bnShowResult();
  }
}

static void bnSuiteTick() {
  if (!S) return;
  if (!S->cur) {
    bnSuiteEnd(false);
    return;
  }
  if (sysHeapFree() < MEM_FLOOR + 2 * 1024 && !S->running) {
    bnSuiteEnd(true);
    toast("Test durdu: bellek kritik");
    return;
  }
  // ara bellekler işlemci ve ekran testinde gerekir; bellek testi kendi ayırdığını ölçsün diye bırakılır
  if (S->cur == SU_RAM) {
    if (B->nSlot) bnFreeSlots();
  } else if (!B->nSlot) {
    String why;
    if (!bnAllocSlots(&why)) {
      bnSuiteEnd(true);
      alertBox("Test durdu", why);
      return;
    }
  }
  if (S->cur == SU_CPU) bnCpuTick();
  else if (S->cur == SU_RAM) bnRamTick();
  else bnTftTick();
}

// ============================================================================
// Çizim
// ============================================================================
// Metin, titremeden: 4 bitlik ara bellekte çizilip tek seferde gönderilir
static void bnText(const String& s, int x, int y, int w, int h, Font f, uint16_t fg, uint16_t bg, uint8_t datum) {
  if (!sprOk || w > SPR_W || h > SPR_H) {
    tft.fillRect(x, y, w, h, bg);
    int tx = datum == MR_DATUM ? x + w : datum == MC_DATUM ? x + w / 2 : x;
    text(s, tx, y + h / 2, f, fg, bg, datum);
    return;
  }
  sprPalN = 16;
  for (int i = 0; i < 16; i++) {
    sprPal[i] = i == 15 ? fg : fontBlend(i * 17, fg, bg);
    spr.setPaletteColor(i, sprPal[i]);
  }
  spr.fillRect(0, 0, w, h, 0);
  int tx = datum == MR_DATUM ? w : datum == MC_DATUM ? w / 2 : 0;
  fontDraw<TFT_eSprite, true>(spr, f, s, tx, h / 2, 15, 0, datum);
  spr.pushSprite(x, y, 0, 0, w, h);
}

static void bnBtn(int x, int y, int w, int h, const String& label, bool acc) {
  panel(x, y, w, h, acc ? grayn(0x10) : C_SURF, acc ? BN_ACC : C_LINE);
  text(fit(label, F_SM, w - 8), x + w / 2, y + h / 2 + 1, F_SM, acc ? BN_ACC : C_TEXT, acc ? grayn(0x10) : C_SURF, MC_DATUM);
}

static void bnHeader() {
  header(bnPage == BP_RESULT ? String("Sonuçlar") : bnPage == BP_SUITE ? String("Testler") : String("Infinity"));
  if (bnPage == BP_LIVE) {
    headerAction(0, I_MORE);
    headerAction(1, I_CHART);
  }
}

// printf biçimli kısa değer (ondalık virgül)
static String bnS(const char* fmt, ...) {
  char b[32];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  for (char* p = b + 1; *p; p++)
    if (*p == '.' && p[-1] >= '0' && p[-1] <= '9') *p = ',';
  return String(b);
}
static float bnSec(const BnRun& r) { return std::max<uint32_t>(1, millis() - r.t0 - r.pausedMs) / 1000.f; }

// Alt şerit değerleri (sayfa: 0 kare, 1 işlem, 2 bellek, 3 derinlik)
static void bnStripVals(int pg, String v[3]) {
  bool run = bnSt == BS_RUN && B;
  const BnRun* r = run ? &B->r : nullptr;
  float sec = r ? bnSec(*r) : 1;
  for (int k = 0; k < 3; k++) v[k] = "-";
  if (pg == 0 && r) {
    v[0] = bnS("%.1f", r->ft.fpsCur());
    v[1] = bnS("%.2f", r->ft.n / sec);
    if (r->ft.n) v[2] = bnS("%.0f ms", r->ft.last / 1000.f);
  } else if (pg == 1) {
    if (r) v[0] = bnS("%.2f", r->pxComp / 1e6f / sec), v[1] = bnS("%.1f", (B->iters[0] + B->iters[1]) / 1e6f / sec);
    v[2] = String(run ? B->job[B->pg & 1].maxIt : bnStepAt(bnPathCfg(), bnView.target, bnView.k).maxIt);
  } else if (pg == 2) {
    v[0] = bnS("%.1f KB", sysHeapFree() / 1024.f);
    v[1] = bnS("%.1f KB", (r && r->minFree != 0xFFFFFFFF ? r->minFree : sysHeapMin()) / 1024.f);
    v[2] = bnS("%.1f KB", sysHeapMaxAlloc() / 1024.f);
  } else if (pg == 3) {
    v[0] = r ? bnMSS((uint32_t)sec) : String(BN_DURN[bnCfg.dur]);
    v[1] = bnSpanX(bnView.span);
    v[2] = BA_NAME[bnView.arith < BA_N ? bnView.arith : 0];
  }
}
static const char* const BN_STRIP_L[4][3] = {
    {"FPS", "ort FPS", "kare"}, {"Mpx/s", "Mit/s", "iter"}, {"boş", "en az", "blok"}, {"süre", "büyütme", "sayı"}};
static const int BN_COLX[3] = {66, 130, 194};

static void bnDrawStrip(bool full) {
  uint32_t t0 = micros();
  int pg = (bnCfg.view & 1) ? 3 : bnStripPg;
  bool run = bnSt == BS_RUN;
  if (full) {
    fillBg(0, BN_SY, SCR_W, SCR_H - BN_SY);
    hline(0, BN_SY, SCR_W);
    bnBtn(4, BN_SY + 4, 56, 28, BN_MODE_S[bnCfg.mode & 1], bnExtreme());
    for (int k = 0; k < 3; k++) text(BN_STRIP_L[pg][k], BN_COLX[k], BN_SY + 3, F_XS, C_TEXT3, C_BG);
    if (!(bnCfg.view & 1))  // şerit sayfası (dokununca değişir)
      for (int k = 0; k < 4; k++) tft.fillRect(240 + k * 4, BN_SY + 7, 2, 2, k == pg ? BN_ACC : C_LINE);
    bnBtn(256, BN_SY + 4, 62, 28, run ? "DUR" : "BAŞLA", true);
  }
  String v[3];
  bnStripVals(pg, v);
  for (int k = 0; k < 3; k++) bnText(v[k], BN_COLX[k], BN_SY + 16, 62, 17, F_SM, k == 0 && run ? BN_ACC : C_TEXT, C_BG, ML_DATUM);
  if (B && run) B->r.uiUs += micros() - t0;
}

// Pano: 4 satır × 3 sütun
static void bnDrawPano(bool full) {
  if (!(bnCfg.view & 1)) return;
  uint32_t t0 = micros();
  int y0 = BN_FY + BN_VH[1];
  static const char* const L[12] = {"FPS", "ort", "aralık", "kare", "hesap", "aktarım",
                                    "Mpx/s", "Mit/s", "ekran", "boş", "en az", "blok"};
  if (full) {
    fillBg(0, y0, SCR_W, BN_SY - y0);
    for (int i = 0; i < 12; i++) text(L[i], 4 + (i % 3) * 106, y0 + 9 + (i / 3) * 15, F_XS, C_TEXT3, C_BG, ML_DATUM);
    for (int k = 1; k < 3; k++) tft.drawFastVLine(k * 106 + 1, y0 + 3, 58, C_LINE);
  }
  bool run = bnSt == BS_RUN && B;
  const BnRun* r = run ? &B->r : nullptr;
  String v[12];
  for (auto& x : v) x = "-";
  if (r) {
    float sec = bnSec(*r), n = std::max<uint32_t>(1, r->ft.n);
    v[0] = bnS("%.1f", r->ft.fpsCur());
    v[1] = bnS("%.2f", r->ft.n / sec);
    v[2] = bnS("%.1f-%.1f", r->ft.fpsMin(), r->ft.fpsMax());
    if (r->ft.n) {
      v[3] = bnS("%.0f ms", r->ft.last / 1000.f);
      v[4] = bnS("%.0f ms", r->compUs / 1000.f / n);
      v[5] = bnS("%.1f ms", r->xferUs / 1000.f / n);
    }
    v[6] = bnS("%.2f", r->pxComp / 1e6f / sec);
    v[7] = bnS("%.1f", (B->iters[0] + B->iters[1]) / 1e6f / sec);
    v[8] = bnS("%.2f", r->pxScreen / 1e6f / sec);
  }
  uint32_t fr = sysHeapFree(), mb = sysHeapMaxAlloc();
  v[9] = bnS("%.1fK", fr / 1024.f);
  v[10] = bnS("%.1fK", (r && r->minFree != 0xFFFFFFFF ? r->minFree : sysHeapMin()) / 1024.f);
  v[11] = bnS("%luK·%%%d", (unsigned long)(mb / 1024), bnFragPct(fr, mb));
  for (int i = 0; i < 12; i++) bnText(v[i], 46 + (i % 3) * 106, y0 + 2 + (i / 3) * 15, 58, 14, F_XS, C_TEXT, C_BG, MR_DATUM);
  if (run) B->r.uiUs += micros() - t0;
}

static void bnDrawLive(bool full) {
  int vh = bnVh();
  if (full) {
    if (!bnHaveFrame || bnSt == BS_IDLE) tft.fillRect(0, BN_FY, SCR_W, vh, C_BG);
    if (bnSt == BS_IDLE) bnWantStill = true;  // üstü örtülmüş olabilir: yeniden çizilir
  }
  if (bnErr.length() && bnSt == BS_IDLE) {
    tft.fillRect(0, BN_FY, SCR_W, vh, C_BG);
    auto ls = wrapText(bnErr, F_SM, 280, 3);
    for (size_t i = 0; i < ls.size(); i++) text(ls[i], 160, BN_FY + vh / 2 - 20 + i * 19, F_SM, C_TEXT2, C_BG, TC_DATUM);
  }
  bnDrawPano(full);
  bnDrawStrip(full);
}

// Test ekranı: ilerleme ve o ana dek çıkanlar
static void bnDrawSuite(bool full) {
  if (bnPage != BP_SUITE || !S) return;
  if (full) {
    bnHeader();
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  const char* nm = S->cur == SU_CPU ? "İşlemci testi" : S->cur == SU_RAM ? "Bellek testi" : S->cur == SU_TFT ? "Ekran testi" : "Bitti";
  bnText(nm, 12, 40, 200, 22, F_MD, C_TEXT, C_BG, ML_DATUM);
  int pct = 0;
  String line;
  if (S->cur == SU_CPU) {
    pct = S->step * 100 / (3 * BA_N);
    static const char* const CFG[3] = {"çekirdek 1", "çekirdek 0", "iki çekirdek"};
    int a = std::min(S->step / 3, BA_N - 1);
    line = String(BA_NAME[a]) + " · " + CFG[S->step % 3] + (S->running ? " · ölçülüyor" : "");
  } else if (S->cur == SU_RAM) {
    pct = S->phase * 25;
    line = S->phase <= 1 ? "4 KB bloklar ayrılıyor" : S->phase == 2 ? "doğrulanıyor" : "parçalanma";
  }
  bnText(line, 12, 66, 296, 18, F_SM, C_TEXT2, C_BG, ML_DATUM);
  progress(12, 90, 296, 3, pct, BN_ACC);
  if (S->todo & SU_CPU) {  // işlemci sonuçları geldikçe: Mit/s tablosu
    static const char* const CH[3] = {"çek. 1", "çek. 0", "ikisi"};
    if (full)
      for (int k = 0; k < 3; k++) text(CH[k], 150 + k * 60, 112, F_XS, C_TEXT3, C_BG, MC_DATUM);
    for (int a = 0; a < BA_N; a++) {
      int y = 124 + a * 20;
      if (full) text(BA_NAME[a], 12, y + 8, F_XS, C_TEXT2, C_BG, ML_DATUM);
      for (int k = 0; k < 3; k++) {
        const BnCpuRes& r = S->cpu[a];
        bool cur = S->cur == SU_CPU && S->step == a * 3 + k;
        bnText(r.us[k] ? bnS("%.2f", r.it[k] / (float)r.us[k]) : cur ? String("…") : String("-"), 122 + k * 60, y, 56, 16, F_SM,
               k == 2 && r.us[k] ? BN_ACC : C_TEXT, C_BG, MC_DATUM);
      }
    }
    if (full) text("Mit/s: saniyede milyon iterasyon (ölçülen)", 12, 190, F_XS, C_TEXT3, C_BG, ML_DATUM);
  }
  if (full) {
    hline(0, BN_SY, SCR_W);
    bnBtn(110, BN_SY + 4, 100, 28, "Durdur", false);
  }
}

// Sonuç satırları: sığmayan değer ikinci satıra iner (sağa yaslı); sayfa başları önceden hesaplanır
static const int BN_RROWS = 10;
static std::vector<uint16_t> bnPgAt;
static void bnRowSplit(const String& l, String& k, String& v) {
  int tab = l.indexOf('\t');
  k = tab < 0 ? l : l.substring(0, tab);
  v = tab < 0 ? String("") : l.substring(tab + 1);
}
static int bnRowsOf(const String& l) {
  if (l.startsWith("#")) return 1;
  String k, v;
  bnRowSplit(l, k, v);
  return textW(k, F_XS) + textW(v, F_XS) + 12 > 304 ? 2 : 1;
}
static void bnPaginate() {
  bnPgAt.assign(1, 0);
  int rows = 0;
  for (size_t i = 0; i < bnRep.size(); i++) {
    int r = bnRowsOf(bnRep[i]);
    bool head = bnRep[i].startsWith("#");
    if (rows + r + (head ? 1 : 0) > BN_RROWS && rows) {  // başlık sayfanın sonunda kalmasın
      bnPgAt.push_back(i);
      rows = 0;
    }
    rows += r;
  }
}
static int bnResPages() { return std::max<int>(1, bnPgAt.size()); }
static void bnDrawResult() {
  bnHeader();
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  int n = bnRep.size(), pg = std::min<int>(bnResPg, bnResPages() - 1);
  int first = bnPgAt.empty() ? 0 : bnPgAt[pg], end = pg + 1 < (int)bnPgAt.size() ? bnPgAt[pg + 1] : n;
  for (int i = first, row = 0; i < end && row < BN_RROWS; i++) {
    const String& l = bnRep[i];
    int y = HDR_H + 4 + row * 17 + 8;
    if (l.startsWith("#")) {
      String h = l.substring(1);
      text(h, 8, y, F_XS, BN_ACC, C_BG, ML_DATUM);
      hline(8 + textW(h, F_XS) + 6, y, 300 - textW(h, F_XS));
      row++;
      continue;
    }
    String k, v;
    bnRowSplit(l, k, v);
    text(fit(k, F_XS, 296), 8, y, F_XS, C_TEXT2, C_BG, ML_DATUM);
    if (bnRowsOf(l) == 2) y += 17, row++;
    text(fit(v, F_XS, bnRowsOf(l) == 2 ? 300 : 300 - textW(k, F_XS) - 12), 312, y, F_XS, C_TEXT, C_BG, MR_DATUM);
    row++;
  }
  if (!n) text("Henüz sonuç yok", 160, 110, F_SM, C_TEXT3, C_BG, TC_DATUM);
  hline(0, BN_SY, SCR_W);
  bnBtn(4, BN_SY + 4, 80, 28, "Kapat", false);
  bnBtn(90, BN_SY + 4, 110, 28, bnRepPath.length() ? "Raporu aç" : "Kaydedilmedi", bnRepPath.length() > 0);
  text(String(pg + 1) + "/" + String(bnResPages()), 228, BN_SY + 18, F_XS, C_TEXT3, C_BG, MC_DATUM);
  bnBtn(252, BN_SY + 4, 30, 28, "<", false);
  bnBtn(286, BN_SY + 4, 30, 28, ">", false);
}

static void bnShowResult() {
  bnPage = BP_RESULT;
  bnResPg = 0;
  bnPaginate();
  if (shScreen == SH_APP && curApp && curApp->draw && !ov.kind) bnDrawResult();
}

static void bnDraw(bool full) {
  bnLoad();
  if (bnPage == BP_RESULT) {
    bnDrawResult();
    return;
  }
  if (bnPage == BP_SUITE) {
    bnDrawSuite(true);
    return;
  }
  if (full) bnHeader();
  bnDrawLive(full);
}

// ============================================================================
// Menüler
// ============================================================================
static void bnRestartStill() {
  bnHaveFrame = false;
  if (bnSt == BS_STILL) {
    bnSt = BS_IDLE;
    bnRelease();
  }
  shellRedraw(true);
}
static void bnPick(const String& title, const char* const* names, int n, uint8_t* v, void (*after)()) {
  std::vector<String> o;
  for (int i = 0; i < n; i++) o.push_back(names[i]);
  menu(title, o, [v, after](int c) {
    if (c < 0) return;
    *v = c;
    bnSave();
    if (after) after();
    else shellRedraw(true);
  }, *v);
}
static void bnPalAfter() {
  if (B) bnBuildLut(B->lut, bnCfg.pal, true);
  bnRestartStill();
}
static void bnAdvancedMenu() {
  std::vector<String> o = {"Aritmetik: " + String(bnCfg.arith ? "sabit nokta" : "float"),
                           "Bellek testi sınırı: " + String(BN_MEMKB[bnCfg.mem]) + " KB", "Varsayılanlara dön"};
  menu("Gelişmiş", o, [](int c) {
    if (c == 0) {
      static const char* const A[2] = {"float (donanım)", "sabit nokta Q4.28"};
      bnPick("Sığ katman aritmetiği", A, 2, &bnCfg.arith, bnRestartStill);
    } else if (c == 1) {
      static const char* const M[3] = {"32 KB", "64 KB", "96 KB"};
      bnPick("Bellek testi sınırı", M, 3, &bnCfg.mem, nullptr);
    } else if (c == 2) {
      bnCfg = BnCfg();
      bnSave();
      bnPalAfter();
    }
  });
}
static void bnSettingsMenu() {
  std::vector<String> o = {"Karmaşıklık: " + String(BN_CPLX[bnCfg.cplx]), "Yakınlaşma: " + String(BN_ZOOMN[bnCfg.zoom]),
                           "Palet: " + String(BN_PALS[bnCfg.pal].name), "Süre: " + String(BN_DURN[bnCfg.dur]), "Gelişmiş…"};
  menu("Ayarlar", o, [](int c) {
    static const char* PN[BN_NPAL];
    for (int i = 0; i < BN_NPAL; i++) PN[i] = BN_PALS[i].name;
    if (c == 0) bnPick("Karmaşıklık (iterasyon sınırı)", BN_CPLX, 4, &bnCfg.cplx, bnRestartStill);
    else if (c == 1) bnPick("Yakınlaşma hızı", BN_ZOOMN, 3, &bnCfg.zoom, nullptr);
    else if (c == 2) bnPick("Palet", PN, BN_NPAL, &bnCfg.pal, bnPalAfter);
    else if (c == 3) bnPick("Test süresi", BN_DURN, 4, &bnCfg.dur, nullptr);
    else if (c == 4) bnAdvancedMenu();
  });
}
static void bnTestsMenu() {
  menu("Testler", {"Son sonuçlar", "İşlemci testi", "Ekran testi", "Bellek testi", "Tam test (üçü)", "Görüntüyü sıfırla"},
       [](int c) {
         if (c == 0) {
           if (bnRep.empty() && bnRepPath.length()) openInReader(bnRepPath);
           else bnShowResult();
         } else if (c == 1) bnSuiteStart(SU_CPU);
         else if (c == 2) bnSuiteStart(SU_TFT);
         else if (c == 3) bnSuiteStart(SU_RAM);
         else if (c == 4) bnSuiteStart(SU_CPU | SU_TFT | SU_RAM);
         else if (c == 5) {
           bnView = BnStep();
           bnRestartStill();
         }
       });
}

// ============================================================================
// Kabuk kancaları
// ============================================================================
static void bnTap(int x, int y) {
  if (y < HDR_H) {
    if (bnPage != BP_LIVE) return;
    int h = headerHit(x, y);
    if (h < 0) return;
    if (bnSt == BS_RUN) bnStopRun(BST_USER);  // ayar değişirse ölçüm geçersiz: önce biter
    if (bnPage != BP_LIVE) return;
    if (h == 0) bnSettingsMenu();
    else if (h == 1) bnTestsMenu();
    return;
  }
  if (bnPage == BP_RESULT) {
    if (y < BN_SY) return;
    if (x < 86) {
      bnPage = BP_LIVE;
      shellRedraw(true);
    } else if (x < 204) {
      if (bnRepPath.length()) openInReader(bnRepPath);
    } else if (x >= 250 && x < 284 && bnResPg > 0) {
      bnResPg--;
      bnDrawResult();
    } else if (x >= 284 && bnResPg + 1 < bnResPages()) {
      bnResPg++;
      bnDrawResult();
    }
    return;
  }
  if (bnPage == BP_SUITE) {
    if (y >= BN_SY && x >= 100 && x < 220) bnSuiteEnd(true);
    return;
  }
  if (y >= BN_SY) {
    if (x < 64) {
      if (bnSt == BS_RUN) {
        toast("Mod, test durunca değişir");
        return;
      }
      bnCfg.mode ^= 1;
      bnSave();
      bnRestartStill();
    } else if (x >= 256) {
      if (bnSt == BS_RUN) bnStopRun(BST_USER);
      else bnStart();
    } else if (!(bnCfg.view & 1)) {
      bnStripPg = (bnStripPg + 1) % 4;
      bnDrawStrip(true);
    }
    return;
  }
  if (bnSt != BS_RUN && y < BN_FY + bnVh()) {  // fraktala dokun: tam ekran ↔ pano
    bnCfg.view ^= 1;
    bnSave();
    bnRestartStill();
  }
}

static void bnSwipe(int dir) {
  if (bnPage != BP_RESULT) return;
  if (dir == SW_LEFT && bnResPg + 1 < bnResPages()) bnResPg++;
  else if (dir == SW_RIGHT && bnResPg > 0) bnResPg--;
  else return;
  bnDrawResult();
}

static bool bnRaw(uint8_t, int, int) { return false; }  // yalnız afişler susmalı (appWantsQuiet); dokunuşlar bnTap'te

static void bnTick() {
  uint32_t ms = millis();
  if (bnSt == BS_SUITE) {
    lastInteract = ms;
    bnSuiteTick();
    return;
  }
  if (bnSt == BS_IDLE) {
    if (B && B->nSlot) {  // bırakılamamış ara bellek: işçiler bitince
      if (bnWorkersAlive()) return;
      bnRelease();
    }
    if (bnWantStill && bnPage == BP_LIVE && (!bnErr.length() || ms - bnErrAt > 3000)) bnStill();
    return;
  }
  if (!B) return;
  if (bnSt == BS_RUN) {
    BnRun& r = B->r;
    lastInteract = ms;  // test sürerken ekran kilitlenmez
    uint32_t gap = ms - r.lastTick;
    if (gap > 250) {  // menü ya da sistem penceresi açıktı: süre ve kare aralığı düşülür
      r.pausedMs += gap;
      r.pauses++;
      r.lastFrame += gap * 1000;
    }
    r.lastTick = ms;
  }
#ifdef HOST
  {  // önizleme: görev yok, bir kare burada hesaplanır
    uint32_t ly = 0;
    for (int guard = 0; guard < 4000; guard++) {
      uint32_t pg = B->pg;
      bool did = bnWorkStep(1, ly);
      did |= bnPushReady(0xFFFFFFFF);
      if (B->pg != pg || !did) break;
    }
  }
#else
  bnPushReady(12000);
#endif
  if (bnSt == BS_STILL) {
    if (B->pg >= 1) {
      bnSt = BS_IDLE;
      bnRelease();
      bnDrawStrip(false);
    }
    return;
  }
  bnPrepNext();
  bnMonitor();
  if (bnSt != BS_RUN) return;
  if (ms - B->r.lastUi >= 500 && !toastUntil) {
    B->r.lastUi = ms;
    bnDrawStrip(false);
    bnDrawPano(false);
  }
}

static bool bnBack() {
  if (bnPage == BP_RESULT) {
    bnPage = BP_LIVE;
    shellRedraw(true);
    return true;
  }
  if (bnPage == BP_SUITE) {
    bnSuiteEnd(true);
    return true;
  }
  return false;
}

static void bnOpen() {
  bnLoad();
  bnPage = BP_LIVE;
  bnHaveFrame = false;
  bnErr = "";
}

static void bnClose() {
  if (S) bnSuiteEnd(true, false);
  bnStopRun(BST_CLOSE);
  bnSt = BS_IDLE;
  bnRelease();
  if (B && !bnWorkersAlive()) {
    delete B;
    B = nullptr;
  }
  std::vector<String>().swap(bnRep);
  std::vector<uint16_t>().swap(bnPgAt);
  bnPage = BP_LIVE;
  bnWantStill = false;
}

static const App APP_BENCH = {"Infinity", "Infinity", I_INFINITY, bnOpen, bnDraw, bnTap, nullptr, bnSwipe, bnRaw, bnTick, bnBack, bnClose};
