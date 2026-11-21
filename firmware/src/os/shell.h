#pragma once
// DAXEN VANE kabuğu (çekirdek kısım): uygulama tanımı, gezinme, pencereler (iletişim kutusu,
// seçenek menüsü, bildirim balonu). Ana ekran, başlatıcı ve dokunma dağıtımı os/home.h'dedir.
#include "../ui/keyboard.h"

// Bir uygulama. Boş bırakılan işlevler çağrılmaz.
struct App {
  const char* name;   // başlık çubuğundaki ad
  const char* label;  // başlatıcıdaki kısa ad
  uint8_t icon;
  void (*open)();                          // açılırken
  void (*draw)(bool full);                 // ekranı çiz (full: başlık dahil her şey)
  void (*tap)(int x, int y);               // dokunup bırakma
  bool (*hold)(int x, int y);              // uzun basış; true: kullanıldı
  void (*swipe)(int dir);                  // 0 sola, 1 sağa, 2 yukarı, 3 aşağı
  bool (*raw)(uint8_t ev, int x, int y);   // ham olay; TE_DOWN'da true dönerse sürükleme uygulamanındır
  void (*tick)();                          // her döngüde
  bool (*back)();                          // geri: true dönerse uygulama kendi içinde geri gitti
  void (*close)();                         // kapanırken
};
enum SwipeDir { SW_LEFT = 0, SW_RIGHT = 1, SW_UP = 2, SW_DOWN = 3 };

enum ShellScreen : uint8_t { SH_HOME, SH_LAUNCHER, SH_APP };
static ShellScreen shScreen = SH_HOME;
static const App* curApp = nullptr;
static bool appFromHome = false;  // geri gelince ana ekrana mı, başlatıcıya mı dönülecek
static std::vector<const App*> appStack;  // bir uygulamanın içinden açılanlar (geri: öncekine döner)

// Uygulama kimlikleri (os/registry.h'deki listeyle aynı sıra)
enum AppId : uint8_t {
  AID_YKS, AID_NOTES, AID_FILES, AID_SETTINGS, AID_TERMINAL, AID_NETWORK, AID_SYSINFO, AID_TASKS, AID_TRANSFER, AID_MEDIA,
  AID_NOTIFS, AID_CALC, AID_CLOCK, AID_WEATHER, AID_TODO, AID_GAMES, AID_PHOTOS, AID_READER, AID_DRAW, AID_CARDS, AID_LIGHT, AID_COUNT
};
static void openApp(uint8_t id);
static const App* const* appTable();  // os/registry.h
static int appTableCount();
static void homeShow();
static void launcherShow();
static void appOpen(const App* a, bool fromHome = false);
static void appExit();
static void appOpenChild(const App* a);         // geri gelince bu uygulamaya dönülür
static void openFileWith(const String& path);   // dosyayı türüne göre uygun uygulamada açar
static void openInReader(const String& path);
static void shellRedraw(bool full);
static bool shellOverlayActive();  // pencere, menü, klavye, afiş ya da çalan alarm ekranda mı

// ============================================================================
// Bildirim balonu (kısa mesaj, altta)
// ============================================================================
static uint32_t toastUntil = 0;
static void toast(const String& msg) {
  auto ls = wrapText(msg, F_SM, 276, 2);
  int h = 16 + ls.size() * 19;
  int y = SCR_H - h - 8;
  panel(14, y, 292, h, C_SURF, C_LINE);
  tft.fillRect(14, y + 4, 2, h - 8, C_ACC);
  for (size_t i = 0; i < ls.size(); i++) text(ls[i], 160, y + 9 + i * 19, F_SM, C_TEXT, C_SURF, TC_DATUM);
  toastUntil = millis() + 2600;
}

// ============================================================================
// İletişim kutusu ve seçenek menüsü (ekranın üstünde)
// ============================================================================
enum OverlayKind : uint8_t { OV_NONE, OV_DIALOG, OV_MENU };
struct Overlay {
  uint8_t kind = OV_NONE;
  String title, msg;
  std::vector<String> opts;
  int primary = -1;
  int x = 0, y = 0, w = 0, h = 0;
  std::function<void(int)> cb;
};
static Overlay ov;

static void ovDraw() {
  if (ov.kind == OV_DIALOG) {
    auto ls = wrapText(ov.msg, F_SM, 252, 5);
    ov.w = 280;
    ov.h = 52 + ls.size() * 19 + 50;
    ov.x = 20;
    ov.y = (SCR_H - ov.h) / 2;
    panel(ov.x, ov.y, ov.w, ov.h, C_SURF, C_LINE);
    tft.fillRect(ov.x, ov.y, ov.w, 2, C_ACC);
    text(fit(ov.title, F_MD, 252), ov.x + 14, ov.y + 14, F_MD, C_TEXT, C_SURF);
    for (size_t i = 0; i < ls.size(); i++) text(ls[i], ov.x + 14, ov.y + 42 + i * 19, F_SM, C_TEXT2, C_SURF);
    int n = ov.opts.size(), bw = (ov.w - 28 - (n - 1) * 8) / std::max(1, n);
    for (int i = 0; i < n; i++)
      button(ov.x + 14 + i * (bw + 8), ov.y + ov.h - 46, bw, 34, ov.opts[i], i == ov.primary ? B_PRIMARY : B_NORMAL, I_NONE, C_SURF);
  } else if (ov.kind == OV_MENU) {
    int n = ov.opts.size();
    bool hasTitle = ov.title.length() > 0;
    ov.w = 240;
    ov.h = (hasTitle ? 30 : 4) + n * 32 + 4;
    ov.x = 40;
    ov.y = std::max(4, (SCR_H - ov.h) / 2);
    panel(ov.x, ov.y, ov.w, ov.h, C_SURF, C_LINE);
    int y = ov.y + 4;
    if (hasTitle) {
      text(fit(ov.title, F_SM, ov.w - 24), ov.x + 12, ov.y + 15, F_SM, C_TEXT2, C_SURF, ML_DATUM);
      y = ov.y + 30;
      hline(ov.x + 1, y - 2, ov.w - 2);
    }
    for (int i = 0; i < n; i++) {
      int ry = y + i * 32;
      if (i) hline(ov.x + 12, ry, ov.w - 24);
      text(fit(ov.opts[i], F_MD, ov.w - 24), ov.x + 14, ry + 16, F_MD, i == ov.primary ? C_ACC : C_TEXT, C_SURF, ML_DATUM);
    }
  }
}

static void dialog(const String& title, const String& msg, std::initializer_list<const char*> buttons, std::function<void(int)> cb,
                   int primary = -1) {
  ov = Overlay();
  ov.kind = OV_DIALOG;
  ov.title = title;
  ov.msg = msg;
  for (auto b : buttons) ov.opts.push_back(b);
  ov.primary = primary < 0 ? (int)ov.opts.size() - 1 : primary;
  ov.cb = cb;
  ovDraw();
}

static void menu(const String& title, const std::vector<String>& opts, std::function<void(int)> cb, int highlight = -1) {
  ov = Overlay();
  ov.kind = OV_MENU;
  ov.title = title;
  ov.opts = opts;
  ov.primary = highlight;
  ov.cb = cb;
  ovDraw();
}

// Tek düğmeli bilgi kutusu
static void alertBox(const String& title, const String& msg) {
  dialog(title, msg, {"Tamam"}, nullptr);
}

static void ovTap(int x, int y) {
  int choice = -1;
  if (ov.kind == OV_DIALOG) {
    int n = ov.opts.size(), bw = (ov.w - 28 - (n - 1) * 8) / std::max(1, n);
    if (y >= ov.y + ov.h - 50 && y < ov.y + ov.h)
      for (int i = 0; i < n; i++)
        if (x >= ov.x + 14 + i * (bw + 8) - 3 && x < ov.x + 14 + i * (bw + 8) + bw + 3) choice = i;
    if (choice < 0) return;  // iletişim kutusu yalnızca düğmeyle kapanır
  } else {
    int y0 = ov.y + (ov.title.length() ? 30 : 4);
    if (inRect(x, y, ov.x, y0, ov.w, ov.opts.size() * 32)) choice = (y - y0) / 32;
    else if (inRect(x, y, ov.x, ov.y, ov.w, ov.h)) return;
  }
  auto cb = ov.cb;
  ov = Overlay();
  shellRedraw(true);
  if (cb) cb(choice);
}

// Metin girişi (tam ekran klavye). İptalde geri çağrılmaz.
static void textInput(const String& title, const String& initial, std::function<void(const String&)> done, bool password = false,
                      int maxLen = 120, const String& hint = "") {
  kbOpen(title, initial, [done](bool ok, const String& v) {
    shellRedraw(true);
    if (ok && done) done(v);
  }, password, maxLen, hint);
}
