#pragma once
// 2048: kaydırarak aynı sayıları birleştir. Bir hamle geri alınabilir.
#include "../../os/shell.h"

static uint8_t g2b[16], g2prev[16];  // değer = 2^n (0 boş)
static uint32_t g2Score = 0, g2Best = 0, g2PrevScore = 0;
static bool g2Over = false, g2Won = false, g2CanUndo = false, g2Loaded = false;
static const int G2_X = 70, G2_Y = HDR_H + 8, G2_T = 44, G2_G = 4;

static void g2Save() {
  Lock l;
  prefs.begin("g2048", false);
  prefs.putBytes("b", g2b, 16);
  prefs.putUInt("s", g2Score);
  prefs.putUInt("best", g2Best);
  prefs.putBool("won", g2Won);
  prefs.end();
}

static void g2Add() {
  int empty[16], n = 0;
  for (int i = 0; i < 16; i++)
    if (!g2b[i]) empty[n++] = i;
  if (n) g2b[empty[rnd(n)]] = rnd(10) ? 1 : 2;
}

static void g2New() {
  memset(g2b, 0, 16);
  g2Score = 0;
  g2Over = g2Won = g2CanUndo = false;
  g2Add();
  g2Add();
  g2Save();
}

static void g2Load() {
  Lock l;
  prefs.begin("g2048", true);
  size_t n = prefs.getBytes("b", g2b, 16);
  g2Score = prefs.getUInt("s", 0);
  g2Best = prefs.getUInt("best", 0);
  g2Won = prefs.getBool("won", false);
  prefs.end();
  g2Loaded = true;
  bool any = false;
  for (int i = 0; i < 16; i++) any |= g2b[i] > 0 && g2b[i] < 18;
  if (n != 16 || !any) g2New();
}

static uint16_t g2Col(int v) {
  static const uint16_t cols[] = {0, grayn(0x2A), grayn(0x38), rgbn(70, 78, 86), rgbn(85, 95, 105), rgbn(100, 112, 124),
                                  rgbn(118, 130, 142), rgbn(140, 150, 160), rgbn(160, 168, 176), rgbn(180, 186, 192), C_TEXT, C_ACC0};
  return v <= 0 ? C_SURF : (v < 12 ? cols[v] : C_ACC0);
}

static void g2DrawTile(int i) {
  int x = G2_X + (i % 4) * (G2_T + G2_G), y = G2_Y + (i / 4) * (G2_T + G2_G);
  int v = g2b[i];
  uint16_t c = g2Col(v);
  tft.fillRect(x, y, G2_T, G2_T, c);
  if (!v) return;
  String s = String(1UL << v);
  const uint8_t* f = s.length() >= 4 ? F_SM : (s.length() == 3 ? F_MD : F_LG);
  text(s, x + G2_T / 2, y + G2_T / 2 + 1, f, v >= 7 ? C_BG : C_TEXT, c, MC_DATUM);
}

static void g2DrawSide() {
  fillBg(0, HDR_H, G2_X - 4, SCR_H - HDR_H);
  text("Skor", 8, HDR_H + 10, F_XS, C_TEXT3, C_BG);
  text(String(g2Score), 8, HDR_H + 24, F_MD, C_TEXT, C_BG);
  text("En iyi", 8, HDR_H + 56, F_XS, C_TEXT3, C_BG);
  text(String(g2Best), 8, HDR_H + 70, F_SM, C_TEXT2, C_BG);
  fillBg(264, HDR_H, 56, SCR_H - HDR_H);
  iconButton(270, HDR_H + 10, 40, I_UNDO, g2CanUndo ? B_NORMAL : B_DISABLED);
  iconButton(270, HDR_H + 58, 40, I_REFRESH);
}

static void g2Draw(bool full) {
  if (!g2Loaded) g2Load();
  if (full) {
    header("2048");
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
    tft.fillRect(G2_X - 4, G2_Y - 4, 4 * G2_T + 3 * G2_G + 8, 4 * G2_T + 3 * G2_G + 8, C_SURF2);
  }
  for (int i = 0; i < 16; i++) g2DrawTile(i);
  g2DrawSide();
  if (g2Over) {
    panel(G2_X + 10, G2_Y + 70, 176, 50, C_SURF, C_ACC);
    text("Hamle kalmadı", G2_X + 98, G2_Y + 95, F_MD, C_TEXT, C_SURF, MC_DATUM);
  }
}

// Bir satırı sola kaydırıp birleştirir
static bool g2Slide(uint8_t* r[4]) {
  uint8_t v[4], out[4] = {0, 0, 0, 0};
  int n = 0;
  for (int i = 0; i < 4; i++)
    if (*r[i]) v[n++] = *r[i];
  int o = 0;
  for (int i = 0; i < n; i++) {
    if (i + 1 < n && v[i] == v[i + 1]) {
      out[o++] = v[i] + 1;
      g2Score += 1UL << (v[i] + 1);
      if (v[i] + 1 == 11 && !g2Won) {
        g2Won = true;
        toast("2048! Devam edebilirsin");
      }
      i++;
    } else {
      out[o++] = v[i];
    }
  }
  bool moved = false;
  for (int i = 0; i < 4; i++) {
    if (*r[i] != out[i]) moved = true;
    *r[i] = out[i];
  }
  return moved;
}

static bool g2CanMove() {
  for (int i = 0; i < 16; i++) {
    if (!g2b[i]) return true;
    if (i % 4 < 3 && g2b[i] == g2b[i + 1]) return true;
    if (i < 12 && g2b[i] == g2b[i + 4]) return true;
  }
  return false;
}

static void g2Move(int dir) {  // 0 sol, 1 sağ, 2 yukarı, 3 aşağı
  if (g2Over) return;
  uint8_t before[16];
  memcpy(before, g2b, 16);
  uint32_t sBefore = g2Score;
  bool moved = false;
  for (int k = 0; k < 4; k++) {
    uint8_t* r[4];
    for (int i = 0; i < 4; i++) {
      int idx;
      switch (dir) {
        case 0: idx = k * 4 + i; break;
        case 1: idx = k * 4 + 3 - i; break;
        case 2: idx = i * 4 + k; break;
        default: idx = (3 - i) * 4 + k; break;
      }
      r[i] = &g2b[idx];
    }
    moved |= g2Slide(r);
  }
  if (!moved) return;
  memcpy(g2prev, before, 16);
  g2PrevScore = sBefore;
  g2CanUndo = true;
  g2Add();
  if (g2Score > g2Best) g2Best = g2Score;
  g2Over = !g2CanMove();
  g2Save();
  g2Draw(false);
}

static void g2Swipe(int dir) { g2Move(dir == SW_LEFT ? 0 : dir == SW_RIGHT ? 1 : dir == SW_UP ? 2 : 3); }

static void g2Tap(int x, int y) {
  if (x >= 266 && y >= HDR_H + 6 && y < HDR_H + 54 && g2CanUndo) {
    memcpy(g2b, g2prev, 16);
    g2Score = g2PrevScore;
    g2CanUndo = false;
    g2Over = false;
    g2Save();
    g2Draw(true);
  } else if (x >= 266 && y >= HDR_H + 54 && y < HDR_H + 102) {
    dialog("Yeni oyun?", "Şimdiki oyun silinir.", {"Vazgeç", "Yeni oyun"}, [](int c) {
      if (c == 1) g2New();
    });
  }
}

static const App APP_2048 = {"2048", "2048", I_TILES, nullptr, g2Draw, g2Tap, nullptr, g2Swipe, nullptr, nullptr, nullptr, nullptr};
