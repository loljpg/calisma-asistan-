#pragma once
// Sudoku: tek çözümlü bulmaca üretilir (kolay, orta, zor). Kareye dokun, sağdan rakam seç.
// Oyun kaydedilir; kaldığın yerden devam edersin.
#include "../../os/shell.h"

static uint8_t sdkSol[81], sdkGiven[81], sdkCur[81];
static int sdkSel = -1;
static uint8_t sdkLevel = 0;  // 0 kolay, 1 orta, 2 zor
static bool sdkLoaded = false, sdkWon = false;
static const int SK_C = 22, SK_X = 8, SK_Y = HDR_H + 6;
static const char* const SDK_LEVELS[] = {"Kolay", "Orta", "Zor"};

static bool sdkOk(const uint8_t* g, int i, int v) {
  int r = i / 9, c = i % 9, br = r / 3 * 3, bc = c / 3 * 3;
  for (int k = 0; k < 9; k++) {
    if (g[r * 9 + k] == v || g[k * 9 + c] == v) return false;
    if (g[(br + k / 3) * 9 + bc + k % 3] == v) return false;
  }
  return true;
}

// Çözüm sayar (en fazla limit); rastgele sırayla doldurur (üretimde de kullanılır)
static int sdkSolve(uint8_t* g, int limit, bool randomize) {
  int best = -1, bestN = 10;
  for (int i = 0; i < 81; i++) {
    if (g[i]) continue;
    int n = 0;
    for (int v = 1; v <= 9; v++)
      if (sdkOk(g, i, v)) n++;
    if (n < bestN) {
      bestN = n;
      best = i;
      if (n <= 1) break;
    }
  }
  if (best < 0) return 1;
  if (bestN == 0) return 0;
  uint8_t order[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  if (randomize)
    for (int k = 8; k > 0; k--) std::swap(order[k], order[rnd(k + 1)]);
  int count = 0;
  for (int k = 0; k < 9 && count < limit; k++) {
    int v = order[k];
    if (!sdkOk(g, best, v)) continue;
    g[best] = v;
    count += sdkSolve(g, limit - count, randomize);
    if (count >= limit && randomize) return count;  // ilk çözüm yeterli (üretim)
    g[best] = 0;
  }
  return count;
}

static void sdkSave() {
  uint8_t pack[244];
  memcpy(pack, sdkSol, 81);
  memcpy(pack + 81, sdkGiven, 81);
  memcpy(pack + 162, sdkCur, 81);
  pack[243] = sdkLevel;
  Lock l;
  prefs.begin("sudoku", false);
  prefs.putBytes("g", pack, sizeof pack);
  prefs.end();
}

static void sdkNew(uint8_t level) {
  sdkLevel = level;
  memset(sdkSol, 0, 81);
  sdkSolve(sdkSol, 1, true);  // dolu ve geçerli bir tablo
  memcpy(sdkGiven, sdkSol, 81);
  int targetHoles = level == 0 ? 40 : (level == 1 ? 48 : 54);
  int order[81];
  for (int i = 0; i < 81; i++) order[i] = i;
  for (int k = 80; k > 0; k--) std::swap(order[k], order[rnd(k + 1)]);
  int holes = 0;
  uint32_t t0 = millis();
  for (int k = 0; k < 81 && holes < targetHoles && millis() - t0 < 4000; k++) {
    int i = order[k];
    uint8_t keep = sdkGiven[i];
    sdkGiven[i] = 0;
    uint8_t tmp[81];
    memcpy(tmp, sdkGiven, 81);
    if (sdkSolve(tmp, 2, false) != 1) sdkGiven[i] = keep;  // tek çözüm kalmalı
    else holes++;
  }
  memcpy(sdkCur, sdkGiven, 81);
  sdkSel = -1;
  sdkWon = false;
  sdkSave();
}

static void sdkLoad() {
  uint8_t pack[244];
  size_t n;
  {
    Lock l;
    prefs.begin("sudoku", true);
    n = prefs.getBytes("g", pack, sizeof pack);
    prefs.end();
  }
  sdkLoaded = true;
  if (n != sizeof pack) {
    sdkNew(0);
    return;
  }
  memcpy(sdkSol, pack, 81);
  memcpy(sdkGiven, pack + 81, 81);
  memcpy(sdkCur, pack + 162, 81);
  sdkLevel = pack[243] % 3;
  sdkWon = !memcmp(sdkCur, sdkSol, 81);
}

static bool sdkConflict(int i) {
  int v = sdkCur[i];
  if (!v) return false;
  sdkCur[i] = 0;
  bool ok = sdkOk(sdkCur, i, v);
  sdkCur[i] = v;
  return !ok;
}

static void sdkDrawCell(int i) {
  int r = i / 9, c = i % 9;
  int x = SK_X + c * SK_C + (c / 3), y = SK_Y + r * SK_C + (r / 3);
  bool sel = i == sdkSel;
  bool peer = sdkSel >= 0 && !sel &&
              (sdkSel / 9 == r || sdkSel % 9 == c || (sdkSel / 27 == r / 3 && (sdkSel % 9) / 3 == c / 3));
  bool same = sdkSel >= 0 && sdkCur[sdkSel] && sdkCur[i] == sdkCur[sdkSel];
  uint16_t bg = sel ? C_ACCBG : (same ? grayn(0x24) : (peer ? C_SURF : C_SURF2));
  tft.fillRect(x, y, SK_C - 1, SK_C - 1, bg);
  if (sel) tft.drawRect(x, y, SK_C - 1, SK_C - 1, C_ACC);
  if (sdkCur[i]) {
    bool given = sdkGiven[i] != 0;
    uint16_t fg = given ? C_TEXT : (sdkConflict(i) ? rgbn(205, 120, 110) : C_ACC);
    text(String(sdkCur[i]), x + SK_C / 2, y + SK_C / 2 + 1, F_MD, fg, bg, MC_DATUM);
  }
}

static void sdkDrawBoard() {
  int w = 9 * SK_C + 2;
  tft.fillRect(SK_X - 2, SK_Y - 2, w + 3, w + 3, C_LINE);
  for (int i = 0; i < 81; i++) sdkDrawCell(i);
}

static int sdkCount(int v) {
  int n = 0;
  for (int i = 0; i < 81; i++)
    if (sdkCur[i] == v) n++;
  return n;
}

static void sdkDrawPad() {
  fillBg(212, HDR_H, SCR_W - 212, SCR_H - HDR_H);
  for (int v = 1; v <= 9; v++) {
    int x = 216 + ((v - 1) % 3) * 34, y = HDR_H + 6 + ((v - 1) / 3) * 34;
    bool full = sdkCount(v) >= 9;
    button(x, y, 31, 31, String(v), full ? B_DISABLED : B_NORMAL);
  }
  button(216, HDR_H + 110, 99, 28, "Sil", B_NORMAL, I_ERASER);
  button(216, HDR_H + 144, 99, 28, SDK_LEVELS[sdkLevel], B_NORMAL);
  button(216, HDR_H + 178, 99, 28, "Yeni", B_PRIMARY, I_REFRESH);
}

static void sdkDraw(bool full) {
  if (!sdkLoaded) sdkLoad();
  if (full) {
    header("Sudoku");
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  sdkDrawBoard();
  sdkDrawPad();
  if (sdkWon) {
    panel(30, 96, 160, 54, C_SURF, C_ACC);
    text("Tebrikler!", 110, 112, F_MD, C_TEXT, C_SURF, TC_DATUM);
    text(SDK_LEVELS[sdkLevel] + String(" bulmaca çözüldü"), 110, 133, F_XS, C_TEXT2, C_SURF, TC_DATUM);
  }
}

static void sdkSelect(int i) {
  int old = sdkSel;
  sdkSel = i;
  sdkDrawBoard();
  (void)old;
}

static void sdkPut(int v) {
  if (sdkSel < 0 || sdkGiven[sdkSel] || sdkWon) return;
  sdkCur[sdkSel] = v;
  sdkSave();
  if (!memcmp(sdkCur, sdkSol, 81)) sdkWon = true;
  sdkDraw(false);
}

static void sdkTap(int x, int y) {
  if (y < HDR_H) return;
  if (x < 212) {
    int c = (x - SK_X) / SK_C, r = (y - SK_Y) / SK_C;
    if (c >= 0 && c < 9 && r >= 0 && r < 9) sdkSelect(r * 9 + c);
    return;
  }
  if (y < HDR_H + 108) {
    int c = constrain((x - 216) / 34, 0, 2), r = constrain((y - HDR_H - 6) / 34, 0, 2);
    sdkPut(r * 3 + c + 1);
  } else if (y < HDR_H + 140) {
    sdkPut(0);
  } else if (y < HDR_H + 174) {
    menu("Zorluk", {"Kolay", "Orta", "Zor"}, [](int c) {
      if (c < 0) return;
      toast("Bulmaca hazırlanıyor…");
      sdkNew(c);
      shellRedraw(true);
    }, sdkLevel);
  } else {
    dialog("Yeni bulmaca?", "Şimdiki oyun silinir.", {"Vazgeç", "Yeni"}, [](int c) {
      if (c != 1) return;
      sdkNew(sdkLevel);
    });
  }
}

static const App APP_SUDOKU = {"Sudoku", "Sudoku", I_GRID, nullptr, sdkDraw, sdkTap, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
