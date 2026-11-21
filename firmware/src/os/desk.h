#pragma once
// Masaüstü: sistem menüsü, sistem alanı, hızlı geçiş ve bildirim alanı.
//  - Sistem menüsü: her ekranda üst kenardan aşağı kaydır ya da ana ekranın durum çubuğuna dokun.
//    Saat ve boş bellek, Wi-Fi / Bluetooth / SD / bulut kutucukları, parlaklık, son kullanılan
//    uygulamalar (hızlı geçiş), son bildirim, Terminal / Görevler / yeniden başlatma. Yukarı
//    kaydırınca, üst satıra dokununca ya da BOOT'a basınca kapanır.
//  - Sistem alanı: ana ekranın durum çubuğunda arka planda süren işlerin simgeleri (Aktarım, arka
//    plandaki Lua uygulaması, ağ ya da Bluetooth taraması, kronometre, telefondaki medya).
// Menü bir pencere (overlay) olarak çizilir: altındaki uygulama kapanmaz, Lua uygulaması çizmez.
static const App* deskRecent[4];

static void deskNoteOpen(const App* a) {  // appOpen'dan: yalnızca yerleşik uygulamalar
  bool builtin = false;
  for (int i = 0; i < appTableCount(); i++) builtin |= appTable()[i] == a;
  if (!builtin || a == appTable()[AID_NOTIFS]) return;
  int at = 3;
  for (int i = 0; i < 4; i++)
    if (deskRecent[i] == a) at = i;
  for (int i = at; i > 0; i--) deskRecent[i] = deskRecent[i - 1];
  deskRecent[0] = a;
}

struct DeskAct {
  uint8_t icon;
  const char* name;
};
static int deskActs(DeskAct* o) {
  int n = 0;
  if (webRunning()) o[n++] = {I_TRANSFER, "Aktarım"};
  if (kAlive(luaPid) && !luaFg) o[n++] = {I_CODE, "Lua"};
  if (ndBusy() || bsBusy()) o[n++] = {I_PULSE, "Tarama"};
  if (swRunning) o[n++] = {I_STOPWATCH, "Sayaç"};
  if (media.on && media.playing) o[n++] = {I_NOTE, "Medya"};
  return n;
}

// Durum çubuğundaki etkin işler (en fazla 3), sağdan sola; dönüş: kalan en sol x
static int deskDrawActs(int x) {
  DeskAct a[5];
  int n = std::min(3, deskActs(a));
  for (int i = 0; i < n; i++) {
    x -= 18;
    drawIcon(a[i].icon, x, 5, 14, C_ACC, C_BG);
  }
  return x;
}

static const int SM_TILE_Y = 34, SM_BRI_Y = 90, SM_APP_Y = 122, SM_NOTE_Y = 176, SM_BTN_Y = 206;

static void smTile(int i, uint8_t icon, const char* name, const String& val, bool on) {
  int x = 6 + i * 78;
  panel(x, SM_TILE_Y, 74, 50, on ? C_ACCBG : C_SURF, on ? C_ACC : C_LINE);
  drawIcon(icon, x + 6, SM_TILE_Y + 7, 14, on ? C_ACC : C_TEXT3, on ? C_ACCBG : C_SURF);
  text(name, x + 24, SM_TILE_Y + 14, F_XS, C_TEXT2, on ? C_ACCBG : C_SURF, ML_DATUM);
  text(fit(val, F_XS, 64), x + 8, SM_TILE_Y + 36, F_XS, on ? C_TEXT : C_TEXT3, on ? C_ACCBG : C_SURF, ML_DATUM);
}

static void sysMenuDraw() {
  fillBg(0, 0, SCR_W, SCR_H);
  struct tm t;
  char b[48];
  if (localNow(t)) snprintf(b, sizeof b, "%02d:%02d · %s %d %s", t.tm_hour, t.tm_min, GUN_ADI[t.tm_wday], t.tm_mday, AY_ADI[t.tm_mon]);
  else strcpy(b, "Saat ayarlanıyor…");
  text(b, 10, 14, F_SM, C_TEXT, C_BG, ML_DATUM);
  snprintf(b, sizeof b, "%u KB boş", (unsigned)(sysHeapFree() / 1024));
  text(b, 290, 14, F_XS, C_TEXT3, C_BG, MR_DATUM);
  drawIcon(I_UP, 296, 6, 16, C_TEXT3, C_BG);
  hline(0, 28, SCR_W);

  bool w = wifiConnected();
  smTile(0, w ? I_WIFI : I_WIFI_OFF, "Wi-Fi", w ? wifiSsid() : String("bağlı değil"), w);
  smTile(1, I_BT, "Telefon", bleLinked ? "bağlı" : bleHasToken ? "eşleşmiş" : "eşleşme yok", bleLinked);
  smTile(2, I_SD, "SD kart", sdOk ? "takılı" : "yok", sdOk);
  smTile(3, I_CLOUD, "Bulut", cloudOk ? (netLiveOpen() ? "canlı" : "bağlı") : netHold ? "duraklatıldı" : "bağlı değil", cloudOk);

  drawIcon(I_SUN, 8, SM_BRI_Y + 4, 16, C_TEXT3, C_BG);
  slider(36, SM_BRI_Y, 276, (cfg.bright - 16) * 100 / (255 - 16), C_BG);

  text("Son kullanılanlar", 10, SM_APP_Y, F_XS, C_TEXT3, C_BG, ML_DATUM);
  DeskAct act[5];
  int na = deskActs(act);
  if (na) {  // arka planda süren işler (sistem alanı)
    int x = 312;
    for (int i = na - 1; i >= 0; i--) {
      x -= textW(act[i].name, F_XS) + 24;
      drawIcon(act[i].icon, x, SM_APP_Y - 7, 14, C_ACC, C_BG);
      text(act[i].name, x + 17, SM_APP_Y, F_XS, C_TEXT2, C_BG, ML_DATUM);
    }
  }
  for (int i = 0; i < 4; i++) {
    const App* a = deskRecent[i];
    int x = 6 + i * 78;
    if (!a) {
      text(i ? "" : "Henüz yok", x + 4, SM_APP_Y + 26, F_XS, C_TEXT3, C_BG, ML_DATUM);
      continue;
    }
    panel(x, SM_APP_Y + 10, 74, 42, C_SURF, C_LINE);
    drawIcon(a->icon, x + 29, SM_APP_Y + 14, 16, C_TEXT, C_SURF);
    text(fit(a->label, F_XS, 68), x + 37, SM_APP_Y + 41, F_XS, C_TEXT2, C_SURF, MC_DATUM);
  }

  panel(6, SM_NOTE_Y, 308, 26, C_SURF, C_LINE);
  drawIcon(I_BELL, 12, SM_NOTE_Y + 5, 16, notifs.empty() ? C_TEXT3 : C_ACC, C_SURF);
  String n = notifs.empty() ? String("Bildirim yok") : notifs[0].title + ": " + notifs[0].text;
  if (notifs.size() > 1) n = "(" + String(notifs.size()) + ") " + n;
  text(fit(n, F_XS, 276), 34, SM_NOTE_Y + 13, F_XS, notifs.empty() ? C_TEXT3 : C_TEXT, C_SURF, ML_DATUM);

  button(6, SM_BTN_Y, 84, 30, "Terminal", B_NORMAL, I_NONE, C_BG);
  button(96, SM_BTN_Y, 84, 30, "Görevler", B_NORMAL, I_NONE, C_BG);
  button(186, SM_BTN_Y, 128, 30, "Yeniden başlat", B_NORMAL, I_NONE, C_BG);
}

static void sysMenuClose() {
  ov = Overlay();
  shellRedraw(true);
}

static void sysMenuTap(int x, int y) {
  if (y < 30) {
    sysMenuClose();
    return;
  }
  if (y >= SM_BRI_Y && y < SM_BRI_Y + 26) {
    cfg.bright = 16 + sliderVal(x, 36, 276) * (255 - 16) / 100;
    sysSetBacklight(cfg.bright);
    saveConfig();
    slider(36, SM_BRI_Y, 276, (cfg.bright - 16) * 100 / (255 - 16), C_BG);
    return;
  }
  int col = constrain((x - 6) / 78, 0, 3);
  ov = Overlay();  // aşağıdaki her seçim menüyü kapatır
  if (y >= SM_TILE_Y && y < SM_TILE_Y + 52) {
    static const uint8_t PAGE[] = {1, 2, 5, 4};  // Ayarlar: Wi-Fi, Bluetooth, Depolama, Bulut hesabı
    appFromHome = shScreen == SH_HOME;
    settingsOpenPage(PAGE[col]);
  } else if (y >= SM_APP_Y + 8 && y < SM_APP_Y + 52 && deskRecent[col]) {
    appOpen(deskRecent[col]);
  } else if (y >= SM_NOTE_Y && y < SM_NOTE_Y + 28) {
    openApp(AID_NOTIFS);
  } else if (y >= SM_BTN_Y - 2) {
    int i = x < 93 ? 0 : x < 183 ? 1 : 2;
    if (i == 2) {
      shellRedraw(true);
      dialog("Yeniden başlatılsın mı?", "Kaydedilmemiş veriler kaybolabilir.", {"Vazgeç", "Başlat"}, [](int c) {
        if (c == 1) sysRestart();
      }, 1);
    } else {
      openApp(i ? AID_TASKS : AID_TERMINAL);
    }
  } else {
    shellRedraw(true);
  }
}

static void sysMenuOpen() {
  if (kbm.active || ringOn) return;
  ov = Overlay();
  ov.kind = OV_PANEL;
  ov.pdraw = sysMenuDraw;
  ov.ptap = sysMenuTap;
  toastUntil = bannerUntil = 0;
  sysMenuDraw();
}
