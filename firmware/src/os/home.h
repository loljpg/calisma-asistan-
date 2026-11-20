#pragma once
// Ana ekran, başlatıcı (uygulama ızgarası), dokunma dağıtımı ve sistemin çağırdığı arayüz kancaları.
#include "shell.h"
#include "cat.h"

static const App* const* appTable();  // os/registry.h
static int appTableCount();
static int luaAppCount();             // SD karttaki Lua uygulamaları (apps/lua.h)
static String luaAppLabel(int i);
static void luaAppOpen(int i);
static void closeStack();

// ============================================================================
// Ana ekran
// ============================================================================
static const int DOCK_Y = 192, NOW_Y = 134, NOW_H = 52;
static const uint8_t DOCK[] = {AID_YKS, AID_NOTES, AID_MUSIC, AID_SETTINGS};
static bool homeNotifIcons = false;

static void homeDrawStatus() {
  fillBg(0, 0, SCR_W, 24);
  struct tm t;
  String d = localNow(t) ? String(GUN_ADI[t.tm_wday]) + ", " + String(t.tm_mday) + " " + AY_ADI[t.tm_mon] : String("Saat ayarlanıyor…");
  text(d, 12, 13, F_SM, C_TEXT2, C_BG, ML_DATUM);
  int x = SCR_W - 10;
  bool wifi = wifiConnected();
  x -= 16;
  drawIcon(wifi ? I_WIFI : I_WIFI_OFF, x, 4, 16, wifi ? C_TEXT2 : C_TEXT3, C_BG);
  bool pairing = blePairUntil && (int32_t)(blePairUntil - millis()) > 0;
  if (bleLinked || pairing || bleHasToken) {
    x -= 20;
    drawIcon(I_BT, x, 4, 16, pairing ? C_ACC : (bleLinked ? C_TEXT2 : C_TEXT3), C_BG);
  }
  if (!notifs.empty()) {
    String n = String(notifs.size());
    int w = textW(n, F_XS);
    x -= 22 + w;
    drawIcon(I_BELL, x, 4, 16, C_TEXT, C_BG);
    text(n, x + 18, 13, F_XS, C_ACC, C_BG, ML_DATUM);
  }
}

static void homeDrawClock() {
  bigText(clockStr(), 8, 28, 206, 50, C_TEXT, C_BG, TL_DATUM);
}

static void homeDrawWeather() {
  fillBg(214, 26, 106, 70);
  Weather w;
  {
    Lock l;
    w = weather;
  }
  if (w.valid) {
    drawIcon(weatherIconId(w.code, w.isDay), 218, 32, 28, C_TEXT2, C_BG);
    text(String((int)roundf(w.temp)) + "°", 310, 30, F_LG, C_TEXT, C_BG, TR_DATUM);
    text(fit(weatherText(w.code), F_XS, 96), 310, 64, F_XS, C_TEXT2, C_BG, TR_DATUM);
  } else {
    drawIcon(I_CLOUD, 218, 32, 28, C_TEXT3, C_BG);
    text("–", 310, 30, F_LG, C_TEXT3, C_BG, TR_DATUM);
  }
  text(fit(cfg.wCity, F_XS, 96), 310, 79, F_XS, C_TEXT3, C_BG, TR_DATUM);
}

static void homeDrawInfo() {
  fillBg(0, 80, 212, 20);
  int exam, streak;
  {
    Lock l;
    exam = view.valid ? view.examDays : -1;
    streak = view.valid ? view.streak : 0;
  }
  int x = 12;
  if (exam >= 0) {
    String s = exam == 0 ? String("Sınav bugün") : "YKS'ye " + String(exam) + " gün";
    text(s, x, 90, F_SM, C_TEXT2, C_BG, ML_DATUM);
    x += textW(s, F_SM) + 14;
  }
  if (streak > 0) {
    drawFlame(x, 82, 16, C_BG);
    text(String(streak), x + 18, 90, F_SM, C_TEXT, C_BG, ML_DATUM);
  }
}

static void homeDrawNow() { yksNowCard(8, NOW_Y, 304, NOW_H); }

static void homeDrawDock() {
  fillBg(0, DOCK_Y, SCR_W, SCR_H - DOCK_Y);
  hline(0, DOCK_Y, SCR_W);
  const App* const* tbl = appTable();
  for (int i = 0; i < 5; i++) {
    int cx = i < 4 ? 30 + i * 60 : 280;
    uint8_t icon = i < 4 ? tbl[DOCK[i]]->icon : I_APPS;
    const char* lb = i < 4 ? tbl[DOCK[i]]->label : "Uygulamalar";
    drawIcon(icon, cx - 11, DOCK_Y + 6, 22, i == 4 ? C_ACC : C_TEXT, C_BG);
    text(lb, cx, DOCK_Y + 36, F_XS, C_TEXT2, C_BG, MC_DATUM);
  }
}

static void homeDraw(bool full) {
  if (full) fillBg(0, 0, SCR_W, SCR_H);
  homeDrawStatus();
  homeDrawClock();
  homeDrawWeather();
  homeDrawInfo();
  homeDrawNow();
  if (full) {
    catDrawAll();
    homeDrawDock();
  }
}

static void homeTap(int x, int y) {
  if (y < 24) {
    if (x > 200) openApp(AID_NOTIFS);
    return;
  }
  if (y < 100) {
    if (x >= 214) openApp(AID_WEATHER);
    else if (x < 210) openApp(AID_CLOCK);
    return;
  }
  if (y < NOW_Y - 2) {
    catPet(x);
    return;
  }
  if (y < DOCK_Y) {
    openApp(AID_YKS);
    return;
  }
  int i = x >= 240 ? 4 : constrain(x / 60, 0, 3);
  if (i < 4) {
    appFromHome = true;
    appOpen(appTable()[DOCK[i]], true);
  } else {
    launcherShow();
  }
}

// ============================================================================
// Başlatıcı: 4×3 ızgara, sayfalar (kaydır ya da başlıktaki oklar)
// ============================================================================
static int lnPage = 0;
static const int LN_COLS = 4, LN_ROWS = 3, LN_PER = LN_COLS * LN_ROWS, LN_CW = 80, LN_CH = 69;

static int lnCount() { return appTableCount() + luaAppCount(); }
static int lnPages() { return std::max(1, (lnCount() + LN_PER - 1) / LN_PER); }

static void lnDrawCell(int k, bool pressed = false) {
  int idx = lnPage * LN_PER + k;
  int cx = (k % LN_COLS) * LN_CW, cy = HDR_H + (k / LN_COLS) * LN_CH;
  uint16_t bg = pressed ? C_PRESS : C_BG;
  tft.fillRect(cx + 2, cy + 2, LN_CW - 4, LN_CH - 4, bg);
  if (idx >= lnCount()) return;
  uint8_t icon;
  String label;
  if (idx < appTableCount()) {
    icon = appTable()[idx]->icon;
    label = appTable()[idx]->label;
  } else {
    icon = I_CODE;
    label = luaAppLabel(idx - appTableCount());
  }
  panel(cx + 22, cy + 6, 36, 36, pressed ? C_ACCBG : C_SURF, pressed ? C_ACC : C_LINE);
  drawIcon(icon, cx + 28, cy + 12, 24, C_TEXT, pressed ? C_ACCBG : C_SURF);
  text(fit(label, F_XS, LN_CW - 6), cx + LN_CW / 2, cy + 54, F_XS, C_TEXT2, bg, MC_DATUM);
}

static void lnDrawHeaderPager() {
  int n = lnPages();
  tft.fillRect(200, 0, 70, HDR_H - 1, C_BG);
  if (n <= 1) return;
  for (int i = 0; i < n; i++) tft.fillRect(258 - (n - 1 - i) * 12, 12, 6, 6, i == lnPage ? C_ACC : C_TEXT3);
}

static void launcherDraw(bool full) {
  if (full) {
    header("Uygulamalar");
    lnDrawHeaderPager();
  }
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  for (int k = 0; k < LN_PER; k++) lnDrawCell(k);
}

static void launcherShow() {
  if (curApp && curApp->close) curApp->close();
  closeStack();
  shScreen = SH_LAUNCHER;
  curApp = nullptr;
  lnPage = constrain(lnPage, 0, lnPages() - 1);
  launcherDraw(true);
}

static void launcherTap(int x, int y) {
  if (y < HDR_H) {
    if (x < 40) homeShow();
    else if (x >= 200 && x < 272 && lnPages() > 1) {
      lnPage = (lnPage + 1) % lnPages();
      lnDrawHeaderPager();
      launcherDraw(false);
    }
    return;
  }
  int k = constrain(x / LN_CW, 0, LN_COLS - 1) + constrain((y - HDR_H) / LN_CH, 0, LN_ROWS - 1) * LN_COLS;
  int idx = lnPage * LN_PER + k;
  if (idx >= lnCount()) return;
  lnDrawCell(k, true);
  if (idx < appTableCount()) appOpen(appTable()[idx]);
  else luaAppOpen(idx - appTableCount());
}

static void launcherSwipe(int dir) {
  int n = lnPages();
  if (dir == SW_LEFT && lnPage < n - 1) lnPage++;
  else if (dir == SW_RIGHT && lnPage > 0) lnPage--;
  else if (dir == SW_RIGHT) {
    homeShow();
    return;
  } else return;
  lnDrawHeaderPager();
  launcherDraw(false);
}

// ============================================================================
// Gezinme
// ============================================================================
static void closeStack() {
  for (auto it = appStack.rbegin(); it != appStack.rend(); ++it)
    if ((*it)->close) (*it)->close();
  appStack.clear();
}

static void homeShow() {
  if (curApp && curApp->close) curApp->close();
  closeStack();
  curApp = nullptr;
  shScreen = SH_HOME;
  homeDraw(true);
}

static void appOpen(const App* a, bool fromHome) {
  if (curApp && curApp != a && curApp->close) curApp->close();
  closeStack();
  appFromHome = fromHome || shScreen == SH_HOME;
  curApp = a;
  shScreen = SH_APP;
  toastUntil = 0;
  if (a->open) a->open();
  if (curApp == a && shScreen == SH_APP) a->draw(true);  // open() başka bir yere yönlendirmiş olabilir
}

static void openApp(uint8_t id) {
  if (id < appTableCount()) appOpen(appTable()[id]);
}

static void appOpenChild(const App* a) {
  if (shScreen != SH_APP || !curApp) {
    appOpen(a);
    return;
  }
  appStack.push_back(curApp);
  curApp = a;
  toastUntil = 0;
  if (a->open) a->open();
  if (curApp == a && shScreen == SH_APP) a->draw(true);
}

static void appExit() {
  if (curApp && curApp->close) curApp->close();
  if (!appStack.empty()) {
    curApp = appStack.back();
    appStack.pop_back();
    curApp->draw(true);
    return;
  }
  curApp = nullptr;
  if (appFromHome) homeShow();
  else launcherShow();
}

static void appBack() {
  if (curApp && curApp->back && curApp->back()) return;
  appExit();
}

// ============================================================================
// Telefon bildirimi afişi
// ============================================================================
static uint32_t bannerUntil = 0;
static void drawBanner() {
  panel(6, 4, 308, 62, C_SURF, C_LINE);
  tft.fillRect(6, 4, 2, 62, C_ACC);
  drawAppIcon(banner.icon, banner.app, 18, 14, C_SURF);
  String title = banner.title, body = banner.text;
  if (!title.length()) {
    title = body;
    body = "";
  }
  text(fit(banner.app, F_XS, 250), 48, 10, F_XS, C_TEXT2, C_SURF);
  text(fit(title, F_MD, 254), 48, 24, F_MD, C_TEXT, C_SURF);
  if (body.length()) text(fit(body, F_SM, 254), 48, 45, F_SM, C_TEXT2, C_SURF);
}

// ============================================================================
// Ekranı yeniden çiz
// ============================================================================
static void shellRedraw(bool full) {
  if (kbm.active) {
    if (full) kbmDraw();
    return;
  }
  switch (shScreen) {
    case SH_HOME: homeDraw(full); break;
    case SH_LAUNCHER: launcherDraw(full); break;
    case SH_APP:
      if (curApp) curApp->draw(full);
      break;
  }
  if (ov.kind) ovDraw();
}

// ============================================================================
// Dokunma: basma/sürükleme/bırakma → dokunuş, uzun basış, kaydırma
// ============================================================================
static bool sysWake();  // main.cpp: gece kısıkken ilk dokunuş yalnızca ekranı açar

struct Gesture {
  bool down = false, swallow = false, raw = false, held = false, holdTried = false, kb = false;
  int sx = 0, sy = 0, lx = 0, ly = 0;
  uint32_t at = 0, lastSeen = 0;
};
static Gesture gs;

static void gestureUp() {
  int dx = gs.lx - gs.sx, dy = gs.ly - gs.sy;
  int adx = abs(dx), ady = abs(dy);
  if (gs.held) return;
  if (adx < 14 && ady < 14) {
    // dokunuş
    if (ov.kind) {
      ovTap(gs.sx, gs.sy);
      return;
    }
    if (bannerUntil) {
      if (gs.sy < 70) {
        bannerUntil = 0;
        openApp(AID_NOTIFS);
        return;
      }
      bannerUntil = 0;
      shellRedraw(true);
    }
    if (toastUntil) {
      toastUntil = 0;
      shellRedraw(true);
    }
    switch (shScreen) {
      case SH_HOME: homeTap(gs.sx, gs.sy); break;
      case SH_LAUNCHER: launcherTap(gs.sx, gs.sy); break;
      case SH_APP:
        if (gs.sy < HDR_H && gs.sx < 36) appBack();
        else if (curApp && curApp->tap) curApp->tap(gs.sx, gs.sy);
        break;
    }
    return;
  }
  if (ov.kind) return;
  int dir = -1;
  if (adx > 40 && adx > ady * 3 / 2) dir = dx < 0 ? SW_LEFT : SW_RIGHT;
  else if (ady > 30 && ady > adx * 3 / 2) dir = dy < 0 ? SW_UP : SW_DOWN;
  if (dir < 0) return;
  if (shScreen == SH_APP && dir == SW_RIGHT && gs.sx < 24) {  // soldan kaydırma: geri
    appBack();
    return;
  }
  switch (shScreen) {
    case SH_HOME:
      if (dir == SW_LEFT || dir == SW_UP) launcherShow();
      break;
    case SH_LAUNCHER: launcherSwipe(dir); break;
    case SH_APP:
      if (curApp && curApp->swipe) curApp->swipe(dir);
      break;
  }
}

static void shellTouch(bool t, int x, int y) {
  uint32_t ms = millis();
  if (t) {
    gs.lastSeen = ms;
    lastInteract = ms;
  }
  if (t && !gs.down) {
    gs = Gesture();
    gs.down = true;
    gs.sx = gs.lx = x;
    gs.sy = gs.ly = y;
    gs.at = gs.lastSeen = ms;
    gs.swallow = sysWake();
    if (gs.swallow) return;
    if (kbm.active) {
      gs.kb = true;
      kbmTouch(TE_DOWN, x, y);
      return;
    }
    if (!ov.kind && shScreen == SH_APP && curApp && curApp->raw && !(y < HDR_H && x < 36)) gs.raw = curApp->raw(TE_DOWN, x, y);
    return;
  }
  if (!gs.down) return;
  if (t) {
    if (gs.swallow) return;
    // dokunmatik titremesini yumuşat
    int nx = (gs.lx + x) / 2, ny = (gs.ly + y) / 2;
    if (abs(nx - gs.lx) + abs(ny - gs.ly) >= 2) {
      gs.lx = nx;
      gs.ly = ny;
      if (gs.kb) kbmTouch(TE_MOVE, nx, ny);
      else if (gs.raw && curApp && curApp->raw) curApp->raw(TE_MOVE, nx, ny);
    }
    if (!gs.kb && !gs.raw && !gs.holdTried && !ov.kind && ms - gs.at > LONG_PRESS_MS && abs(gs.lx - gs.sx) < 14 &&
        abs(gs.ly - gs.sy) < 14) {
      gs.holdTried = true;  // uzun basış kullanılırsa bırakınca ayrıca dokunuş sayılmaz
      if (shScreen == SH_APP && curApp && curApp->hold && !(gs.sy < HDR_H && gs.sx < 36)) gs.held = curApp->hold(gs.sx, gs.sy);
    }
    return;
  }
  if (ms - gs.lastSeen <= 40) return;  // kısa kesintiler bırakma sayılmaz
  gs.down = false;
  if (gs.swallow) return;
  if (gs.kb) {
    kbmTouch(TE_UP, gs.lx, gs.ly);
    return;
  }
  if (gs.raw) {
    if (curApp && curApp->raw) curApp->raw(TE_UP, gs.lx, gs.ly);
    return;
  }
  gestureUp();
}

// BOOT tuşu: kısa basış ana ekran, 2 sn Ayarlar, 6 sn Wi-Fi
static void settingsOpenPage(uint8_t p);
static void shellBoot(int stage) {
  if (kbm.active) kbmFinish(false);
  ov = Overlay();
  if (stage == 0) homeShow();
  else if (stage == 1) {
    appFromHome = true;
    settingsOpenPage(0);
  } else {
    appFromHome = true;
    settingsOpenPage(1);
  }
}

// Her döngüde
static void shellTick() {
  uint32_t ms = millis();
  if (kbm.active) {
    kbmTick();
    return;
  }
  if (toastUntil && (int32_t)(ms - toastUntil) > 0) {
    toastUntil = 0;
    shellRedraw(true);
  }
  if (bannerUntil && (int32_t)(ms - bannerUntil) > 0) {
    bannerUntil = 0;
    shellRedraw(true);
  }
  if (ov.kind) return;
  static int lastMinute = -1;
  static uint32_t lastSec = 0, lastCat = 0;
  struct tm t;
  int minute = localNow(t) ? t.tm_hour * 60 + t.tm_min : -1;
  bool minuteChanged = minute != lastMinute;
  lastMinute = minute;
  if (shScreen == SH_HOME) {
    if (minuteChanged && !toastUntil && !bannerUntil) {
      homeDrawStatus();
      homeDrawClock();
      homeDrawNow();
    }
    uint32_t s = swElapsedMs() / 1000;
    if (swRunning && s != lastSec && !toastUntil) {
      lastSec = s;
      yksNowCard(8, NOW_Y, 304, NOW_H, true);
    }
    bool night = t.tm_hour >= NIGHT_FROM || t.tm_hour < NIGHT_TO;
    if (minute < 0) night = false;
    uint32_t every = cat.st == CAT_SLEEP ? 700 : 70;
    if (ms - lastCat >= every && !bannerUntil) {
      lastCat = ms;
      catUpdate(night);
      catFlush();
    }
  } else if (shScreen == SH_APP && curApp) {
    if (minuteChanged && !toastUntil && !bannerUntil && !ov.kind && curApp->draw) {
      // başlıktaki saat
      if (minute >= 0) text(clockStr(), 312, 15, F_XS, C_TEXT3, C_BG, MR_DATUM);
    }
    if (curApp->tick) curApp->tick();
  }
}

// ============================================================================
// Sistemin çağırdığı kancalar
// ============================================================================
static void uiRedraw(bool full) { shellRedraw(full); }
static void uiToast(const String& msg) {
  if (kbm.active) return;
  toast(msg);
}
static bool appWantsQuiet() { return curApp && curApp->raw && shScreen == SH_APP; }  // oyun ve çizimde afiş yok
static void uiNotif(const Notif& n) {
  banner = n;
  if (kbm.active || ov.kind || otaProgress >= 0 || appWantsQuiet()) return;
  drawBanner();
  bannerUntil = millis() + BANNER_MS;
}
static void uiNotifsChanged() {
  if (kbm.active || ov.kind || bannerUntil || toastUntil) return;
  if (shScreen == SH_HOME) homeDrawStatus();
  else if (shScreen == SH_APP && curApp == appTable()[AID_NOTIFS]) curApp->draw(false);
}
static void uiMediaChanged(bool full) {
  if (shScreen == SH_APP && curApp == appTable()[AID_MEDIA] && !kbm.active && !ov.kind && !bannerUntil && !toastUntil) curApp->draw(full);
}
static void uiArtChanged() { uiMediaChanged(false); }
static void uiTimerChanged() {
  if (kbm.active || ov.kind) return;
  if (shScreen == SH_HOME) homeDrawNow();
  else if (shScreen == SH_APP && curApp == appTable()[AID_YKS]) curApp->draw(true);
}
static void uiPaletteChanged() {
  themeApply();
  shellRedraw(true);
}
static void uiDataChanged() {
  if (kbm.active || ov.kind || bannerUntil || toastUntil) return;
  if (shScreen == SH_HOME) homeDraw(false);
  else if (shScreen == SH_APP && curApp && !curApp->raw && curApp->draw) {
    if (curApp == appTable()[AID_YKS] || curApp == appTable()[AID_SETTINGS] || curApp == appTable()[AID_WEATHER]) curApp->draw(false);
  }
}
static void uiWifiChanged() {
  if (shScreen == SH_HOME && !kbm.active && !ov.kind && !bannerUntil && !toastUntil) homeDrawStatus();
}
