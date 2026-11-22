#pragma once
// Oyunlar: oyun listesi. Her oyun ayrı bir uygulamadır; geri gelince listeye dönülür.
#include "games/blokpatlat.h"
#include "games/snake.h"
#include "games/g2048.h"
#include "games/mines.h"
#include "games/sudoku.h"
#include "games/chess.h"
#include "games/smac.h"

struct GameDef {
  const App* app;
  const char* desc;
  const char* bestNs;  // en iyi skorun saklandığı ad alanı (yoksa boş)
};
static const GameDef GAMES[] = {
    {&APP_SMAC, "Potadan potaya at", "smac"},
    {&APP_BLOKPATLAT, "Satırları patlat", "bp"},
    {&APP_SNAKE, "Yemi ye, uza", "snake"},
    {&APP_2048, "Sayıları birleştir", "g2048"},
    {&APP_MINES, "Mayınlardan kaç", ""},
    {&APP_SUDOKU, "Üç zorluk", ""},
    {&APP_CHESS, "DAXEN'e karşı", ""},
};
static const int GAME_N = sizeof(GAMES) / sizeof(GAMES[0]);
// İki sütunlu döşeme (en çok 8 oyun)
static const int GM_W = 152, GM_H = 48, GM_GAP = 4;
static void gmCell(int i, int& x, int& y) { x = 6 + (i % 2) * (GM_W + GM_GAP), y = HDR_H + 4 + (i / 2) * (GM_H + GM_GAP); }

static uint32_t gameBest(const char* ns) {
  if (!ns || !*ns) return 0;
  Lock l;
  prefs.begin(ns, true);
  uint32_t b = prefs.getUInt("best", 0);
  prefs.end();
  return b;
}

static void gmDraw(bool full) {
  if (full) header("Oyunlar");
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  for (int i = 0; i < GAME_N; i++) {
    int x, y;
    gmCell(i, x, y);
    panel(x, y, GM_W, GM_H, C_SURF, C_LINE);
    drawIcon(GAMES[i].app->icon, x + 8, y + 14, 20, C_TEXT2, C_SURF);
    uint32_t b = gameBest(GAMES[i].bestNs);
    String bs = b ? String(b) : String("");
    int bw = bs.length() ? textW(bs, F_XS) + 6 : 0;
    text(fit(GAMES[i].app->name, F_SM, GM_W - 44 - bw), x + 36, y + 6, F_SM, C_TEXT, C_SURF);
    if (bw) text(bs, x + GM_W - 6, y + 9, F_XS, C_TEXT3, C_SURF, TR_DATUM);
    text(fit(GAMES[i].desc, F_XS, GM_W - 42), x + 36, y + 28, F_XS, C_TEXT2, C_SURF);
  }
}

static void gmTap(int x, int y) {
  for (int i = 0; i < GAME_N; i++) {
    int cx, cy;
    gmCell(i, cx, cy);
    if (inRect(x, y, cx, cy, GM_W, GM_H)) appOpenChild(GAMES[i].app);
  }
}

static const App APP_GAMES = {"Oyunlar", "Oyunlar", I_GAMEPAD, nullptr, gmDraw, gmTap, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
