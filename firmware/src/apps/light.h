#pragma once
// Işık ölçer: kartın üstündeki ışık sensörü (GPIO34). Ham değer, göreli ışık yüzdesi ve son bir
// dakikanın grafiği. Sensörün yönü kartlara göre değişebilir: elinle kapatınca yüzde düşmüyorsa
// "Ters çevir"i aç (otomatik parlaklık da bunu kullanır).
#include "../os/shell.h"

static uint8_t lmHist[120];
static int lmN = 0, lmMin = 9999, lmMax = -1;
static uint32_t lmAt = 0;

static void lmDrawTop(int raw, int pct) {
  fillBg(0, HDR_H, SCR_W, 74);
  drawIcon(I_LIGHT, 12, HDR_H + 12, 32, C_ACC, C_BG);
  text("%" + String(pct), 56, HDR_H + 6, F_LG, C_TEXT, C_BG);
  String desc = pct < 10 ? "Karanlık" : pct < 30 ? "Loş" : pct < 60 ? "Normal" : pct < 85 ? "Aydınlık" : "Çok aydınlık";
  text(desc, 56, HDR_H + 40, F_SM, C_TEXT2, C_BG);
  text("Ham değer " + String(raw), 308, HDR_H + 8, F_XS, C_TEXT2, C_BG, TR_DATUM);
  if (lmMax >= 0) text("En az %" + String(lmMin) + " · en çok %" + String(lmMax), 308, HDR_H + 24, F_XS, C_TEXT3, C_BG, TR_DATUM);
  progress(12, HDR_H + 64, 296, 3, pct, C_ACC);
}

static void lmDrawGraph() {
  const int Y0 = HDR_H + 78, H = 60, X0 = 12, W = 296;
  fillBg(0, Y0, SCR_W, H + 18);
  tft.drawRect(X0 - 1, Y0, W + 2, H, C_LINE);
  for (int i = 1; i < lmN; i++) {
    int x0 = X0 + (i - 1) * W / 119, x1 = X0 + i * W / 119;
    int y0 = Y0 + H - 2 - lmHist[i - 1] * (H - 4) / 100, y1 = Y0 + H - 2 - lmHist[i] * (H - 4) / 100;
    tft.drawLine(x0, y0, x1, y1, C_ACC);
  }
  text("son 1 dakika", 308, Y0 + H + 2, F_XS, C_TEXT3, C_BG, TR_DATUM);
}

static void lmDrawBottom() {
  fillBg(0, 186, SCR_W, SCR_H - 186);
  hline(12, 190, 296);
  text("Ters çevir", 12, 206, F_SM, C_TEXT, C_BG, ML_DATUM);
  text("Sensörü kapatınca yüzde düşmüyorsa aç", 12, 224, F_XS, C_TEXT3, C_BG, ML_DATUM);
  toggleSw(272, 196, cfg.ldrInv, C_BG);
}

static void lmDraw(bool full) {
  if (full) {
    header("Işık ölçer");
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  lmDrawTop(sysLight(), sysLightPct());
  lmDrawGraph();
  lmDrawBottom();
}

static void lmTick() {
  if (millis() - lmAt < 500) return;
  lmAt = millis();
  int pct = sysLightPct();
  if (lmN < 120) lmHist[lmN++] = pct;
  else {
    memmove(lmHist, lmHist + 1, 119);
    lmHist[119] = pct;
  }
  lmMin = std::min(lmMin, pct);
  lmMax = std::max(lmMax, pct);
  lmDrawTop(sysLight(), pct);
  lmDrawGraph();
}

static void lmTap(int x, int y) {
  if (y >= 190) {
    cfg.ldrInv = !cfg.ldrInv;
    saveConfig();
    lmN = 0;
    lmMin = 9999;
    lmMax = -1;
    lmDraw(true);
  }
}

static void lmOpen() {
  lmN = 0;
  lmMin = 9999;
  lmMax = -1;
}

static const App APP_LIGHT = {"Işık ölçer", "Işık ölçer", I_LIGHT, lmOpen, lmDraw, lmTap, nullptr, nullptr, nullptr, lmTick, nullptr, nullptr};
