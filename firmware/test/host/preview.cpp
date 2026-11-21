// DAXEN VANE arayüz önizlemesi (bilgisayarda): gerçek arayüz kodunu sahte ekranla derler,
// her senaryonun ekranını out/ klasörüne yazar. Senaryo adı verilirse yalnız o çalışır.
#include "host_sys.h"
#include <map>

int main(int argc, char** argv) {
  setupState();
  std::string only = argc > 1 ? argv[1] : "";
  auto want = [&](const char* n) { return only.empty() || only == n; };

  if (want("logo")) {
    fillBg(0, 0, 320, 240);
    DRAW_LOGO(LOGO_L, (320 - LOGO_L_W) / 2, 78);
    tft.fillRect(130, 176, 60, 2, C_LINE);
    tft.fillRect(130, 176, 24, 2, C_ACC);
    shot("00-acilis");
  }
  if (want("icons")) {
    fillBg(0, 0, 320, 240);
    for (int i = 1; i < I_COUNT; i++) {
      int x = ((i - 1) % 11) * 29 + 4, y = ((i - 1) / 11) * 29 + 4;
      drawIcon(i, x, y, 24, C_TEXT, C_BG);
    }
    shot("01-simgeler");
    fillBg(0, 0, 320, 240);
    for (int i = 1; i < I_COUNT; i++) {
      int x = ((i - 1) % 16) * 20 + 2, y = ((i - 1) / 16) * 22 + 4;
      drawIcon(i, x, y, 16, C_TEXT, C_BG);
    }
    shot("01-simgeler16");
  }
  if (want("home")) {
    homeShow();
    for (int i = 0; i < 30; i++) step(70);
    shot("10-ana");
    swItemId = "10:10";
    swDers = "Kimya";
    swKonu = "Mol kavramı";
    swDk = 30;
    swRunning = true;
    swStart = g_ms - 754000;
    homeShow();
    step(1000);
    shot("11-ana-kronometre");
    swRunning = false;
    swAccMs = 0;
    swItemId = "";
    uiNotif(notifs[0]);
    shot("12-ana-bildirim");
    bannerUntil = 0;
    homeShow();
    toast("Telefonla eşleşti");
    shot("13-ana-balon");
    toastUntil = 0;
    // sistem menüsü: son kullanılanlar, arka planda Aktarım, bildirim
    openApp(AID_WEATHER); openApp(AID_FILES); openApp(AID_TERMINAL); openApp(AID_NOTES);
    webStart();
    homeShow();
    shot("15-ana-sistem-alani");
    drag(160, 8, 160, 140);
    shot("16-sistem-menusu");
    webStop();
    ov = Overlay();
    // Müzikten kalan modül dosyası bir kez sorulur; "Sil" dosyayı siler, sonra bir daha sorulmaz
    homeShow();
    fOfferOldMusicModule();
    shot("14-eski-muzik-modulu");
    if (ov.kind != OV_DIALOG || !SD.exists(OLD_MUSIC_FILE)) { fprintf(stderr, "eski müzik modülü sorulmadı\n"); exit(1); }
    auto cb = ov.cb;
    ov = Overlay();
    cb(1);
    toastUntil = 0;
    homeShow();
    fOfferOldMusicModule();
    if (SD.exists(OLD_MUSIC_FILE) || ov.kind != OV_NONE) { fprintf(stderr, "eski müzik modülü silinmedi ya da yine soruldu\n"); exit(1); }
  }
  if (want("launcher")) {
    launcherShow();
    shot("20-uygulamalar");
    launcherSwipe(SW_LEFT);
    shot("21-uygulamalar-2");
    lnPage = 0;
  }
  if (want("yks")) {
    yksTab = 0;
    appOpen(&APP_YKS);
    shot("30-yks-plan");
    hold(150, HDR_H + 2 * YKS_ROW_H + 20);
    shot("31-yks-menu");
    ov = Overlay();
    yksSetTab(1);
    shot("32-yks-sayac");
    selectForTimer(view.items[2]);
    timerToggleRun();
    step(1500);
    yksDraw(true);
    shot("33-yks-sayac-calisiyor");
    timerReset();
    focusSet(true);
    yksDraw(true);
    shot("33b-yks-odak");
    yksSetTab(2);
    sqLoad();
    for (int d = 13; d >= 1; d--) sq.last[d] = (d * 37) % 140;
    sqDers = 1; yksSoruTap(100, 120); yksSoruTap(160, 120); yksSoruTap(220, 120);  // +1 +5 +10 Matematik
    sqDers = 3; yksSoruTap(280, 120);                                               // +20 Fizik
    yksDrawSoru();
    shot("34-yks-soru");
    yksSoruTap(250, 44);
    shot("34b-yks-soru-ders");
    yksSoruTap(20, 10 + 124);
    yksSetTab(3);
    shot("35-yks-ozet");
    {  // kalıcı kayıt: son 80 günün kronometre oturumları
      struct tm t; localNow(t);
      time_t now = time(nullptr);
      sdMkdirs("/calisma");
      std::map<std::string, std::string> files;
      for (int i = 0; i < 80; i++) {
        time_t d = now - (time_t)i * 86400; struct tm dt; localtime_r(&d, &dt);
        int mins = (i * 53 + 17) % 200; if (i % 9 == 4) mins = 0;
        if (!mins) continue;
        char date[16], fn[48], line[160];
        strftime(date, sizeof date, "%Y-%m-%d", &dt);
        snprintf(fn, sizeof fn, "/calisma/kayit-%04d-%02d.txt", dt.tm_year + 1900, dt.tm_mon + 1);
        snprintf(line, sizeof line, "{\"type\":\"session\",\"date\":\"%s\",\"at\":1,\"sec\":%d,\"ders\":\"Matematik\"}\n", date, mins * 60);
        files[fn] += line;
        files[fn] += "{\"type\":\"complete\",\"itemId\":\"x\",\"date\":\"" + std::string(date) + "\"}\n";
      }
      for (auto& f : files) sdWrite(f.first.c_str(), f.second.c_str());
    }
    yksTap(100, 140);
    shot("36-yks-gecmis");
    yksBack();
    focusSet(false);
  }
  if (want("settings")) {
    appOpen(&APP_SETTINGS);
    shot("40-ayarlar");
    setGo(SP_WIFI);
    shot("41-wifi");
    tap(100, WIFI_LIST_Y + 2 * WIFI_ROW + 10);
    shot("42-wifi-sifre");
    for (const char* k : {"s", "i", "f", "r", "e"}) {
      (void)k;
    }
    kbm.value = "gizli123";
    kbm.cur = kbm.value.length();
    kbmDraw();
    shot("42b-wifi-sifre-yazili");
    kbmFinish(false);
    setGo(SP_BT);
    shot("43-bluetooth");
    setGo(SP_DISPLAY);
    shot("44-ekran");
    setGo(SP_ACCOUNT);
    shot("45-hesap");
    setGo(SP_STORAGE);
    shot("46-depolama");
    setGo(SP_UPDATE);
    shot("47-guncelleme");
    setGo(SP_SYSTEM);
    shot("48-sistem");
    authSetPin("2468");
    cfg.lockOn = true;
    authDevOn();
    setGo(SP_SECURITY);
    shot("49-guvenlik");
    authDevOff();
    appExit();
    homeShow();
    lockNow();
    shot("49a-kilitli");
    tap(160, 120);
    kbm.value = "24";
    kbm.cur = 2;
    kbmDraw();
    shot("49b-kilit-pin");
    kbm.value = "2468";
    kbmFinish(true);
    authClearPin();
    cfg.lockOn = false;
  }
  if (want("kb")) {
    textInput("Not başlığı", "", nullptr);
    kbm.kb.layer = 1;
    kbmDraw();
    shot("49-klavye-123");
    kbm.kb.layer = 2;
    kbmDraw();
    shot("49b-klavye-isaret");
    kbmFinish(false);
  }
  if (want("phone")) {
    appOpen(&APP_NOTIFS);
    shot("50-bildirimler");
    tap(100, HDR_H + 20);
    shot("51-bildirim");
    appOpen(&APP_MEDIA);
    shot("52-medya");
  }
  if (want("files")) {
    appOpen(&APP_FILES);
    shot("60-dosyalar");
    hold(100, HDR_H + 20);
    shot("61-dosya-menu");
    ov = Overlay();
    dialog("Klasör silinsin mi?", "notlar ve içindeki her şey silinecek.", {"Vazgeç", "Sil"}, nullptr);
    shot("62-iletisim");
    ov = Overlay();
  }
  if (want("games")) {
    appOpen(&APP_GAMES);
    shot("70-oyunlar");
    appOpenChild(&APP_BLOKPATLAT);
    shot("71-blokpatlat");
  }
  if (want("s2")) {
    weather.feels = 7.2f;
    weather.hum = 81;
    weather.wind = 12.4f;
    weather.isDay = true;
    weather.code = 61;
    weather.hours = 24;
    weather.hHour0 = 22;
    for (int i = 0; i < 24; i++) {
      weather.hTemp[i] = (int8_t)(9 + 4 * sin((i - 14) / 24.0 * 6.28));
      weather.hRain[i] = i > 8 && i < 14 ? 60 : 0;
      weather.hCode[i] = 3;
    }
    weather.days = 7;
    int codes[] = {61, 3, 0, 2, 80, 71, 0};
    for (int i = 0; i < 7; i++) {
      weather.dWday[i] = (5 + i) % 7;
      weather.dMax[i] = 12 - i;
      weather.dMin[i] = 4 - i / 2;
      weather.dCode[i] = codes[i];
      weather.dRain[i] = i == 0 ? 70 : (i == 4 ? 40 : 0);
    }
    weatherAt = g_ms - 5 * 60000;
    appOpen(&APP_WEATHER);
    shot("80-hava");
    appOpen(&APP_CALC);
    for (const char* k : {"sin", "3", "0", ")", "×", "2", "^", "3", "+", "√", "2"}) calcKey(k);
    calcDraw(false);
    shot("81-hesap");
    calcKey("=");
    shot("82-hesap-sonuc");
    appOpen(&APP_TODO);
    todos = {TodoItem("Fizik denemesi çöz", false), TodoItem("Kütüphaneye kitabı götür", true), TodoItem("Matematik türev tekrarı", false)};
    todoLoaded = true;
    tdDraw(true);
    shot("83-yapilacaklar");
    alarmN = 2;
    alarms[0].h = 6; alarms[0].m = 45; alarms[0].days = 0x1F;
    alarms[1].h = 9; alarms[1].m = 30; alarms[1].days = 0x60; alarms[1].on = false;
    alarmsLoaded = true;
    clkTab = 0;
    appOpen(&APP_CLOCK);
    shot("84-alarm");
    tap(100, HDR_H + 20);
    shot("85-alarm-duzenle");
    clkBack();
    clkSetTab(1);
    shot("86-sayac");
    clkSetTab(2);
    cwRunning = true; cwStart = g_ms - 83400; cwLaps = {20100, 45500};
    clkDrawStopwatch();
    shot("87-kronometre");
    cwRunning = false;
    clkSetTab(3);
    shot("88-dunya");
    ringStartNow("Alarm 06:45");
    shot("89-alarm-caliyor");
    ringOn = false;
    appOpen(&APP_TRANSFER);
    shot("90-aktarim");
    // notlar
    { sdMkdirs("/daxen/notlar"); sdWrite("/daxen/notlar/Alışveriş.txt", "Alışveriş listesi\nsüt, ekmek, peynir\nfotokopi: fizik notları\n\nYarın 10:00 kütüphane"); }
    appOpen(&APP_NOTES);
    shot("91-notlar");
    edOpen("/tmp-yok.txt");
    edText = "Türev kuralları\nf(x) = xⁿ ise f'(x) = n·xⁿ⁻¹. Çarpımın türevi: (uv)' = u'v + uv'. Bölümün türevi ise (u/v)' = (u'v − uv')/v² olur. Zincir kuralı en çok karıştırılan kısım.";
    edCur = edText.length();
    edKb = true;
    edDrawAll();
    shot("92-not-metin");
    edKb = false;
    edDrawAll();
    shot("93-not-metin-klavyesiz");
    ntsMode = 0;
    ntsPath = "/daxen/notlar/El.el";
    hwStrokes.clear(); hwPts.clear();
    hwOpen("/yok.el");
    // el yazısı benzeri çizgiler
    for (int k = 0; k < 3; k++) {
      shellTouch(true, 30 + k * 60, 80);
      hwRaw(TE_DOWN, 30 + k * 60, 80);
      for (int i = 1; i < 30; i++) hwRaw(TE_MOVE, 30 + k * 60 + i * 2, 80 + (int)(12 * sin(i / 3.0)));
      hwRaw(TE_UP, 0, 0);
    }
    shot("94-not-el");
    ntsMode = 0;
  }
  if (want("s4")) {
    appOpen(&APP_GAMES);
    shot("a0-oyunlar");
    appOpenChild(&APP_SNAKE);
    snDraw(true);
    for (int i = 0; i < 6; i++) { snPaused = false; snStep(); }
    snFood = 10 * SN_W + 20; snDrawField(); snDrawScore();
    shot("a1-yilan");
    appExit();
    appOpenChild(&APP_2048);
    uint8_t b[16] = {1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 1, 10, 11, 2, 1};
    memcpy(g2b, b, 16); g2Loaded = true; g2Score = 3480; g2Best = 9120;
    g2Draw(true);
    shot("a2-2048");
    appExit();
    appOpenChild(&APP_MINES);
    mnNew(); mnOpenAt(5 * MN_W + 6); mnToggleFlag(0);
    mnDraw(true);
    shot("a3-mayin");
    appExit();
    appOpenChild(&APP_SUDOKU);
    sdkNew(1); sdkLoaded = true; sdkSel = 40; sdkCur[sdkGiven[0] ? 1 : 0] = 5;
    sdkDraw(true);
    shot("a4-sudoku");
    appExit();
    appOpenChild(&APP_CHESS);
    chInit(chP); chLoaded = true; chHuman = 1;
    ChMove m{52, 36, 0, 0}; chApply(m);
    ChMove m2{12, 28, 0, 0}; chApply(m2);
    chSel = 62; chDraw(true);
    shot("a5-satranc");
    { uint32_t t0 = clock(); ChMove bm; chNodeLimit = 20000; chBest(chP, 2, 0, bm); printf("satranç orta: %u düğüm, %ld ms (bilgisayarda)\n", chNodes, (clock() - t0) * 1000 / CLOCKS_PER_SEC); }
    appExit();
    { std::string t; for (int i = 0; i < 30; i++) t += "1919 senesi Mayısının 19 uncu günü Samsun'a çıktım. Vaziyet ve manzarai umumiye: Osmanlı Devletinin dahil bulunduğu grup, Harbi Umumide mağlup olmuş, Osmanlı ordusu her tarafta zedelenmiş, şeraiti ağır bir mütarekename imzalanmıştı. "; sdMkdirs("/daxen/kitaplar"); sdWrite("/daxen/kitaplar/Nutuk (ilk bölüm).txt", t.c_str()); }
    appOpen(&APP_READER);
    shot("b0-okuyucu-liste");
    tap(100, HDR_H + 15);
    shot("b1-okuyucu-sayfa");
    rdBar = true; rdShowPage();
    shot("b2-okuyucu-menu");
    rdBar = false;
    appOpen(&APP_CARDS);
    shot("b3-kartlar");
    kdOpenDeck(1); kdCur = 3; kdDraw(true);
    shot("b4-kart-on");
    kdFlip = true; kdDrawCard();
    shot("b5-kart-arka");
    appOpen(&APP_LIGHT);
    for (int i = 0; i < 60; i++) { g_ldr = 1800 + 900 * sin(i / 8.0); g_ms += 520; lmTick(); }
    shot("b6-isik");
    appOpen(&APP_DRAW);
    {
      auto drag = [](int tool, int x0, int y0, int x1, int y1, int steps) {
        drTool = tool;
        drRaw(TE_DOWN, x0, y0);
        for (int i = 1; i <= steps; i++) {
          g_ms += 40;
          drRaw(TE_MOVE, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
        }
        g_ms += 40;
        drRaw(TE_UP, x1, y1);
      };
      drEdit("/daxen/cizim/Manzara.cizim");
      drDoc.flags |= DRF_GRID;
      drSize = 1; drColor = 14; drag(DT_RECTF, 0, 160, 320, 240, 3);       // deniz
      drColor = 8; drag(DT_OVALF, 230, 50, 290, 110, 3);                     // güneş
      drSize = 3; drColor = 7; drag(DT_OVAL, 220, 40, 300, 120, 3);
      drColor = 10; drSize = 4; drTool = DT_PEN;                             // tepe
      drRaw(TE_DOWN, 10, 160);
      for (int i = 1; i <= 60; i++) drRaw(TE_MOVE, 10 + i * 3, 160 - (int)(45 * sin(i / 19.0)));
      drRaw(TE_UP, 190, 160);
      drColor = 0; drSize = 2; drag(DT_LINE, 40, 60, 100, 61, 4);            // 45°'ye yaslanır
      drag(DT_RECT, 120, 120, 180, 158, 3);                                  // ev
      drColor = 6; drag(DT_LINE, 120, 120, 150, 96, 2);
      drag(DT_LINE, 150, 96, 180, 120, 2);
      drMirror = true; drColor = 17; drSize = 6; drTool = DT_PEN;           // ayna: iki kuş
      drRaw(TE_DOWN, 40, 200);
      for (int i = 1; i <= 20; i++) drRaw(TE_MOVE, 40 + i * 2, 200 - (int)(10 * sin(i / 6.0)));
      drRaw(TE_UP, 80, 200);
      drMirror = false;
      drColor = 0; drSize = 6; drAddText(20, 100, "Merhaba DAXEN");
      drToolbar(); drPaintAll();
      shot("b7-cizim");
      drPanel = 1; drToolbar();
      shot("b7a-cizim-araclar");
      drClosePanel(); drPanel = 2; drToolbar();
      shot("b7b-cizim-renk");
      drClosePanel();
      String out;
      drExport(drDoc, "Manzara", out);
      drBack();
      drEdit("/daxen/cizim/Not.cizim");
      drDoc.paper = DR_PAPERS[2]; drColor = 4; drSize = 3;
      drag(DT_RECT, 30, 50, 290, 220, 2);
      drAddText(40, 70, "Alışveriş: süt, ekmek");
      drBack();
      shot("b7c-cizim-liste");
    }
    appOpen(&APP_PHOTOS);
    shot("b8-foto-liste");
    phIdx = 0; phShow(); phBar = true; phDrawBar();
    shot("b9-foto");
    phIdx = -1;
  }
  if (want("s5")) {
    sdRemoveTree("/daxen/uygulamalar");
    launcherShow();
    lnPage = lnPages() - 1; launcherDraw(true);
    shot("d0-uygulamalar-lua");
    launcherHold(10, HDR_H + 10);  // bir Lua uygulamasına uzun basış
    shot("d0a-lua-menu");
    ov = Overlay();
    launcherDraw(true);
    for (int i = 0; i < luaAppCount(); i++) printf("lua: %s\n", luaAppLabel(i).c_str());
    auto byName = [&](const char* n) { for (int i = 0; i < luaAppCount(); i++) if (luaAppLabel(i) == n) return i; return 0; };
    luaAppOpen(byName("Merhaba"));
    shot("d0b-lua-izin");
    luaOpenOk(byName("Merhaba"));
    tap(160, 120); tap(160, 120); tap(160, 120); step(40);
    shot("d1-lua-merhaba");
    luaOpenOk(byName("Zar"));
    tap(160, 120); step(500);
    shot("d2-lua-zar");
    luaOpenOk(byName("Sayaç"));
    tap(250, 190); tap(250, 190); tap(250, 190); tap(60, 190); step(40);
    shot("d3-lua-sayac");
    luaOpenOk(byName("Top"));
    drag(100, 200, 200, 80); step(600);
    shot("d4-lua-top");
    printf("lua bellek: %u / %u\n", (unsigned)luaMem, (unsigned)luaMemMax);
    FILE* f = fopen((g_sdroot + "/daxen/uygulamalar/hatali.lua").c_str(), "w");
    fputs("-- ad: Hatalı örnek\nfunction ciz()\n  ekran.temizle()\n  local t = nil\n  ekran.yazi(t.ad, 10, 40)\nend\n", f);
    fclose(f);
    luaScanned = false;
    luaOpenOk(byName("Hatalı örnek"));
    step(40);
    shot("d5-lua-hata");
    f = fopen((g_sdroot + "/daxen/uygulamalar/dongu.lua").c_str(), "w");
    fputs("-- ad: Döngü\nfunction dokun() while true do end end\n", f);
    fclose(f);
    luaScanned = false;
    luaOpenOk(byName("Döngü"));
    g_ms_hook = true; tap(100, 100); step(40); g_ms_hook = false; step(40);
    shot("d6-lua-zamanasimi");
    appExit();
    luaScanned = false;
    luaOpenOk(byName("Zamanlayıcı"));
    step(40); tap(160, 190); step(40);
    for (int i = 0; i < 3; i++) step(1000);
    shot("d7-lua-zamanlayici");
    appExit();
  }
  if (want("magaza")) {  // mağaza ve mağazadaki Lua uygulamaları
    appOpen(&APP_STORE);
    shot("h0-magaza");
    for (int r = 0; r < (int)stApps.size() && r < ST_ROWS; r++) stTap(270, HDR_H + r * ST_ROW + 20);
    stDraw(true);
    shot("h1-magaza-kurulu");
    luaScanned = false;
    auto byName = [&](const char* n) { for (int i = 0; i < luaAppCount(); i++) if (luaAppLabel(i) == n) return i; return 0; };
    luaOpenOk(byName("Pong"));
    step(2500);
    shot("h2-pong");
    luaOpenOk(byName("Nefes"));
    step(2600);
    shot("h3-nefes");
    luaOpenOk(byName("Deneme"));
    step(200);
    tap(150, 88); step(80); tap(118, 204); step(80); tap(222, 204); step(80);
    shot("h4-deneme-giris");
    tap(274, 230); step(80);
    tap(200, 88); step(80); tap(66, 204); step(80); tap(274, 230); step(80);
    tap(150, 116); step(80); tap(170, 204); step(80); tap(274, 230); step(80);
    tap(150, 144); step(80); tap(14, 204); step(80); tap(222, 204); step(80); tap(274, 230); step(80);
    tap(220, 45); step(200);
    tap(150, 88); step(80); tap(14, 204); step(80); tap(14, 230); step(80); tap(274, 230); step(80);
    tap(220, 45); step(200);
    tap(280, 45); step(200);
    shot("h5-deneme-gecmis");
    appExit();
    homeShow();
  }
  if (want("gizli")) {  // gizli özellikler (docs/gizli.md)
    const char* f[] = {"vane.lua", "uyanis.lua", "hayalet.lua", "radar.lua", "bilinmeyen.lua"};
    const char* n[] = {"g1-project-vane", "g2-developer-awakening", "g3-ghost-protocol", "g4-dark-radar", "g5-the-unknown"};
    const int ms[] = {9000, 8000, 2000, 6200, 9000};
    for (int i = 0; i < 5; i++) {
      homeShow();
      gizliAc(f[i]);
      step(ms[i]);
      shot(n[i]);
      appExit();
    }
    homeShow();
  }
  if (want("kal")) {
    calScreen(CAL_PX[0], CAL_PY[0], "Dokunmatik ayarı", "Artının tam ortasına dokun, kısa bir an tut", 0, 9, "");
    shot("f0-kalibrasyon-1");
    calScreen(CAL_PX[4], CAL_PY[4], "Dokunmatik ayarı", "Artının tam ortasına dokun, kısa bir an tut", 4, 9, "Biraz daha uzun ve sabit tut");
    shot("f1-kalibrasyon-5");
    calScreen(CAL_PX[8], CAL_PY[8], "Dokunmatik ayarı", "Artının tam ortasına dokun, kısa bir an tut", 8, 9, "");
    shot("f2-kalibrasyon-9");
    calScreen(CAL_VX[0], CAL_VY[0], "Doğrulama", "Artının tam ortasına dokun, kısa bir an tut", 0, 2, "");
    shot("f3-dogrulama");
    calResult(1.4f, 1.1f, 2.6f, true);
    shot("f4-sonuc-iyi");
    calResult(7.9f, 4.2f, 11.5f, false);
    shot("f5-sonuc-kotu");
  }
  if (want("s6")) {
    // Çekirdek süreç tablosu (önizlemede kesme yok: istatistikler örnek)
    auto nop = []() {};
    const char* names[] = {"dokunma", "kabuk", "bluetooth", "saat", "ışık"};
    uint16_t per[] = {10, 16, 10, 200, 50};
    uint16_t cpu[] = {21, 64, 12, 3, 4};
    uint32_t runs[] = {512340, 320112, 512044, 25602, 102411};
    for (int i = 0; i < 5; i++) {
      Proc* p = kFind(kService(names[i], per[i], nop));
      p->cpu10 = cpu[i]; p->runs = runs[i]; p->lastUs = 40 + i * 70; p->maxUs = 1800 + i * 900; p->totalUs = (uint64_t)runs[i] * 120;
      p->startedMs = 0;
    }
    Proc* n = kFind(kAdopt("ağ", nullptr, 1, 0, 16384)); n->startedMs = 0;
    int lp = kSpawn("lua:zamanlayici", [](Proc*) {}, 14 * 1024, true);
    Proc* l = kFind(lp); l->done = false; l->cpu10 = 9; l->runs = 3120; l->lastUs = 310; l->maxUs = 5200; l->totalUs = 960000; l->startedMs = g_ms - 754000;
    kLoad10 = 114;
    appOpen(&APP_TASKS);
    shot("e0-gorevler");
    tap(160, TM_Y + 1 * TM_RH + 10);
    shot("e1-gorev-servis");
    tmBack();
    tmTop = 9; tmDraw(false);
    { int ix = 0; for (size_t i = 0; i < tmPids.size(); i++) if (tmPids[i] == lp) ix = i; tap(160, TM_Y + (ix - tmTop) * TM_RH + 10); }
    shot("e2-gorev-lua");
    tap(160, SCR_H - 24);
    shot("e3-gorev-sonlandir");
    ov = Overlay();
    appOpen(&APP_TERMINAL);
    tlRun("sysinfo");
    shot("e5-terminal");
    ov = Overlay();
    ndConsented = true;
    tlRun("wifi ping 192.168.1.1 4");
    tlTick();
    shot("e6-terminal-ping");
    appOpen(&APP_NETWORK);
    shot("e7-ag-baglanti");
    nwTap(160, 215);  // bağlantıyı test et
    nwTick();
    shot("e8-ag-test");
    nwTap(160, HDR_H + 12);  // Wi-Fi sekmesi
    nwTap(160, 215);
    nwTick();
    shot("e9-ag-tarama");
    nwTap(264, HDR_H + 12);  // Bluetooth sekmesi
    nwTap(160, 215);         // tara
    nwTick();
    shot("e10-ag-bluetooth");
    nwTap(160, NW_Y + NW_AP_H * 2 + 10);  // üçüncü cihaz: ayrıntı
    shot("e11-ag-bt-ayrinti");
    appOpen(&APP_TERMINAL);
    tlRun("bluetooth scan 5");
    g_ms += 200;
    tlTick();
    shot("e12-terminal-bt");
    ov = Overlay();
    // satranç motoru süreçte: insan hamlesi → düşünme → yanıt
    appOpen(&APP_GAMES);
    appOpenChild(&APP_CHESS);
    chHuman = 1; chNewGame(); chDraw(true);
    auto sqXY = [](int sq, int& x, int& y) { int v = chView(sq); x = CH_X + (v % 8) * CH_SQ + 10; y = CH_Y + (v / 8) * CH_SQ + 10; };
    int x, y;
    sqXY(52, x, y); tap(x, y);
    sqXY(36, x, y); tap(x, y);
    printf("satranç: hamle sonrası meşgul=%d sıra=%d\n", (int)chAiBusy, chP.side);
    step(100);
    printf("satranç: yanıt sonrası meşgul=%d sıra=%d geçmiş=%d son=%d-%d\n", (int)chAiBusy, chP.side, (int)chHist.size(), chLastFrom, chLastTo);
    shot("e4-satranc-yanit");
  }
  printf("tamam\n");
  return 0;
}
