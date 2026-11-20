#pragma once
// Mayın Tarlası: dokun aç, basılı tut ya da bayrak kipinde dokun: işaretle. İlk dokunuş güvenlidir.
#include "../../os/shell.h"

static const int MN_W = 12, MN_H = 8, MN_C = 22, MN_X = 28, MN_Y = HDR_H + 4, MN_MINES = 16;
static uint8_t mnCell[MN_W * MN_H];  // bit0-3 komşu sayısı, bit4 mayın, bit5 açık, bit6 bayrak
static bool mnStarted = false, mnOver = false, mnWon = false, mnFlagMode = false;
static uint32_t mnStart = 0, mnEnd = 0;

enum { MN_MINE = 16, MN_OPEN = 32, MN_FLAG = 64 };

static void mnNew() {
  memset(mnCell, 0, sizeof mnCell);
  mnStarted = mnOver = mnWon = false;
  mnStart = mnEnd = 0;
}

static void mnPlace(int safe) {
  int sx = safe % MN_W, sy = safe / MN_W, n = 0;
  while (n < MN_MINES) {
    int i = rnd(MN_W * MN_H), x = i % MN_W, y = i / MN_W;
    if (abs(x - sx) <= 1 && abs(y - sy) <= 1) continue;
    if (mnCell[i] & MN_MINE) continue;
    mnCell[i] |= MN_MINE;
    n++;
  }
  for (int i = 0; i < MN_W * MN_H; i++) {
    int x = i % MN_W, y = i / MN_W, c = 0;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if ((dx || dy) && nx >= 0 && ny >= 0 && nx < MN_W && ny < MN_H && (mnCell[ny * MN_W + nx] & MN_MINE)) c++;
      }
    mnCell[i] = (mnCell[i] & ~15) | c;
  }
  mnStarted = true;
  mnStart = millis();
}

static uint16_t mnNumCol(int n) {
  switch (n) {
    case 1: return C_ACC;
    case 2: return C_TEXT;
    case 3: return rgbn(200, 170, 140);
    default: return C_TEXT2;
  }
}

static void mnDrawCell(int i) {
  int x = MN_X + (i % MN_W) * MN_C, y = MN_Y + (i / MN_W) * MN_C;
  uint8_t c = mnCell[i];
  if (c & MN_OPEN) {
    tft.fillRect(x, y, MN_C, MN_C, C_BG);
    tft.drawRect(x, y, MN_C, MN_C, C_SURF2);
    if (c & MN_MINE) {
      tft.fillRect(x + 1, y + 1, MN_C - 2, MN_C - 2, mnOver && !mnWon ? C_ACCBG : C_BG);
      drawIcon(I_MINE, x + 3, y + 3, MN_C - 6, C_TEXT, mnOver && !mnWon ? C_ACCBG : C_BG);
    } else if (c & 15) {
      text(String(c & 15), x + MN_C / 2, y + MN_C / 2 + 1, F_MD, mnNumCol(c & 15), C_BG, MC_DATUM);
    }
  } else {
    tft.fillRect(x + 1, y + 1, MN_C - 2, MN_C - 2, C_SURF);
    tft.drawFastHLine(x + 1, y + 1, MN_C - 2, grayn(0x26));
    tft.drawRect(x, y, MN_C, MN_C, C_BG);
    if (c & MN_FLAG) drawIcon(I_FLAG, x + 4, y + 4, MN_C - 8, C_ACC, C_SURF);
  }
}

static int mnFlags() {
  int f = 0;
  for (int i = 0; i < MN_W * MN_H; i++)
    if (mnCell[i] & MN_FLAG) f++;
  return f;
}

static void mnDrawStatus() {
  fillBg(0, MN_Y + MN_H * MN_C + 4, SCR_W, SCR_H - (MN_Y + MN_H * MN_C + 4));
  int y = MN_Y + MN_H * MN_C + 8;
  drawIcon(I_MINE, 16, y - 1, 16, C_TEXT2, C_BG);
  text(String(MN_MINES - mnFlags()), 38, y + 7, F_SM, C_TEXT, C_BG, ML_DATUM);
  uint32_t sec = mnStarted ? ((mnEnd ? mnEnd : millis()) - mnStart) / 1000 : 0;
  text(fmtHM(sec / 60, sec % 60), 160, y + 7, F_SM, C_TEXT2, C_BG, MC_DATUM);
  String st = mnOver ? (mnWon ? "Kazandın!" : "Mayına bastın") : (mnFlagMode ? "Bayrak kipi" : "");
  text(st, 304, y + 7, F_SM, mnOver || mnFlagMode ? C_ACC : C_TEXT2, C_BG, MR_DATUM);
}

static void mnDraw(bool full) {
  if (full) {
    header("Mayın Tarlası");
    headerAction(0, I_REFRESH);
    headerAction(1, I_FLAG, mnFlagMode);
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  for (int i = 0; i < MN_W * MN_H; i++) mnDrawCell(i);
  mnDrawStatus();
}

static void mnReveal(int i) {
  std::vector<int> st = {i};
  while (!st.empty()) {
    int k = st.back();
    st.pop_back();
    if (mnCell[k] & (MN_OPEN | MN_FLAG)) continue;
    mnCell[k] |= MN_OPEN;
    mnDrawCell(k);
    if ((mnCell[k] & 15) || (mnCell[k] & MN_MINE)) continue;
    int x = k % MN_W, y = k / MN_W;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && ny >= 0 && nx < MN_W && ny < MN_H) st.push_back(ny * MN_W + nx);
      }
  }
}

static void mnCheckWin() {
  for (int i = 0; i < MN_W * MN_H; i++)
    if (!(mnCell[i] & MN_MINE) && !(mnCell[i] & MN_OPEN)) return;
  mnOver = mnWon = true;
  mnEnd = millis();
  for (int i = 0; i < MN_W * MN_H; i++)
    if (mnCell[i] & MN_MINE) mnCell[i] |= MN_FLAG;
  mnDraw(false);
}

static void mnToggleFlag(int i) {
  if (mnCell[i] & MN_OPEN) return;
  mnCell[i] ^= MN_FLAG;
  mnDrawCell(i);
  mnDrawStatus();
}

static void mnOpenAt(int i) {
  if (mnCell[i] & (MN_FLAG | MN_OPEN)) return;
  if (!mnStarted) mnPlace(i);
  if (mnCell[i] & MN_MINE) {
    mnOver = true;
    mnEnd = millis();
    for (int k = 0; k < MN_W * MN_H; k++)
      if (mnCell[k] & MN_MINE) mnCell[k] |= MN_OPEN;
    mnDraw(false);
    return;
  }
  mnReveal(i);
  mnCheckWin();
  mnDrawStatus();
}

static int mnHit(int x, int y) {
  if (x < MN_X || y < MN_Y || x >= MN_X + MN_W * MN_C || y >= MN_Y + MN_H * MN_C) return -1;
  return ((y - MN_Y) / MN_C) * MN_W + (x - MN_X) / MN_C;
}

static void mnTap(int x, int y) {
  if (y < HDR_H) {
    int h = headerHit(x, y);
    if (h == 0) {
      mnNew();
      mnDraw(false);
    } else if (h == 1) {
      mnFlagMode = !mnFlagMode;
      headerAction(1, I_FLAG, mnFlagMode);
      mnDrawStatus();
    }
    return;
  }
  if (mnOver) {
    mnNew();
    mnDraw(false);
    return;
  }
  int i = mnHit(x, y);
  if (i < 0) return;
  if (mnFlagMode) mnToggleFlag(i);
  else mnOpenAt(i);
}

static bool mnHold(int x, int y) {
  int i = mnHit(x, y);
  if (i < 0 || mnOver) return false;
  if (mnFlagMode) mnOpenAt(i);
  else mnToggleFlag(i);
  return true;
}

static void mnTick() {
  static uint32_t last = 0;
  if (mnStarted && !mnOver && millis() - last > 1000) {
    last = millis();
    mnDrawStatus();
  }
}

static void mnOpen() {
  if (mnOver) mnNew();
}

static const App APP_MINES = {"Mayın Tarlası", "Mayın Tarlası", I_MINE, mnOpen, mnDraw, mnTap, mnHold, nullptr, nullptr, mnTick, nullptr, nullptr};
