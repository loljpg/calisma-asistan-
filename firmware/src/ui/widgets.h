#pragma once
// DAXEN VANE ortak bileşenleri: başlık çubuğu, düğme, liste satırı, anahtar, kaydırıcı, sekme,
// boş durum, kaydırma çubuğu. Hepsi aynı kurallarla çizilir: grafit yüzey, ince kenarlık, gümüş yazı,
// etkin öğede çelik mavisi.
#include "theme.h"
#include "icons.h"

static const int HDR_H = 30;  // uygulama başlık çubuğu
static const int SCR_W = 320, SCR_H = 240;

// Ham dokunma olayları: basma, sürükleme, bırakma
enum TouchEv : uint8_t { TE_DOWN, TE_MOVE, TE_UP };

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static String clockStr() {
  struct tm t;
  if (!localNow(t)) return "--:--";
  return fmtHM(t.tm_hour, t.tm_min);
}

// Başlık çubuğu: geri oku, başlık, sağda saat. Sağdaki eylem simgeleri headerAction ile eklenir.
static int hdrActions = 0;
static void header(const String& title, bool back = true, bool showClock = true) {
  fillBg(0, 0, SCR_W, HDR_H - 1);
  hline(0, HDR_H - 1, SCR_W);
  int tx = 12;
  if (back) {
    drawIcon(I_BACK, 4, 5, 20, C_TEXT, C_BG);
    tx = 30;
  }
  hdrActions = 0;
  int right = showClock ? 270 : 312;
  if (showClock) text(clockStr(), 312, 15, F_XS, C_TEXT3, C_BG, MR_DATUM);
  text(fit(title, F_MD, right - tx - 4), tx, 15, F_MD, C_TEXT, C_BG, ML_DATUM);
}

// Sağdan sola eylem simgesi; k: 0 en sağdaki. Dokunma alanı 32×30.
static int hdrActX(int k, bool clock = true) { return (clock ? 272 : 314) - 32 * (k + 1) + 2; }
static void headerAction(int k, uint8_t icon, bool on = false, bool clock = true) {
  int x = hdrActX(k, clock);
  tft.fillRect(x, 0, 30, HDR_H - 1, C_BG);
  drawIcon(icon, x + 5, 5, 20, on ? C_ACC : C_TEXT2, C_BG);
}
static int headerHit(int x, int y, bool clock = true) {  // -1: geri, 0..: eylem, -2: hiçbiri
  if (y >= HDR_H) return -2;
  if (x < 36) return -1;
  for (int k = 0; k < 4; k++)
    if (x >= hdrActX(k, clock) - 1 && x < hdrActX(k, clock) + 31) return k;
  return -2;
}

// --- Düğmeler ---
enum BtnStyle : uint8_t { B_NORMAL, B_PRIMARY, B_ON, B_GHOST, B_DISABLED };
static void button(int x, int y, int w, int h, const String& label, BtnStyle st = B_NORMAL, uint8_t icon = I_NONE,
                   uint16_t under = C_BG) {
  uint16_t fill = C_SURF, border = C_LINE, fg = C_TEXT;
  switch (st) {
    case B_PRIMARY: border = C_ACC; break;
    case B_ON: fill = C_ACCBG; border = C_ACC; break;
    case B_GHOST: fill = under; border = under; fg = C_TEXT2; break;
    case B_DISABLED: fill = C_SURF2; fg = C_TEXT3; break;
    default: break;
  }
  panel(x, y, w, h, fill, border);
  int iw = icon ? (h >= 34 ? 20 : 16) : 0;
  int lw = label.length() ? textW(label, h >= 34 ? F_MD : F_SM) : 0;
  const uint8_t* f = h >= 34 ? F_MD : F_SM;
  int total = iw + (iw && lw ? 6 : 0) + lw;
  if (total > w - 12) {
    lw = w - 12 - iw - (iw ? 6 : 0);
    total = w - 12;
  }
  int sx = x + (w - total) / 2;
  if (iw) drawIcon(icon, sx, y + (h - iw) / 2, iw, st == B_ON || st == B_PRIMARY ? C_ACC : fg, fill);
  if (lw > 0) text(fit(label, f, lw + 1), sx + iw + (iw ? 6 : 0), y + h / 2 + 1, f, fg, fill, ML_DATUM);
}

// Yalnızca simgeli kare düğme
static void iconButton(int x, int y, int s, uint8_t icon, BtnStyle st = B_NORMAL) {
  uint16_t fill = st == B_ON ? C_ACCBG : (st == B_DISABLED ? C_SURF2 : C_SURF);
  panel(x, y, s, s, fill, st == B_ON || st == B_PRIMARY ? C_ACC : C_LINE);
  int is = s >= 40 ? 24 : (s >= 30 ? 20 : 16);
  drawIcon(icon, x + (s - is) / 2, y + (s - is) / 2, is, st == B_DISABLED ? C_TEXT3 : (st == B_ON ? C_ACC : C_TEXT), fill);
}

// --- Anahtar (açık/kapalı) 36×20 ---
static void toggleSw(int x, int y, bool on, uint16_t bg = C_SURF) {
  tft.fillRect(x, y, 36, 20, bg);
  panel(x, y, 36, 20, on ? C_ACCBG : C_SURF2, on ? C_ACC : C_LINE);
  if (on) tft.fillRect(x + 20, y + 4, 12, 12, C_TEXT);
  else tft.fillRect(x + 4, y + 4, 12, 12, C_TEXT3);
}

// --- Kaydırıcı: değer 0-100, dokunma yüksekliği 24 ---
static void slider(int x, int y, int w, int val, uint16_t bg = C_SURF) {
  tft.fillRect(x - 6, y, w + 12, 24, bg);
  val = constrain(val, 0, 100);
  int kx = x + (w - 8) * val / 100;
  tft.fillRect(x, y + 11, w, 2, C_LINE);
  tft.fillRect(x, y + 11, kx - x + 4, 2, C_ACC);
  tft.fillRect(kx, y + 4, 8, 16, C_TEXT);
}
static int sliderVal(int tx, int x, int w) { return constrain((tx - x - 4) * 100 / std::max(1, w - 8), 0, 100); }

// --- Sekmeli seçici (segmented) ---
static void segmented(int x, int y, int w, int h, const char* const* labels, int n, int sel, uint16_t bg = C_BG) {
  int sw = w / n;
  for (int i = 0; i < n; i++) {
    int sx = x + i * sw, ww = i == n - 1 ? w - sw * i : sw;
    bool on = i == sel;
    tft.fillRect(sx, y, ww, h, on ? C_ACCBG : C_SURF);
    text(fit(labels[i], F_SM, ww - 6), sx + ww / 2, y + h / 2 + 1, F_SM, on ? C_TEXT : C_TEXT2, on ? C_ACCBG : C_SURF, MC_DATUM);
  }
  tft.drawRect(x, y, w, h, C_LINE);
  for (int i = 1; i < n; i++) tft.drawFastVLine(x + i * sw, y, h, C_LINE);
  int sx = x + sel * sw, ww = sel == n - 1 ? w - sw * sel : sw;
  tft.drawRect(sx, y, ww + (sel < n - 1 ? 1 : 0), h, C_ACC);
  (void)bg;
}
static int segmentedHit(int tx, int x, int w, int n) { return constrain((tx - x) * n / std::max(1, w), 0, n - 1); }

// --- Liste satırı ---
// İki satırlık (başlık + alt yazı) ya da tek satırlık; sağda değer ya da ok.
static void listRow(int y, int h, uint8_t icon, const String& title, const String& sub, const String& value, bool chevron,
                    bool sel = false, uint16_t titleCol = C_TEXT) {
  uint16_t bg = sel ? C_PRESS : C_BG;
  tft.fillRect(0, y, SCR_W, h - 1, bg);
  hline(icon ? 44 : 12, y + h - 1, SCR_W - (icon ? 44 : 12));
  int x = 12;
  if (icon) {
    drawIcon(icon, 12, y + (h - 20) / 2, 20, sel ? C_ACC : C_TEXT2, bg);
    x = 44;
  }
  int right = SCR_W - 10;
  if (chevron) {
    drawIcon(I_RIGHT, SCR_W - 22, y + (h - 14) / 2, 14, C_TEXT3, bg);
    right -= 18;
  }
  int vw = 0;
  if (value.length()) {
    String v = fit(value, F_SM, 120);
    vw = textW(v, F_SM) + 8;
    text(v, right, y + h / 2 + 1, F_SM, C_TEXT2, bg, MR_DATUM);
  }
  int tw = right - vw - x;
  if (sub.length()) {
    int ty = y + (h - 38) / 2;
    text(fit(title, F_MD, tw), x, ty, F_MD, titleCol, bg);
    text(fit(sub, F_XS, tw), x, ty + 22, F_XS, C_TEXT2, bg);
  } else {
    text(fit(title, F_MD, tw), x, y + h / 2 + 1, F_MD, titleCol, bg, ML_DATUM);
  }
}

// Bölüm başlığı (liste içinde küçük gri yazı)
static void sectionLabel(int y, const String& s) {
  fillBg(0, y, SCR_W, 22);
  text(s, 12, y + 13, F_XS, C_TEXT3, C_BG, ML_DATUM);
}

// Dikey kaydırma çubuğu (sağ kenarda ince)
static void scrollBar(int y, int h, int first, int visible, int total) {
  tft.fillRect(SCR_W - 3, y, 3, h, C_BG);
  if (total <= visible) return;
  int bh = std::max(12, h * visible / total);
  int by = y + (h - bh) * first / std::max(1, total - visible);
  tft.fillRect(SCR_W - 3, by, 2, bh, C_TEXT3);
}

// Boş durum: ortada simge ve iki satır yazı
static void emptyState(int y0, int h, uint8_t icon, const String& l1, const String& l2 = "") {
  fillBg(0, y0, SCR_W, h);
  int cy = y0 + h / 2 - 28;
  drawIcon(icon, 160 - 16, cy, 32, C_TEXT3, C_BG);
  text(fit(l1, F_MD, 296), 160, cy + 50, F_MD, C_TEXT, C_BG, MC_DATUM);
  if (l2.length()) {
    auto ls = wrapText(l2, F_SM, 290, 3);
    for (size_t i = 0; i < ls.size(); i++) text(ls[i], 160, cy + 74 + i * 19, F_SM, C_TEXT2, C_BG, MC_DATUM);
  }
}

// Onay kutusu (kare)
static void checkBox(int x, int y, int s, bool on, uint16_t bg) {
  tft.fillRect(x, y, s, s, bg);
  if (on) {
    tft.fillRect(x, y, s, s, C_ACCBG);
    tft.drawRect(x, y, s, s, C_ACC);
    drawIcon(I_CHECK, x + 2, y + 2, s - 4, C_TEXT, C_ACCBG);
  } else {
    tft.drawRect(x, y, s, s, C_TEXT3);
  }
}

// Seri alevi: çizgi simgesi, vurgu renginde
static void drawFlame(int x, int y, int s, uint16_t bg) { drawIcon(I_FLAME, x, y, s, C_ACC, bg); }

// Kare telefon uygulama simgesi (gümüş tonlarda). Simge yoksa baş harf.
static IconImg* findIcon(uint32_t id) {
  for (auto* ic : icons)
    if (ic->id == id) return ic;
  return nullptr;
}
static void drawAppIcon(uint32_t id, const String& app, int x, int y, uint16_t bg) {
  IconImg* ic = findIcon(id);
  if (ic) {
    ic->used = millis();
    uint16_t row[ICON_PX];
    for (int j = 0; j < ICON_PX; j++) {
      for (int i = 0; i < ICON_PX; i++) {
        uint16_t c = ic->px[j * ICON_PX + i];
        row[i] = c == 0xF81F ? bg : silver565(c);
      }
      tft.pushImage(x, y + j, ICON_PX, 1, row);
    }
    return;
  }
  panel(x, y, ICON_PX, ICON_PX, C_SURF, C_LINE);
  String letter = app.length() ? app.substring(0, ((uint8_t)app[0] & 0x80) ? 2 : 1) : String("?");
  text(trUpper(letter), x + ICON_PX / 2, y + ICON_PX / 2 + 1, F_XS, C_TEXT2, C_SURF, MC_DATUM);
}
