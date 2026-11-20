#pragma once
// Yılan: parmağını kaydırarak yön ver. Yemi ye, uza; duvara ya da kendine çarpma.
#include "../../os/shell.h"

static const int SN_C = 10, SN_W = 30, SN_H = 20, SN_X = 10, SN_Y = HDR_H + 5;
static std::vector<uint16_t> snBody;  // y*SN_W+x, baş sonda
static int snDir = 1, snNext = 1;     // 0 yukarı, 1 sağ, 2 aşağı, 3 sol
static uint16_t snFood = 0;
static uint32_t snScore = 0, snBest = 0, snStepAt = 0;
static bool snOver = false, snPaused = true, snLoaded = false;

static void snCell(uint16_t c, uint16_t col) {
  int x = SN_X + (c % SN_W) * SN_C, y = SN_Y + (c / SN_W) * SN_C;
  tft.fillRect(x + 1, y + 1, SN_C - 2, SN_C - 2, col);
}

static void snPlaceFood() {
  for (int t = 0; t < 400; t++) {
    uint16_t c = rnd(SN_W * SN_H);
    if (std::find(snBody.begin(), snBody.end(), c) == snBody.end()) {
      snFood = c;
      return;
    }
  }
}

static void snNew() {
  snBody.clear();
  for (int i = 0; i < 4; i++) snBody.push_back((SN_H / 2) * SN_W + 6 + i);
  snDir = snNext = 1;
  snScore = 0;
  snOver = false;
  snPaused = true;
  snPlaceFood();
}

static void snDrawScore() {
  tft.fillRect(150, 0, 120, HDR_H - 1, C_BG);
  text(String(snScore), 262, 15, F_MD, C_TEXT, C_BG, MR_DATUM);
  text("En iyi " + String(snBest), 214, 15, F_XS, C_TEXT3, C_BG, MR_DATUM);
}

static void snDrawField() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  tft.drawRect(SN_X - 2, SN_Y - 2, SN_W * SN_C + 4, SN_H * SN_C + 4, C_LINE);
  for (size_t i = 0; i < snBody.size(); i++) snCell(snBody[i], i + 1 == snBody.size() ? C_TEXT : C_TEXT2);
  snCell(snFood, C_ACC);
  if (snOver) {
    panel(70, 80, 180, 96, C_SURF, C_LINE);
    tft.fillRect(70, 80, 180, 2, C_ACC);
    text("Oyun bitti", 160, 94, F_MD, C_TEXT, C_SURF, TC_DATUM);
    text("Skor " + String(snScore), 160, 118, F_SM, C_TEXT2, C_SURF, TC_DATUM);
    button(100, 140, 120, 28, "Yeniden", B_PRIMARY, I_REFRESH, C_SURF);
  } else if (snPaused) {
    text("Başlamak için kaydır", 160, SN_Y + SN_H * SN_C - 22, F_SM, C_TEXT3, C_BG, MC_DATUM);
  }
}

static void snDraw(bool full) {
  if (!snLoaded) {
    Lock l;
    prefs.begin("snake", true);
    snBest = prefs.getUInt("best", 0);
    prefs.end();
    snLoaded = true;
    snNew();
  }
  if (full) header("Yılan");
  snDrawScore();
  snDrawField();
}

static void snStep() {
  if ((snNext + 2) % 4 != snDir) snDir = snNext;
  uint16_t h = snBody.back();
  int x = h % SN_W, y = h / SN_W;
  x += snDir == 1 ? 1 : (snDir == 3 ? -1 : 0);
  y += snDir == 2 ? 1 : (snDir == 0 ? -1 : 0);
  uint16_t n = y * SN_W + x;
  bool hit = x < 0 || y < 0 || x >= SN_W || y >= SN_H;
  if (!hit)
    for (size_t i = 1; i < snBody.size(); i++)
      if (snBody[i] == n) hit = true;
  if (hit) {
    snOver = true;
    if (snScore > snBest) {
      snBest = snScore;
      Lock l;
      prefs.begin("snake", false);
      prefs.putUInt("best", snBest);
      prefs.end();
    }
    snDrawField();
    snDrawScore();
    return;
  }
  snCell(h, C_TEXT2);
  snBody.push_back(n);
  snCell(n, C_TEXT);
  if (n == snFood) {
    snScore++;
    snPlaceFood();
    snCell(snFood, C_ACC);
    snDrawScore();
  } else {
    snCell(snBody.front(), C_BG);
    snBody.erase(snBody.begin());
  }
}

static void snTick() {
  if (snOver || snPaused) return;
  uint32_t every = std::max<uint32_t>(70, 160 - snScore * 3);
  if (millis() - snStepAt >= every) {
    snStepAt = millis();
    snStep();
  }
}

static bool snRaw(uint8_t ev, int x, int y) {
  static int sx, sy;
  static bool turned;
  if (ev == TE_DOWN && y < HDR_H) return false;
  if (ev == TE_DOWN) {
    sx = x;
    sy = y;
    turned = false;
    if (snOver && x >= 100 && x < 220 && y >= 136 && y < 172) {
      snNew();
      snDrawField();
      snDrawScore();
    }
    return true;
  }
  if (snOver) return true;
  int dx = x - sx, dy = y - sy;
  if (!turned && (abs(dx) > 16 || abs(dy) > 16)) {  // sürüklerken yön hemen değişir
    snNext = abs(dx) > abs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);
    turned = true;
    if (snPaused) {
      snPaused = false;
      snDrawField();
    }
    sx = x;
    sy = y;
    turned = false;
  }
  return true;
}

static void snClose() { snPaused = true; }

static const App APP_SNAKE = {"Yılan", "Yılan", I_SNAKE, nullptr, snDraw, nullptr, nullptr, nullptr, snRaw, snTick, nullptr, snClose};
