// DAXEN VANE · birim testleri (bilgisayarda, gerçek yazılım koduyla).
// ./run.sh test bunları AddressSanitizer ve UndefinedBehaviorSanitizer ile derleyip çalıştırır.
#include "host_sys.h"
#include "core/calib.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>


static int g_fail = 0, g_pass = 0;
static std::string g_group;
#define CHECK(cond)                                                                          \
  do {                                                                                       \
    if (cond) g_pass++;                                                                      \
    else {                                                                                   \
      g_fail++;                                                                              \
      printf("  HATA %s:%d [%s] %s\n", __FILE__, __LINE__, g_group.c_str(), #cond);       \
    }                                                                                        \
  } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

static void group(const char* g) {
  g_group = g;
  printf("· %s\n", g);
}

// ---------------------------------------------------------------------------
// Dokunmatik kalibrasyonu
// ---------------------------------------------------------------------------
static void testCalib() {
  group("dokunmatik kalibrasyonu");
  const int PX[9] = {24, 160, 296, 24, 160, 296, 24, 160, 296};
  const int PY[9] = {26, 26, 26, 120, 120, 120, 214, 214, 214};
  // Gerçek bir panele benzer ters dönüşüm: eksenler yer değiştirmiş, biri ters, 2° dönük
  auto rawOf = [](float x, float y, int& rx, int& ry, bool swap) {
    float ang = 2.0f * (float)M_PI / 180.f;
    float u = 3700 - x * 10.9f, v = 300 + y * 14.2f;  // ölçek ve ters eksen
    float ur = u * cosf(ang) - v * sinf(ang) + 60, vr = u * sinf(ang) + v * cosf(ang) - 40;
    rx = lroundf(swap ? vr : ur);
    ry = lroundf(swap ? ur : vr);
  };
  for (int swap = 0; swap < 2; swap++) {
    int rx[9], ry[9];
    for (int i = 0; i < 9; i++) rawOf(PX[i], PY[i], rx[i], ry[i], swap);
    TouchCal k;
    CalFit fit;
    CHECK(calSolve(rx, ry, PX, PY, 9, k, &fit));
    CHECK(fit.rms < 0.2f);
    // ayarda kullanılmayan noktalar da doğru eşlenmeli
    const int tx[4] = {92, 228, 5, 315}, ty[4] = {74, 168, 5, 235};
    for (int i = 0; i < 4; i++) {
      int a, b;
      rawOf(tx[i], ty[i], a, b, swap);
      float x, y;
      calApply(k, a, b, x, y);
      CHECK_NEAR(x, tx[i], 0.6);
      CHECK_NEAR(y, ty[i], 0.6);
    }
    // eski biçime dönüş ve geri: kabaca aynı eşleme (dönüklük kaybolur, birkaç piksel)
    bool sw;
    int x1, x2, y1, y2;
    calToLegacy(k, sw, x1, x2, y1, y2);
    CHECK(sw == (swap != 0));
    TouchCal L = calFromLegacy(sw, x1, x2, y1, y2);
    float x, y;
    calApply(L, rx[4], ry[4], x, y);
    CHECK_NEAR(x, 160, 6);
    CHECK_NEAR(y, 120, 6);
  }
  // gürültülü ölçüm: ±6 ham birim (~0,5 px) gürültüyle bile sapma 1 pikselin altında
  {
    int rx[9], ry[9];
    unsigned seed = 7;
    for (int i = 0; i < 9; i++) {
      rawOf(PX[i], PY[i], rx[i], ry[i], false);
      seed = seed * 1103515245 + 12345;
      rx[i] += (int)((seed >> 16) % 13) - 6;
      seed = seed * 1103515245 + 12345;
      ry[i] += (int)((seed >> 16) % 13) - 6;
    }
    TouchCal k;
    CalFit fit;
    CHECK(calSolve(rx, ry, PX, PY, 9, k, &fit));
    CHECK(fit.rms < 1.0f);
  }
  // hepsi aynı yere basılırsa çözüm reddedilir
  {
    int rx[9], ry[9];
    for (int i = 0; i < 9; i++) rx[i] = 2000, ry[i] = 2000 + (i & 1);
    TouchCal k;
    CHECK(!calSolve(rx, ry, PX, PY, 9, k));
  }
  // eski kayıt (varsayılan değerler) köşeleri doğru eşler
  {
    TouchCal L = calFromLegacy(false, 400, 3480, 520, 3520);
    float x, y;
    calApply(L, 400, 520, x, y);
    CHECK_NEAR(x, 20, 0.01);
    CHECK_NEAR(y, 20, 0.01);
    calApply(L, 3480, 3520, x, y);
    CHECK_NEAR(x, 300, 0.01);
    CHECK_NEAR(y, 220, 0.01);
  }
  int v[5] = {9, 1, 5, 1000, 4};
  CHECK(calMedian(v, 5) == 5);
}

// ---------------------------------------------------------------------------
// Satranç: perft (yasal hamle ağacının yaprak sayısı, bilinen değerler)
// ---------------------------------------------------------------------------
static uint64_t perft(const ChPos& p, int d) {
  std::vector<ChMove> mv;
  chLegal(p, mv);
  if (d == 1) return mv.size();
  uint64_t n = 0;
  for (auto& m : mv) {
    ChPos q;
    chMake(p, m, q);
    n += perft(q, d - 1);
  }
  return n;
}

// FEN'den konum (yalnızca testler için)
static bool fenPos(const char* fen, ChPos& p) {
  memset(&p, 0, sizeof p);
  p.ep = -1;
  int sq = 0;
  const char* s = fen;
  for (; *s && *s != ' '; s++) {
    if (*s == '/') continue;
    if (isdigit((unsigned char)*s)) {
      sq += *s - '0';
      continue;
    }
    int t = 0;
    switch (tolower(*s)) {
      case 'p': t = CP_P; break;
      case 'n': t = CP_N; break;
      case 'b': t = CP_B; break;
      case 'r': t = CP_R; break;
      case 'q': t = CP_Q; break;
      case 'k': t = CP_K; break;
      default: return false;
    }
    if (sq >= 64) return false;
    p.b[sq++] = isupper((unsigned char)*s) ? t : -t;
  }
  if (sq != 64 || *s != ' ') return false;
  s++;
  p.side = *s == 'w' ? 1 : -1;
  s += 2;
  for (; *s && *s != ' '; s++) {
    if (*s == 'K') p.castle |= 1;
    if (*s == 'Q') p.castle |= 2;
    if (*s == 'k') p.castle |= 4;
    if (*s == 'q') p.castle |= 8;
  }
  if (*s == ' ') s++;
  if (*s && *s != '-') p.ep = (8 - (s[1] - '0')) * 8 + (s[0] - 'a');
  return true;
}

static void testChess() {
  group("satranç kuralları (perft)");
  ChPos p;
  chInit(p);
  CHECK(perft(p, 1) == 20);
  CHECK(perft(p, 2) == 400);
  CHECK(perft(p, 3) == 8902);
  CHECK(perft(p, 4) == 197281);
  // "Kiwipete": rok, geçerken alma, terfi, açmaz hepsi bir arada
  ChPos k;
  CHECK(fenPos("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", k));
  CHECK(perft(k, 1) == 48);
  CHECK(perft(k, 2) == 2039);
  CHECK(perft(k, 3) == 97862);
  // Konum 3: geçerken almayla açılan şah
  ChPos t;
  CHECK(fenPos("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", t));
  CHECK(perft(t, 1) == 14);
  CHECK(perft(t, 2) == 191);
  CHECK(perft(t, 3) == 2812);
  CHECK(perft(t, 4) == 43238);
  // Konum 4: terfiler ve rok hakları
  ChPos f;
  CHECK(fenPos("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", f));
  CHECK(perft(f, 1) == 6);
  CHECK(perft(f, 2) == 264);
  CHECK(perft(f, 3) == 9467);
  // motor: matı bir hamlede görür (Qh5xf7#)
  ChPos m;
  CHECK(fenPos("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", m));
  ChMove best;
  chNodeLimit = 60000;
  CHECK(chBest(m, 2, 0, best));
  CHECK(best.from == 31 && best.to == 13);  // h5 → f7
}

// ---------------------------------------------------------------------------
// Hesap makinesi
// ---------------------------------------------------------------------------
static void testCalc() {
  group("hesap makinesi");
  struct C {
    const char* e;
    double v;
  } ok[] = {
      {"2+3×4", 14},      {"(2+3)×4", 20},   {"2^10", 1024},     {"10÷4", 2.5},      {"−3+5", 2},
      {"√16", 4},         {"2π", 2 * M_PI},  {"sin(30)", 0.5},   {"cos(60)", 0.5},   {"2(3+4)", 14},
      {"1,5+1,5", 3},     {"(3", 3},         {"-2^2", -4},       {"3-2-1", 0},       {"2^3^2", 512},
  };
  calcDeg = true;
  for (auto& c : ok) {
    double r = 0;
    bool good = calcEval(c.e, r);
    if (!good || std::fabs(r - c.v) > 1e-9) printf("    %s → %s %.12g (beklenen %.12g)\n", c.e, good ? "=" : "hata", r, c.v);
    CHECK(good && std::fabs(r - c.v) <= 1e-9);
  }
  const char* bad[] = {"2+", "1÷0", "√(-1)", "×2", ")"};  // açık kalan parantez kendiliğinden kapanır
  for (auto e : bad) {
    double r;
    bool good = calcEval(e, r);
    if (good) printf("    %s hata vermeliydi (= %g)\n", e, r);
    CHECK(!good);
  }
  CHECK(calcFmt(2.5) == "2,5");
  CHECK(calcFmt(1.0 / 3) == "0,3333333333" || calcFmt(1.0 / 3).startsWith("0,333333"));
}

// ---------------------------------------------------------------------------
// Türkçe metin yardımcıları
// ---------------------------------------------------------------------------
static void testText() {
  group("Türkçe metin");
  CHECK(trUpper("istanbul ılık") == "İSTANBUL ILIK");
  CHECK(trLower("IŞIK İzmir") == "ışık izmir");
  CHECK(utf8Count("çğıöşü") == 6);
  CHECK(safeName("a/b\\c:d") != "a/b\\c:d");
  CHECK(baseName("/Muzik/Sezen Aksu/Firuze.mp3") == "Firuze.mp3");
  CHECK(dirName("/Muzik/Sezen Aksu/Firuze.mp3") == "/Muzik/Sezen Aksu");
  CHECK(extOf("Şarkı.MP3") == "mp3");
  CHECK(joinPath("/", "a") == "/a");
  CHECK(joinPath("/x", "a") == "/x/a");
  CHECK(fmtMs(65000) == "1:05");
}

// ---------------------------------------------------------------------------
// Sudoku: üretilen bulmacanın tek çözümü olmalı ve çözüm geçerli olmalı
// ---------------------------------------------------------------------------
static void testSudoku() {
  group("sudoku üretici");
  for (int level = 0; level < 3; level++) {
    sdkNew(level);
    uint8_t g[81];
    memcpy(g, sdkGiven, 81);
    CHECK(sdkSolve(g, 2, false) == 1);
    bool valid = true;
    for (int i = 0; i < 81; i++) {
      int v = sdkSol[i];
      if (v < 1 || v > 9) valid = false;
      uint8_t t = sdkSol[i];
      sdkSol[i] = 0;
      if (!sdkOk(sdkSol, i, t)) valid = false;
      sdkSol[i] = t;
      if (sdkGiven[i] && sdkGiven[i] != sdkSol[i]) valid = false;
    }
    CHECK(valid);
  }
}

// ---------------------------------------------------------------------------
// Lua: arabirim ve kum havuzu
// ---------------------------------------------------------------------------
static void testLua() {
  group("Lua uygulamaları");
  luaScanned = false;
  int n = luaAppCount();
  CHECK(n >= 5);  // örnekler yazıldı
  // geçici bir uygulama: arabirimin her parçası, kum havuzu dışına çıkma denemesi hata vermeli
  FILE* f = fopen((g_sdroot + "/daxen/uygulamalar/test_api.lua").c_str(), "w");
  fputs(
      "-- ad: Test\n"
      "local ok = true\n"
      "function baslat()\n"
      "  assert(ekran.en == 320 and ekran.boy == 240)\n"
      "  assert(dosya.yaz('a.txt', 'merhaba'))\n"
      "  assert(dosya.oku('a.txt') == 'merhaba')\n"
      "  dosya.ekle('a.txt', '!')\n"
      "  assert(dosya.oku('a.txt') == 'merhaba!')\n"
      "  ayar.kaydet('k', 42)\n"
      "  assert(ayar.al('k') == '42')\n"
      "  assert(not pcall(dosya.oku, '../sir.txt'))\n"
      "  assert(not pcall(dosya.oku, 'x/y'))\n"
      "  assert(io == nil and os == nil and dofile == nil and loadfile == nil)\n"
      "  assert(math.floor(7 / 2) == 3 and 7 // 2 == 3)\n"
      "  ekran.yazi('x', 10.5, 40.5)\n"  // ondalıklı koordinat hata vermez
      "  sistem.bildir('tamam')\n"
      "end\n"
      "function ciz() ekran.temizle() end\n",
      f);
  fclose(f);
  luaScanned = false;
  int idx = -1;
  for (int i = 0; i < luaAppCount(); i++)
    if (luaAppLabel(i) == "Test") idx = i;
  CHECK(idx >= 0);
  if (idx >= 0) {
    luaAppOpen(idx);
    shellTick();
    CHECK(!luaHasError());
    if (luaHasError()) printf("    Lua hatası: %s\n", luaErr.c_str());
    appExit();
  }
  // söz dizimi hatası: dosya adı ve satır numarası gösterilir
  f = fopen((g_sdroot + "/daxen/uygulamalar/test_hata.lua").c_str(), "w");
  fputs("-- ad: Hata\nfunction ciz()\n  ekran.temizle(\nend\n", f);
  fclose(f);
  luaScanned = false;
  for (int i = 0; i < luaAppCount(); i++)
    if (luaAppLabel(i) == "Hata") idx = i;
  luaAppOpen(idx);
  shellTick();
  CHECK(luaHasError());
  CHECK(luaErr.indexOf("test_hata.lua:") >= 0);
  appExit();
  remove((g_sdroot + "/daxen/uygulamalar/test_api.lua").c_str());
  remove((g_sdroot + "/daxen/uygulamalar/test_hata.lua").c_str());
}

// ---------------------------------------------------------------------------
// Çekirdek: süreç tablosu
// ---------------------------------------------------------------------------
static int g_svcRuns = 0;
static void testKernel() {
  group("çekirdek");
  int before = 0;
  for (auto& p : kProcs)
    if (p.used) before++;
  int pid = kService("test", 10, []() { g_svcRuns++; });
  CHECK(pid > 0);
  Proc* p = kFind(pid);
  CHECK(p && p->kind == PK_SERVICE && p->period == 10);
  int t = kSpawn("test-görev", [](Proc* pr) { kAddBusy(pr, 1500); }, 4096, true);
  CHECK(t > 0 && !kAlive(t));  // önizlemede eşzamanlı çalışır ve biter
  kKill(pid);                  // servisler sonlandırılamaz
  CHECK(!kFind(pid)->killReq);
  p->used = false;  // temizlik
  int after = 0;
  for (auto& q : kProcs)
    if (q.used) after++;
  CHECK(after <= before + 1);
}

int main() {
  setupState();
  testCalib();
  testChess();
  testCalc();
  testText();
  testSudoku();
  testLua();
  testKernel();
  printf("\n%d denetim geçti, %d hata\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
