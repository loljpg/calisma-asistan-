#pragma once
// Oyunlar: oyun listesi. Her oyun ayrı bir uygulamadır; geri gelince listeye dönülür.
#include "games/blokpatlat.h"

struct GameDef {
  const App* app;
  const char* desc;
  const char* bestNs;  // en iyi skorun saklandığı ad alanı (yoksa boş)
};
static const GameDef GAMES[] = {
    {&APP_BLOKPATLAT, "Parçaları yerleştir, satırları patlat", "bp"},
};
static const int GAME_N = sizeof(GAMES) / sizeof(GAMES[0]);
static const int GM_ROW = 42;

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
    uint32_t b = gameBest(GAMES[i].bestNs);
    listRow(HDR_H + i * GM_ROW, GM_ROW, GAMES[i].app->icon, GAMES[i].app->name, GAMES[i].desc, b ? String(b) : String(""), true);
  }
}

static void gmTap(int x, int y) {
  int i = (y - HDR_H) / GM_ROW;
  if (y >= HDR_H && i < GAME_N) appOpenChild(GAMES[i].app);
}

static const App APP_GAMES = {"Oyunlar", "Oyunlar", I_GAMEPAD, nullptr, gmDraw, gmTap, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
