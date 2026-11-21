// DAXEN VANE · birim testleri (bilgisayarda, gerçek yazılım koduyla).
// ./run.sh test bunları AddressSanitizer ve UndefinedBehaviorSanitizer ile derleyip çalıştırır.
#include "host_sys.h"
#include "core/calib.h"
#include "sys/ota_roots.h"

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
    CHECK(ov.kind == OV_DIALOG && ov.msg.indexOf("eski uygulama") >= 0);  // ilk açılışta izin sorulur
    ov.cb(1);
    ov = Overlay();
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
  luaOpenOk(idx);
  shellTick();
  CHECK(luaHasError());
  CHECK(luaErr.indexOf("test_hata.lua:") >= 0);
  appExit();
  remove((g_sdroot + "/daxen/uygulamalar/test_hata.lua").c_str());

  group("Lua manifesti ve izinler");
  CHECK(luaParsePerms("ekran, Dosya ,bluetooth,uçak") == (LP_EKRAN | LP_DOSYA | LP_BT) && luaParsePerms("") == 0);
  CHECK(luaPermText(LP_EKRAN | LP_LED | LP_WIFI) == "ekran, led, wifi" && luaPermText(0) == "hiçbiri");
  LuaAppInfo m;
  for (const char* l : {"-- ad: Radar", "-- surum: 2.0.1", "-- izin: ekran, bluetooth", "-- arkaplan: hayır", "-- bir not: yok"}) luaParseHeader(m, l);
  CHECK(m.name == "Radar" && m.ver == "2.0.1" && m.perm == (LP_EKRAN | LP_BT) && m.manifest && !m.bg);
  CHECK(LuaAppInfo().perm == LP_ESKI && !LuaAppInfo().manifest);
  // yalnız ekran izni olan uygulama: led ve dosya hata verir, günlüğe yazılır; load yalnız metin
  auto writeApp = [](const char* name, const char* body) {
    FILE* w = fopen((g_sdroot + "/daxen/uygulamalar/" + name).c_str(), "w");
    fputs(body, w);
    fclose(w);
  };
  writeApp("test_izin.lua",
           "-- ad: İzin\n-- izin: ekran\n"
           "function baslat()\n"
           "  local ok, e = pcall(sistem.led, 1, 2, 3)\n"
           "  assert(not ok and e:find('izin yok: led'))\n"
           "  ok, e = pcall(dosya.yaz, 'a.txt', 'x')\n"
           "  assert(not ok and e:find('izin yok: dosya'))\n"
           "  assert(not pcall(wifi.durum) and not pcall(bluetooth.tara))\n"
           "  assert(load('return 1 + 1')() == 2)\n"
           "  local f, err = load(string.dump(function() return 1 end))\n"
           "  assert(f == nil and err:find('binary'))\n"
           "  sistem.gunluk('merhaba günlük')\n"
           "  sistem.led(0, 0, 0)\n"
           "end\n");
  luaScanned = false;
  int ti = -1;
  for (int i = 0; i < luaAppCount(); i++)
    if (luaAppLabel(i) == "İzin") ti = i;
  CHECK(ti >= 0);
  luaAppOpen(ti);
  CHECK(ov.kind == OV_DIALOG && ov.msg.indexOf("ekran") >= 0 && ov.msg.indexOf("eski") < 0);
  ov = Overlay();  // vazgeçildi: uygulama açılmaz, onay kaydedilmez
  CHECK(curApp != &APP_LUA && luaApproval(luaApps[ti].path) == 0);
  luaOpenOk(ti);
  shellTick();
  CHECK(luaHasError() && luaErr.indexOf("izin yok: led") >= 0);
  appExit();
  std::vector<String> lg;
  tlLogTail(luaDataDirOf(luaApps[ti].path) + "/gunluk.txt", 5, lg);
  CHECK(lg.size() == 2 && lg[0].indexOf("uygulama: merhaba günlük") >= 0 && lg[1].indexOf("hata: ") >= 0 && lg[1].indexOf("izin yok: led") >= 0);
  luaAppOpen(ti);  // onaylı ve değişmedi: pencere yok
  CHECK(ov.kind == OV_NONE && curApp == &APP_LUA);
  appExit();
  writeApp("test_izin.lua", "-- ad: İzin\n-- izin: ekran, led\nfunction baslat() sistem.led(0, 0, 0) end\n");
  luaScanned = false;
  luaAppCount();
  luaAppOpen(ti);  // dosya değişti: yeniden sorulur
  CHECK(ov.kind == OV_DIALOG && ov.msg.startsWith("Uygulama değişmiş"));
  ov.cb(1);
  ov = Overlay();
  shellTick();
  CHECK(!luaHasError());
  appExit();
  // wifi ve bluetooth okuma
  writeApp("test_radar.lua",
           "-- ad: Radar\n-- izin: ekran, wifi, bluetooth\n"
           "function baslat()\n"
           "  local w = wifi.durum()\n"
           "  assert(w.bagli and w.ag == 'EvAgi' and w.sinyal < 0)\n"
           "  assert(bluetooth.tara(5))\n"
           "  local c = bluetooth.cihazlar()\n"
           "  assert(#c == 4 and c[1].ad == 'Mi Band 7' and c[1].uretici == 'Huami' and c[1].sinyal == -58)\n"
           "  assert(c[2].ad == '' and c[2].uretici == 'Apple' and c[4].adres == 'D1:22:33:44:55:66')\n"
           "end\n");
  luaScanned = false;
  for (int i = 0; i < luaAppCount(); i++)
    if (luaAppLabel(i) == "Radar") ti = i;
  luaOpenOk(ti);
  shellTick();
  CHECK(!luaHasError());
  if (luaHasError()) printf("    Lua hatası: %s\n", luaErr.c_str());
  appExit();
  CHECK(bsCount() == 0);  // uygulama kapanınca liste bırakıldı

  group("Lua yönetimi (terminal)");
  CmdOut lo = termExec("lua list");
  bool seen = false;
  for (auto& l : lo.lines) seen |= l == "Radar · ekran, wifi, bluetooth · onaylı";
  CHECK(seen);
  lo = termExec("lua info radar");
  CHECK(lo.lines[0] == "Ad: Radar" && lo.lines[3] == "İzinler: ekran, wifi, bluetooth" && lo.lines[4] == "Onay: verildi");
  CHECK(termExec("lua info yok").lines[0].startsWith("Böyle bir Lua"));
  CHECK(termExec("lua log izin 1").lines.size() == 1);
  CHECK(termExec("lua remove radar").lines[0] == "Bu komut geliştirici yetkisi ister (açmak için: dev)");
  // kaldırma: onay penceresi, dosya silinir, veri klasörü kalır
  luaScanned = false;
  luaAppCount();
  for (int i = 0; i < luaAppCount(); i++)
    if (luaAppLabel(i) == "İzin") ti = i;
  String ip = luaApps[ti].path;
  luaRemoveAsk(ti);
  CHECK(ov.kind == OV_DIALOG);
  ov.cb(1);
  ov = Overlay();
  CHECK(!SD.exists(ip) && SD.exists(luaDataDirOf(ip) + "/gunluk.txt") && luaApproval(ip) == 0);
  // başka klasördeki .lua dosyası yüklenir
  FILE* w = fopen((g_sdroot + "/indirilen.lua").c_str(), "w");
  fputs("-- ad: İndirilen\n", w);
  fclose(w);
  openFileWith("/indirilen.lua");
  CHECK(ov.kind == OV_MENU && ov.opts.size() == 2);
  ov.cb(1);
  ov = Overlay();
  CHECK(SD.exists(LUA_APPS_DIR "/indirilen.lua"));
  remove((g_sdroot + "/indirilen.lua").c_str());
  remove((g_sdroot + "/daxen/uygulamalar/indirilen.lua").c_str());
  remove((g_sdroot + "/daxen/uygulamalar/test_radar.lua").c_str());
  remove((g_sdroot + "/daxen/uygulamalar/test_api.lua").c_str());

  group("küçük sscanf (kütüphane yerine)");
  unsigned ma = 9, mi = 9, pa = 9;
  CHECK(miniScan("1.2.0", "%u.%u.%u", &ma, &mi, &pa) == 3 && ma == 1 && mi == 2 && pa == 0);  // eş yaşam sürümü
  CHECK(miniScan("v12.30.7x", "v%u.%u.%u", &ma, &mi, &pa) == 3 && ma == 12 && mi == 30 && pa == 7);
  CHECK(miniScan("1.x.0", "%u.%u.%u", &ma, &mi, &pa) == 1);
  uint8_t hb[3] = {0};
  CHECK(miniScan("a1:0B:ff", "%2hhx:%2hhx:%2hhx", &hb[0], &hb[1], &hb[2]) == 3 && hb[0] == 0xa1 && hb[1] == 0x0b && hb[2] == 0xff);
  int sd = 0;
  unsigned short hs = 0;
  CHECK(miniScan("  -42 ve 65535", "%d ve %hu", &sd, &hs) == 2 && sd == -42 && hs == 65535);
  CHECK(miniScan("", "%u", &ma) == 0 && miniScan("12345", "%3u", &ma) == 1 && ma == 123);
  // saat dilimi çözümlemesi (tzset'in biçimleri): C kütüphanesinin sscanf'iyle aynı sonuç
  struct TzCase {
    const char* fmt;
    const char* in;
  } tz[] = {{"%10[-+0-9A-Za-z]%n", "+03>-3"},  {"%10[-+0-9A-Za-z]%n", "MESZ+0330>"}, {"%10[A-Za-z]%n", "CET-1CEST"},
            {"%10[A-Za-z]%n", "ABCDEFGHIJKLM5"}, {"%10[A-Za-z]%n", "5X"},           {"%hu%n:%hu%n:%hu%n", "3"},
            {"%hu%n:%hu%n:%hu%n", "3:30"},       {"%hu%n:%hu%n:%hu%n", "10:05:59,M3"}, {"M%hu%n.%hu%n.%hu%n", "M3.5.0/3"},
            {"M%hu%n.%hu%n.%hu%n", "J60"},        {"/%hu%n:%hu%n:%hu%n", "/3,M10"},     {"%hu%n:%hu%n:%hu%n", ""}};
  bool same = true;
  for (auto& c : tz) {
    char b1[16] = {0}, b2[16] = {0};
    int n1 = -9, n2 = -9, m1 = -9, m2 = -9, k1 = -9, k2 = -9;
    unsigned short h1[3] = {0}, h2[3] = {0};
    int r1, r2;
    if (c.fmt[1] == '1') {  // %10[…]%n
      r1 = miniScan(c.in, c.fmt, b1, &n1);
      r2 = sscanf(c.in, c.fmt, b2, &n2);
    } else {
      r1 = miniScan(c.in, c.fmt, &h1[0], &n1, &h1[1], &m1, &h1[2], &k1);
      r2 = sscanf(c.in, c.fmt, &h2[0], &n2, &h2[1], &m2, &h2[2], &k2);
    }
    bool ok = (r1 == r2 || (r1 == 0 && r2 == -1)) && !strcmp(b1, b2) && !memcmp(h1, h2, sizeof h1) && n1 == n2 && m1 == m2 && k1 == k2;
    if (!ok) printf("    fark: \"%s\" \"%s\": %d/%d %s/%s n %d/%d\n", c.fmt, c.in, r1, r2, b1, b2, n1, n2);
    same &= ok;
  }
  CHECK(same);

  group("sıkıştırılmış dosyalar");
  CHECK(crc32Upd(0, (const uint8_t*)"123456789", 9) == 0xCBF43926u);
  CHECK(crc32Upd(crc32Upd(0, (const uint8_t*)"1234", 4), (const uint8_t*)"56789", 5) == 0xCBF43926u);
  bool allOk = true;
  for (auto& p : LUA_PACK) {
    std::vector<uint8_t> b(p.len);
    allOk &= lzUnpack(p.data, p.plen, b.data(), p.len) == p.len && b[0] != 0;
    allOk &= lzUnpack(p.data, p.plen, b.data(), p.len - 1) == 0;  // tampon küçükse 0
  }
  CHECK(allOk && sizeof(LUA_PACK) / sizeof(LUA_PACK[0]) >= 6);
  const uint8_t bad[] = {0x00, 0xff, 0xff};  // ilk öğe olmayan geçmişe başvuruyor
  uint8_t ob[8];
  CHECK(lzUnpack(bad, 3, ob, 8) == 0);
  // örnekler SD'de, rehber yeni sürüm
  File g = SD.open(LUA_APPS_DIR "/REHBER.txt");
  CHECK(g && g.readString().indexOf("-- izin:") >= 0);
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

// ---------------------------------------------------------------------------
// Kalıcı ayar geçişleri (core/migrate.h)
// ---------------------------------------------------------------------------
static void testMigrate() {
  group("ayar geçişleri");
  Preferences p;
  // Müzik yazılımının bıraktığı kayıtlar ve korunması gereken bir kullanıcı ayarı
  p.begin("mus", false);
  p.putInt("vol", 12);
  p.putString("hp", "00:11:22:33:44:55");
  p.end();
  p.begin("mus2", false);
  p.putString("lbl", "app1");
  p.end();
  p.begin("cs", false);
  p.remove("mig");
  p.putBool("getmus", true);
  p.putBool("mdl", true);
  p.putBool("inv", true);
  p.end();
  CHECK(storeMigrate(p) == STORE_MIG);
  p.begin("mus", true);
  CHECK(!p.isKey("vol") && !p.isKey("hp"));
  p.end();
  p.begin("mus2", true);
  CHECK(!p.isKey("lbl"));
  p.end();
  p.begin("cs", true);
  CHECK(!p.isKey("getmus") && !p.isKey("mdl"));
  CHECK(p.getBool("inv", false));  // kullanıcı ayarı kalır
  CHECK(p.getInt("mig", 0) == STORE_MIG);
  p.end();
  // İkinci açılışta hiçbir şey silinmez
  p.begin("mus", false);
  p.putInt("vol", 3);
  p.end();
  CHECK(storeMigrate(p) == STORE_MIG);
  p.begin("mus", true);
  CHECK(p.getInt("vol", 0) == 3);
  p.end();
}

// ---------------------------------------------------------------------------
// Sistem günlüğü halkası (core/log.h)
// ---------------------------------------------------------------------------
static void testLog() {
  group("günlük halkası");
  LogRing r;
  CHECK(logTail(r, 10).empty());
  logPut(r, "1 a: bir");
  logPut(r, "2 a: iki\nyarım");  // satır içi yeni satır boşluk olur
  auto t = logTail(r, 10);
  CHECK(t.size() == 2 && t[0] == "1 a: bir" && t[1] == "2 a: iki yarım");
  CHECK(logTail(r, 1).size() == 1 && logTail(r, 1)[0] == "2 a: iki yarım");
  // Halka defalarca dolar: dönen her satır tam olmalı, en sonuncusu en yenisi
  char line[64];
  for (int i = 0; i < 500; i++) {
    snprintf(line, sizeof line, "%5d test: satır %d son", i, i);
    logPut(r, line);
  }
  t = logTail(r, 1000);
  bool whole = !t.empty();
  for (auto& x : t) whole = whole && x.startsWith("  ") && x.indexOf("test: satır") > 0 && x.endsWith(" son");
  CHECK(whole);
  CHECK(t.back() == "  499 test: satır 499 son");
  CHECK(t.size() > 40 && t.size() < LOG_RING / 20);
  // Uzun satır kısaltılır
  String uzun;
  for (int i = 0; i < 300; i++) uzun += 'x';
  logPut(r, uzun.c_str());
  CHECK(logTail(r, 1)[0].length() == LOG_LINE_MAX);
  CHECK(r.used == LOG_RING && r.lines == 503);
}

// ---------------------------------------------------------------------------
// Güncelleme kök paketi (sys/ota_roots.h): kartın ikili aramasıyla her kök bulunmalı
// ---------------------------------------------------------------------------
static void testOtaRoots() {
  group("güncelleme kök paketi");
  const uint8_t* b = OTA_ROOTS;
  size_t n = (b[0] << 8) | b[1];
  CHECK(n == OTA_ROOTS_N && n >= 6);
  std::vector<const uint8_t*> crts;  // esp_crt_bundle_init ile aynı ayrıştırma
  const uint8_t* cur = b + 2;
  for (size_t i = 0; i < n; i++) {
    crts.push_back(cur);
    size_t nl = cur[0] << 8 | cur[1], kl = cur[2] << 8 | cur[3];
    cur += 4 + nl + kl;
  }
  CHECK((size_t)(cur - b) == sizeof(OTA_ROOTS));  // artık bayt yok
  bool sorted = true, found = true;
  for (size_t i = 0; i + 1 < n; i++) {
    size_t a = crts[i][0] << 8 | crts[i][1], c = crts[i + 1][0] << 8 | crts[i + 1][1];
    int r = memcmp(crts[i] + 4, crts[i + 1] + 4, std::min(a, c));
    sorted = sorted && (r < 0 || (r == 0 && a < c));
  }
  for (size_t k = 0; k < n; k++) {  // esp_crt_verify_callback'teki ikili arama
    const uint8_t* issuer = crts[k] + 4;
    int start = 0, end = (int)n - 1, middle = (end - start) / 2;
    bool hit = false;
    while (start <= end) {
      size_t nl = crts[middle][0] << 8 | crts[middle][1];
      int cmp = memcmp(issuer, crts[middle] + 4, nl);
      if (cmp == 0) {
        hit = (size_t)middle == k;
        break;
      }
      if (cmp < 0) end = middle - 1;
      else start = middle + 1;
      middle = (start + end) / 2;
    }
    found = found && hit;
  }
  CHECK(sorted);
  CHECK(found);
}

// ---------------------------------------------------------------------------
// Komut yorumlayıcısı (core/cmd.h) ve terminal komutları (apps/terminal.h)
// ---------------------------------------------------------------------------
static int g_cmdHits = 0;
static void cDummy(const std::vector<String>& a, CmdOut& o) {
  g_cmdHits++;
  o.add("arg=" + String((int)a.size()));
}
static void testCmd() {
  group("komut yorumlayıcısı");
  std::vector<String> w;
  String err;
  CHECK(cmdSplit("  wifi   ping  \"bir iki\"  ", w, err) && w.size() == 3 && w[2] == "bir iki");
  CHECK(cmdSplit("", w, err) && w.empty());
  CHECK(!cmdSplit("a \"kapanmadı", w, err));
  CHECK(!cmdSplit(String("x") + (char)7, w, err));
  String uzun;
  for (int i = 0; i < CMD_LINE_MAX + 1; i++) uzun += 'a';
  CHECK(!cmdSplit(uzun, w, err));
  CHECK(!cmdSplit("a b c d e f g h i", w, err));  // en fazla 2 + 6 kelime
  static const Cmd T[] = {
      {"say", nullptr, CL_USER, 0, 2, cDummy, "[a] [b]", ""},
      {"grup", "bir", CL_USER, 1, 1, cDummy, "<x>", ""},
      {"gizli", nullptr, CL_DEV, 0, 0, cDummy, "", ""},
  };
  CmdOut o;
  cmdRun(T, 3, "SAY 1 2", CL_USER, o);
  CHECK(g_cmdHits == 1 && o.lines.size() == 1 && o.lines[0] == "arg=2");
  o = CmdOut();
  cmdRun(T, 3, "say 1 2 3", CL_USER, o);  // fazla argüman
  CHECK(g_cmdHits == 1 && o.lines[0].startsWith("Kullanım: say"));
  o = CmdOut();
  cmdRun(T, 3, "grup", CL_USER, o);  // alt komut yok
  CHECK(g_cmdHits == 1 && o.lines.size() == 2 && o.lines[1].indexOf("grup bir") >= 0);
  o = CmdOut();
  cmdRun(T, 3, "grup bir x", CL_USER, o);
  CHECK(g_cmdHits == 2);
  o = CmdOut();
  cmdRun(T, 3, "gizli", CL_USER, o);  // yetki yetmez
  CHECK(g_cmdHits == 2 && o.lines[0].indexOf("geliştirici") >= 0);
  o = CmdOut();
  cmdRun(T, 3, "rm -rf /", CL_USER, o);  // tabloda yok: hiçbir şey çalışmaz
  CHECK(g_cmdHits == 2 && o.lines[0].startsWith("Bilinmeyen komut: rm"));
  CmdHistory h;
  for (int i = 0; i < 20; i++) h.push("k" + String(i));
  h.push("k19");
  CHECK(h.items.size() == 16 && h.items.front() == "k4" && h.items.back() == "k19");

  group("terminal komutları");
  CmdOut m = termExec("memory");
  CHECK(m.lines.size() == 5 && m.lines[0].startsWith("Boş: ") && m.lines[3].startsWith("Bulut canlı akışı"));
  CHECK(termExec("clear").clear);
  sysLog("test", "bir");
  sysLog("test", "iki");
  CmdOut lg = termExec("logs 2");
  CHECK(lg.lines.size() == 2 && lg.lines[1].endsWith("test: iki"));
  CmdOut hp = termExec("help");
  CHECK(hp.lines.size() > 10);
  CmdOut ts = termExec("tasks");
  CHECK(ts.lines.size() >= 2);
  CHECK(termExec("version").lines[0].startsWith("Yazılım: fw-"));
  CHECK(termExec("app launch yokboyle").lines[0].startsWith("Böyle bir uygulama yok"));
}

// ---------------------------------------------------------------------------
// Ağ teşhisi hesapları (core/netcalc.h) ve terminalin ağ komutları
// ---------------------------------------------------------------------------
static std::vector<uint8_t> ipPkt(uint8_t icmpType, uint16_t id, uint16_t seq, bool inner) {
  std::vector<uint8_t> p(20, 0);
  p[0] = 0x45;
  p[9] = 1;
  std::vector<uint8_t> ic = {icmpType, 0, 0, 0, 0, 0, 0, 0};
  if (!inner) {
    ic[4] = id >> 8, ic[5] = id & 255, ic[6] = seq >> 8, ic[7] = seq & 255;
  } else {  // içinde bizim IP başlığımız ve yankı isteğimiz
    std::vector<uint8_t> ip2(20, 0);
    ip2[0] = 0x45;
    ip2[9] = 1;
    ic.insert(ic.end(), ip2.begin(), ip2.end());
    std::vector<uint8_t> echo = {8, 0, 0, 0, (uint8_t)(id >> 8), (uint8_t)(id & 255), (uint8_t)(seq >> 8), (uint8_t)(seq & 255)};
    ic.insert(ic.end(), echo.begin(), echo.end());
  }
  p.insert(p.end(), ic.begin(), ic.end());
  return p;
}

static void testNet() {
  group("ağ hesapları");
  CHECK(rssiQuality(-40) == 100 && rssiQuality(-100) == 0 && rssiQuality(-75) == 50);
  CHECK(String(authText(3)) == "WPA2" && String(authText(0)) == "açık" && String(authText(99)) == "bilinmiyor");
  uint8_t m[6] = {0x24, 0x6f, 0x28, 0xaa, 0xbb, 0x0c};
  CHECK(macText(m) == "24:6F:28:AA:BB:0C");
  PingStats st;
  st.add(true, 4);
  st.add(false, 0);
  st.add(true, 10);
  st.add(true, 7);
  CHECK(st.lossPct() == 25 && st.minMs == 4 && st.maxMs == 10);
  CHECK(st.summary() == "4 gönderildi, 3 alındı, %25 kayıp · en az/ort/en çok 4/7/10 ms");
  CHECK(PingStats().summary() == "0 gönderildi, 0 alındı, %0 kayıp");
  auto e = ipPkt(0, 0x1234, 7, false);
  CHECK(icmpMatch(e.data(), e.size(), 0x1234, 7) == 0);
  CHECK(icmpMatch(e.data(), e.size(), 0x1234, 8) == -1);  // başka sıra
  auto t = ipPkt(11, 0xbeef, 3, true);
  CHECK(icmpMatch(t.data(), t.size(), 0xbeef, 3) == 11);
  CHECK(icmpMatch(t.data(), t.size(), 0xbeee, 3) == -1);  // başkasının isteği
  auto u = ipPkt(3, 0xbeef, 3, true);
  CHECK(icmpMatch(u.data(), u.size(), 0xbeef, 3) == 3);
  CHECK(icmpMatch(t.data(), 30, 0xbeef, 3) == -1);  // kısa paket
  auto q = ipPkt(8, 1, 1, false);
  CHECK(icmpMatch(q.data(), q.size(), 1, 1) == -1);  // istek, yanıt değil

  group("uygulama listesi sırası");
  const App* const* at = appTable();
  CHECK(at[AID_YKS] == &APP_YKS && at[AID_NOTES] == &APP_NOTES && at[AID_FILES] == &APP_FILES && at[AID_SETTINGS] == &APP_SETTINGS);
  CHECK(at[AID_TERMINAL] == &APP_TERMINAL && at[AID_NETWORK] == &APP_NETWORK && at[AID_SYSINFO] == &APP_SYSINFO && at[AID_TASKS] == &APP_TASKS);
  CHECK(at[AID_TRANSFER] == &APP_TRANSFER && at[AID_MEDIA] == &APP_MEDIA && at[AID_NOTIFS] == &APP_NOTIFS && at[AID_CALC] == &APP_CALC);
  CHECK(at[AID_CLOCK] == &APP_CLOCK && at[AID_WEATHER] == &APP_WEATHER && at[AID_TODO] == &APP_TODO && at[AID_GAMES] == &APP_GAMES);
  CHECK(at[AID_PHOTOS] == &APP_PHOTOS && at[AID_READER] == &APP_READER && at[AID_DRAW] == &APP_DRAW && at[AID_CARDS] == &APP_CARDS);
  CHECK(at[AID_LIGHT] == &APP_LIGHT && appTableCount() == 21);

  group("terminal ağ komutları");
  ndConsented = false;
  ov = Overlay();
  CmdOut o = termExec("wifi ping 192.168.1.1 3");
  CHECK(o.lines.size() == 1 && o.lines[0].indexOf("onay") >= 0 && ov.kind == OV_DIALOG);  // önce onay
  ov = Overlay();
  ndConsented = true;
  std::vector<String> drop;
  ndDrain(drop);
  o = termExec("wifi ping 192.168.1.1 3");
  std::vector<String> l;
  ndDrain(l);
  CHECK(o.lines.empty() && l.size() == 5 && l.back().indexOf("%33 kayıp") >= 0);
  CHECK(termExec("wifi tcp github.com 70000").lines[0].startsWith("Port"));
  CHECK(termExec("wifi status").lines.size() == 7);
  CHECK(termExec("wifi ip").lines.size() == 3);
  CHECK(termExec("stop").lines[0] == "Süren ağ ya da Bluetooth işi yok");
  ndDrain(drop);
}

// ---------------------------------------------------------------------------
// Bluetooth keşfi (core/blecalc.h) ve terminalin bluetooth komutları
// ---------------------------------------------------------------------------
static BleDev bleOf(const std::vector<uint8_t>& ad, int8_t rssi = -60) {
  BleDev d;
  bleDevInit(d);
  d.rssi = rssi;
  bleParseAd(ad.data(), ad.size(), d);
  return d;
}

// ---------------------------------------------------------------------------
// Güvenlik: PIN, bekleme, geliştirici seviyesi, ekran kilidi (core/auth.h, os/authui.h)
// ---------------------------------------------------------------------------
static void typePin(const char* v, bool ok = true) {  // açık klavyeye yazıp onaylar
  kbm.value = v;
  kbmFinish(ok);
}

static void testAuth() {
  group("PIN hesapları");
  CHECK(authPinValid("1234") && authPinValid("12345678") && !authPinValid("123") && !authPinValid("123456789") && !authPinValid("12a4"));
  CHECK(authDelayMs(4) == 0 && authDelayMs(5) == 30000 && authDelayMs(6) == 60000 && authDelayMs(9) == 480000 && authDelayMs(10) == 900000 &&
        authDelayMs(200) == 900000);
  uint8_t h[32];
  mbedtls_sha256_ret((const uint8_t*)"abc", 3, h, 0);
  CHECK(h[0] == 0xba && h[1] == 0x78 && h[31] == 0xad);  // FIPS 180-2 örneği
  uint8_t s1[16] = {1}, s2[16] = {2}, a[32], b[32];
  authHash(s1, "1234", a);
  authHash(s2, "1234", b);
  CHECK(!authSame(a, b, 32));  // aynı PIN, farklı tuz: farklı özet

  group("PIN saklama ve bekleme");
  authClearPin();
  CHECK(!authHasPin() && authCheck("0000") == 1);  // PIN yokken her şey açık
  CHECK(!authSetPin("12") && authSetPin("2468") && authHasPin());
  {
    prefs.begin("cs", true);
    uint8_t buf[64] = {0};
    size_t n = prefs.getBytes("pinh", buf, 64);
    prefs.end();
    CHECK(n == 32 && memmem(buf, n, "2468", 4) == nullptr);  // düz PIN yok
  }
  CHECK(authCheck("1357") == 0 && authCheck("2468") == 1 && authFails == 0);
  for (int i = 0; i < 5; i++) authCheck("0000");
  CHECK(authFails == 5 && authWaitSec() == 30 && authCheck("2468") == -1);  // bekleme sürerken denenmez
  authLoaded = false;
  authWaitUntil = 0;
  authLoad();  // yeniden başlatma bekletmeyi sıfırlamaz
  CHECK(authWaitSec() == 30);
  g_ms += 31000;
  CHECK(authCheck("2468") == 1 && authFails == 0 && !authWaitSec());

  group("geliştirici seviyesi");
  CHECK(authLevel() == CL_USER);
  CHECK(termExec("lua stop").lines[0].indexOf("geliştirici yetkisi") >= 0);
  CmdOut d = termExec("dev");
  CHECK(d.lines[0].startsWith("PIN'i kartın ekranında") && kbm.active && kbm.password);
  typePin("1111");
  CHECK(authLevel() == CL_USER);
  termExec("dev");
  typePin("2468");
  CHECK(authLevel() == CL_DEV && authDevLeftMin() == 10);
  CHECK(termExec("lua stop").lines[0] == "Çalışan Lua uygulaması yok");
  CHECK(termExec("dev").lines[0].startsWith("Geliştirici seviyesi açık (10 dk"));
  g_ms += 601000;
  CHECK(authLevel() == CL_USER);  // 10 dk sonra kendiliğinden kapanır
  authDevOn();
  CHECK(termExec("dev off").lines[0] == "Geliştirici seviyesi kapandı" && authLevel() == CL_USER);
  CHECK(termExec("backup").lines[0].startsWith("Yedeklendi") && g_backups == 1);
  CHECK(termExec("restore").lines[0].indexOf("geliştirici") >= 0);

  group("ekran kilidi");
  ov = Overlay();
  homeShow();
  cfg.lockOn = true;
  lastInteract = g_ms;
  shellTick();
  CHECK(!lockActive);
  g_ms += 301000;
  openApp(AID_CALC);
  lastInteract = g_ms - 301000;
  shellTick();  // 5 dk dokunulmadı: kilitlenir, ana ekrana döner
  CHECK(lockActive && shScreen == SH_HOME);
  tap(160, 120);  // dokunuş yalnızca PIN'i sorar
  CHECK(kbm.active && shScreen == SH_HOME);
  typePin("9999");
  CHECK(lockActive);
  tap(160, 10);  // durum çubuğu da sistem menüsünü açmaz
  CHECK(ov.kind == OV_NONE && kbm.active);
  typePin("", false);  // vazgeçildi: kilit sürer
  CHECK(lockActive && !kbm.active);
  shellBoot(1);  // BOOT ile Ayarlar açılmaz
  CHECK(curApp != &APP_SETTINGS && kbm.active);
  typePin("2468");
  CHECK(!lockActive);
  cfg.lockOn = false;

  group("Ayarlar → Güvenlik");
  authClearPin();
  settingsOpenPage(SP_SECURITY);
  tap(160, SEC_Y + 10);  // PIN yok: yeni PIN iki kez
  CHECK(kbm.active && kbm.title.startsWith("Yeni PIN"));
  typePin("1357");
  CHECK(kbm.active && kbm.title.indexOf("tekrar") >= 0);
  typePin("1358");
  CHECK(!authHasPin());  // aynı değil
  tap(160, SEC_Y + 10);
  typePin("1357");
  typePin("1357");
  CHECK(authHasPin());
  tap(160, SEC_Y + SEC_ROW + 10);  // ekran kilidi: PIN ister
  CHECK(kbm.active);
  typePin("1357");
  CHECK(cfg.lockOn);
  tap(160, SEC_Y + SEC_ROW * 3 + 10);  // yedek: şifresiz mi şifreli mi
  CHECK(ov.kind == OV_DIALOG && ov.opts.size() == 3);
  ov.cb(2);  // şifreli: önce PIN
  ov = Overlay();
  CHECK(kbm.active);
  typePin("1357");
  CHECK(g_backups == 11);
  tap(160, SEC_Y + 10);  // PIN'i kaldır: kilit de kapanır
  CHECK(ov.kind == OV_MENU);
  ov.cb(1);
  ov = Overlay();
  typePin("1357");
  CHECK(!authHasPin() && !cfg.lockOn);
  appExit();
  homeShow();
}

static void testBle() {
  group("BLE reklam çözücü");
  BleDev d = bleOf({2, 1, 6, 5, 9, 'V', 'a', 'n', 'e', 4, 0xFF, 0x4C, 0x00, 7, 3, 2, 0x0F, 0x18, 2, 0x0A, 0xF4, 3, 0x19, 0xC1, 0x00});
  CHECK(String(d.name) == "Vane" && d.mfr == 0x004C && d.tx == -12 && d.appear == 0x00C1);
  CHECK(d.nSvc == 1 && d.svc[0] == 0x180F && String(bleCompany(d.mfr)) == "Apple");
  CHECK(String(bleAppear(0x00C1)) == "Kol saati" && String(bleAppear(0x0941)) == "Kulaklık" && String(bleAppear(0x0A41)) == "İşitme cihazı");
  CHECK(bleAppear(0x0AC0) == nullptr && String(bleAppear(0x0040)) == "Telefon");
  CHECK(bleAppear(0x0C40) == nullptr && bleAppear(0x0001) == nullptr);
  CHECK(bleCompany(0xFFFE) == nullptr && String(bleSvcName(0xFEAA)) == "Eddystone" && bleSvcName(0x1234) == nullptr);
  // bozuk alanlar: sıfır uzunluk durdurur, taşan alan okunmaz
  BleDev z = bleOf({2, 1, 6, 0, 9, 'X'});
  CHECK(!z.name[0]);
  BleDev o = bleOf({2, 1, 6, 20, 9, 'A', 'B'});
  CHECK(!o.name[0] && o.mfr == 0xFFFF && o.tx == BD_NO_TX);
  BleDev s1 = bleOf({4, 0xFF, 0x4C});  // üretici verisi tek bayt: alınmaz
  CHECK(s1.mfr == 0xFFFF);
  // kısaltılmış ad yalnız tam ad yoksa; tam ad önceliklidir
  CHECK(String(bleOf({4, 8, 'A', 'B', 'C', 6, 9, 'A', 'B', 'C', 'D', 'E'}).name) == "ABCDE");
  CHECK(String(bleOf({6, 9, 'A', 'B', 'C', 'D', 'E', 4, 8, 'A', 'B', 'C'}).name) == "ABCDE");
  // ad: 20 bayt sınırı, UTF-8 karakter ortasından kesilmez, denetim karakterleri atılır
  std::vector<uint8_t> nm = {22, 9};
  const char* tr = "Çalışma ışığı 12345";  // 19 harf, 24 bayt
  for (const char* c = tr; *c; c++) nm.push_back((uint8_t)*c);
  nm[0] = nm.size() - 1;
  BleDev u = bleOf(nm);
  CHECK(strlen(u.name) <= 20 && String(u.name) == "Çalışma ışığ");
  CHECK(String(bleOf({5, 9, 'a', 0, 'b', '\n'}).name) == "ab");
  CHECK(String(bleOf({4, 9, 'a', 0xC3, 'b'}).name) == "a");  // bozuk UTF-8: orada kesilir
  // DAXEN servis kimliği (128 bit, küçük uçlu)
  std::vector<uint8_t> dx = {17, 7};
  const uint8_t svcLe[16] = {0xde, 0xc0, 0xa7, 0xa5, 0x41, 0x0c, 0x8e, 0x9b, 0x2d, 0x4c, 0x17, 0x5a, 0x01, 0x00, 0x3a, 0x6e};
  dx.insert(dx.end(), svcLe, svcLe + 16);
  BleDev dv = bleOf(dx);
  CHECK((dv.flags & BD_DAXEN) && dv.n128 == 1 && String(bleTitle(dv)) == "DAXEN VANE");
  dx[2] ^= 1;
  CHECK(!(bleOf(dx).flags & BD_DAXEN) && bleOf(dx).n128 == 1);
  // servis verisi (0x16) 16 bit kimlik olarak, en fazla 3, tekrar yok
  BleDev sv = bleOf({7, 3, 0x0F, 0x18, 0x0F, 0x18, 0x0D, 0x18, 3, 0x16, 0xAA, 0xFE, 3, 3, 0x0A, 0x18});
  CHECK(sv.nSvc == 3 && sv.svc[0] == 0x180F && sv.svc[1] == 0x180D && sv.svc[2] == 0xFEAA);

  group("BLE adres türü ve liste");
  BleDev a;
  bleDevInit(a);
  uint8_t ad1[6] = {0xC8, 1, 2, 3, 4, 5};
  memcpy(a.addr, ad1, 6);
  CHECK(String(bleAddrKind(a)) == "genel adres");
  a.addrType = 1;
  CHECK(String(bleAddrKind(a)) == "rastgele, sabit");
  a.addr[0] = 0x5E;
  CHECK(String(bleAddrKind(a)) == "gizlilik adresi (değişir)");
  a.addr[0] = 0x12;
  CHECK(String(bleAddrKind(a)) == "rastgele, çözülemez");

  BleDev L[3];
  int n = 0;
  uint16_t drop = 0;
  auto mk = [](uint8_t last, int8_t rssi) {
    BleDev x;
    bleDevInit(x);
    x.addr[5] = last;
    x.rssi = rssi;
    return x;
  };
  CHECK(bleMerge(L, n, 3, mk(1, -70), 1000, drop) == 0 && n == 1 && L[0].seen == 1 && L[0].rssiMax == -70);
  BleDev upd = mk(1, -60);
  strcpy(upd.name, "Saat");
  upd.mfr = 0x0075;
  upd.flags = BD_CONN;
  CHECK(bleMerge(L, n, 3, upd, 2000, drop) == 0 && n == 1 && L[0].seen == 2 && L[0].rssi == -60 && L[0].rssiMax == -60);
  CHECK(String(L[0].name) == "Saat" && L[0].mfr == 0x0075 && L[0].first == 1000 && L[0].last == 2000 && (L[0].flags & BD_CONN));
  BleDev weaker = mk(1, -80);  // adsız tarama yanıtı: ad ve üretici silinmez, en güçlü değer kalır
  CHECK(bleMerge(L, n, 3, weaker, 3000, drop) == 0 && String(L[0].name) == "Saat" && L[0].mfr == 0x0075 && L[0].rssi == -80 && L[0].rssiMax == -60);
  BleDev other = mk(1, -70);
  other.addrType = 1;  // aynı baytlar, başka adres türü: ayrı cihaz
  CHECK(bleMerge(L, n, 3, other, 3000, drop) == 1 && n == 2);
  CHECK(bleMerge(L, n, 3, mk(3, -90), 3000, drop) == 2 && n == 3);
  CHECK(bleMerge(L, n, 3, mk(4, -95), 3000, drop) == -1 && drop == 1 && n == 3);  // dolu, daha zayıf
  CHECK(bleMerge(L, n, 3, mk(5, -50), 3000, drop) == 2 && drop == 2 && L[2].addr[5] == 5);  // en zayıfın yerine
  uint8_t idx[3];
  bleOrder(L, n, idx);
  CHECK(L[idx[0]].addr[5] == 5 && L[idx[1]].addr[5] == 1 && L[idx[1]].addrType == 0 && L[idx[2]].addrType == 1);

  group("BLE ayrıntı satırları");
  BleDev x = mk(9, -66);
  x.rssiMax = -61;
  x.mfr = 0x038F;
  x.svc[0] = 0x180F, x.svc[1] = 0xFEE0, x.nSvc = 2, x.n128 = 1;
  x.flags = BD_NOCONN;
  x.seen = 12, x.first = 1000, x.last = 9500;
  std::vector<String> det;
  bleDetail(x, 10000, det);
  CHECK(det.size() == 7 && det[0] == "Ad: reklamda yok" && det[1] == "Adres: 00:00:00:00:00:09 · genel adres");
  CHECK(det[2] == "Sinyal: -66 dBm (orta) · en güçlü -61 dBm" && det[3] == "Üretici: Xiaomi (0x038F)");
  CHECK(det[4] == "Servisler: Pil 0x180F, 0xFEE0, 1 adet 128 bit" && det[5] == "Bağlanabilir: hayır");
  CHECK(det[6] == "Paket: 12 · ilk 9 sn önce · son şimdi");
  x.appear = 0x0C41;  // adı listede olmayan kategori
  det.clear();
  bleDetail(x, 10000, det);
  CHECK(det[4] == "Görünüm: listede yok (0x0C41)" && bleSub(x) == "Xiaomi · 00:00:00:00:00:09");
  x.appear = 0;
  x.mfr = 0xFFFF;
  CHECK(bleSub(x) == "Pil · 00:00:00:00:00:09");  // üretici yoksa bilinen ilk servis
  x.mfr = 0x7777;
  CHECK(bleSub(x) == "üretici 0x7777 · 00:00:00:00:00:09" && bleRow(3, x) == "3. -66 dBm  00:00:00:00:00:09  (adsız) · 0x7777");

  group("masaüstü: sistem menüsü ve hızlı geçiş");
  homeShow();
  openApp(AID_CALC);
  openApp(AID_CLOCK);
  openApp(AID_CALC);
  openApp(AID_NOTIFS);  // bildirimler listeye girmez
  CHECK(deskRecent[0] == &APP_CALC && deskRecent[1] == &APP_CLOCK);
  drag(160, 10, 160, 150);  // başlıktan aşağı: menü açılır, uygulama kapanmaz
  CHECK(ov.kind == OV_PANEL && curApp == &APP_NOTIFS);
  drag(160, 150, 160, 20);  // yukarı: kapanır
  CHECK(ov.kind == OV_NONE && curApp == &APP_NOTIFS);
  drag(160, 120, 160, 220);  // ortadan aşağı kaydırma menüyü açmaz
  CHECK(ov.kind == OV_NONE);
  sysMenuOpen();
  tap(6 + 78 + 30, SM_APP_Y + 30);  // ikinci son kullanılan: Saat
  CHECK(ov.kind == OV_NONE && curApp == &APP_CLOCK);
  sysMenuOpen();
  tap(6 + 78 * 2 + 30, SM_TILE_Y + 20);  // SD kutucuğu: Ayarlar → Depolama
  CHECK(ov.kind == OV_NONE && curApp == &APP_SETTINGS);
  sysMenuOpen();
  cfg.bright = 128;
  int br = cfg.bright;
  tap(300, SM_BRI_Y + 10);  // parlaklık sağa: menü açık kalır
  CHECK(ov.kind == OV_PANEL && cfg.bright > br);
  tap(260, SM_BTN_Y + 15);  // yeniden başlat: önce onay
  CHECK(ov.kind == OV_DIALOG);
  ov = Overlay();
  homeShow();
  tap(100, 10);  // ana ekranın durum çubuğu
  CHECK(ov.kind == OV_PANEL);
  ov = Overlay();
  homeShow();

  group("terminal bluetooth komutları");
  CmdOut t = termExec("bluetooth scan 2");
  CHECK(t.lines.size() == 1 && t.lines[0] == "Süre 3–30 sn olmalı");
  t = termExec("bluetooth scan");
  CHECK(t.lines.size() == 1 && t.lines[0].startsWith("Taranıyor (10 sn)"));
  std::vector<String> rep;
  CHECK(bsTakeDone() && !bsTakeDone());
  tlBtList(rep);
  CHECK(rep.size() == 6 && rep[0] == "4 cihaz, 6 paket (en güçlü sinyal önce):" && rep.back() == "Ayrıntı: bluetooth info <no>");
  CHECK(rep[1] == "  1. -58 dBm  C8:0F:10:4A:22:91  Mi Band 7 · Huami");
  CHECK(rep[2].indexOf("5E:33:A1:07:9C:4B  (adsız) · Apple") >= 0 && rep[3].indexOf("JBL Tune 520") >= 0);
  t = termExec("bluetooth info 3");
  CHECK(t.lines.size() >= 8 && t.lines[0] == "Ad: JBL Tune 520" && t.lines[4] == "Görünüm: Kulaklık (0x0941)");
  CHECK(t.lines[5] == "Servisler: Pil 0x180F" && t.lines[6] == "Yayın gücü: -8 dBm" && t.lines[7] == "Bağlanabilir: evet");
  CHECK(termExec("bluetooth info 9").lines[0].startsWith("Listede böyle bir cihaz yok"));
  CHECK(termExec("bluetooth info x").lines[0].startsWith("Listede böyle bir cihaz yok"));
  CHECK(termExec("bluetooth devices").lines.size() == 6 && termExec("bluetooth status").lines.size() == 4);
  bsRelease(true);
  CHECK(termExec("bluetooth devices").lines[0] == "Liste boş (önce: bluetooth scan)");
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
  testMigrate();
  testLog();
  testOtaRoots();
  testCmd();
  testNet();
  testBle();
  testAuth();
  printf("\n%d denetim geçti, %d hata\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
