#pragma once
#include "../core/touch.h"

// Dokunmatik ayarı: 9 nokta (3×3) → afin dönüşüm (en küçük kareler, core/calib.h), ardından
// ayarda kullanılmayan 2 noktayla doğrulama ve sonuç ekranı. BOOT ile her an çıkılır.
static const int CAL_PX[9] = {24, 160, 296, 24, 160, 296, 24, 160, 296};
static const int CAL_PY[9] = {26, 26, 26, 120, 120, 120, 214, 214, 214};
static const int CAL_VX[2] = {92, 228}, CAL_VY[2] = {74, 168};

static void calTarget(int x, int y, uint16_t col) {
  tft.drawFastHLine(x - 14, y, 29, col);
  tft.drawFastVLine(x, y - 14, 29, col);
  tft.drawCircle(x, y, 9, col);
  tft.fillCircle(x, y, 1, col);
}

static void calScreen(int x, int y, const String& title, const String& sub, int step, int steps, const String& warn) {
  fillBg(0, 0, SCR_W, SCR_H);
  int ty = y < 150 ? 150 : 46;  // yazı artıdan uzak yarıda
  text(title, 160, ty, F_MD, C_TEXT, C_BG, MC_DATUM);
  text(sub, 160, ty + 24, F_SM, C_TEXT2, C_BG, MC_DATUM);
  if (warn.length()) text(warn, 160, ty + 46, F_XS, C_ACC, C_BG, MC_DATUM);
  else text("Tırnakla dokunmak daha hassas · BOOT: çık", 160, ty + 46, F_XS, C_TEXT3, C_BG, MC_DATUM);
  for (int i = 0; i < steps; i++) tft.fillCircle(160 - (steps - 1) * 6 + i * 12, ty + 64, 2, i < step ? C_ACC : (i == step ? C_TEXT : C_LINE));
  calTarget(x, y, C_ACC);
}

// 0 tamam, 1 çıkıldı
static int calPoint(int x, int y, const String& title, int step, int steps, int& rx, int& ry) {
  String warn;
  for (;;) {
    calScreen(x, y, title, "Artının tam ortasına dokun, kısa bir an tut", step, steps, warn);
    int spread = 0;
    int r = touchCapture(rx, ry, spread);
    if (r == 1) return 1;
    if (r == 0) {
      tft.fillCircle(x, y, 6, C_ACC);
      delay(120);
      return 0;
    }
    warn = "Biraz daha uzun ve sabit tut";
  }
}

static void calResult(float vErr, float rms, float vMax, bool good) {
  fillBg(0, 0, SCR_W, SCR_H);
  text("Dokunmatik ayarı", 160, 40, F_MD, C_TEXT, C_BG, MC_DATUM);
  char b1[16], b2[80];
  snprintf(b1, sizeof b1, "%.1f", vErr);
  snprintf(b2, sizeof b2, "9 nokta · uyum %.1f px · doğrulamada en çok %.1f px", rms, vMax);
  String n1(b1), n2(b2);
  n1.replace(".", ",");
  n2.replace(".", ",");
  text(n1, 160, 84, F_LG, good ? C_ACC : C_TEXT, C_BG, MC_DATUM);
  text("ortalama sapma (piksel)", 160, 110, F_XS, C_TEXT3, C_BG, MC_DATUM);
  text(n2, 160, 136, F_XS, C_TEXT2, C_BG, MC_DATUM);
  text(good ? "Hassasiyet iyi." : "Sapma yüksek; yeniden ayarlaman önerilir.", 160, 160, F_SM, good ? C_TEXT2 : C_ACC, C_BG, MC_DATUM);
  button(12, 190, 144, 38, "Yeniden", good ? B_NORMAL : B_PRIMARY, I_REFRESH);
  button(164, 190, 144, 38, "Kaydet", good ? B_PRIMARY : B_NORMAL, I_CHECK);
}

static void runCalibration() {
  bool first = !cfg.calibrated;
  TouchCal old = tcal;
  for (;;) {
    int rx[9], ry[9];
    for (int i = 0; i < 9; i++) {
      if (calPoint(CAL_PX[i], CAL_PY[i], "Dokunmatik ayarı", i, 9, rx[i], ry[i])) {
        tcal = old;
        if (first) {  // ilk açılışta atlandı: varsayılanlarla devam
          Lock l;
          cfg.calibrated = true;
          saveConfig();
        }
        return;
      }
    }
    TouchCal k;
    CalFit fit;
    if (!calSolve(rx, ry, CAL_PX, CAL_PY, 9, k, &fit) || fit.rms > 15) {
      fillBg(0, 0, SCR_W, SCR_H);
      text("Ayar tutmadı", 160, 100, F_MD, C_TEXT, C_BG, MC_DATUM);
      text("Noktalar birbirini tutmuyor; baştan alalım", 160, 126, F_SM, C_TEXT2, C_BG, MC_DATUM);
      delay(1800);
      continue;
    }
    // Doğrulama: yeni ayarla, ayarda kullanılmayan iki nokta
    tcal = k;
    float vErr = 0, vMax = 0;
    bool quit = false;
    for (int i = 0; i < 2 && !quit; i++) {
      int vx, vy;
      if (calPoint(CAL_VX[i], CAL_VY[i], "Doğrulama", i, 2, vx, vy)) {
        quit = true;
        break;
      }
      float fx, fy;
      calApply(k, vx, vy, fx, fy);
      float e = hypotf(fx - CAL_VX[i], fy - CAL_VY[i]);
      vErr += e / 2;
      vMax = std::max(vMax, e);
    }
    if (quit) {  // doğrulamadan çıkıldı: yeni ayar yine de kaydedilir
      Lock l;
      cfg.calibrated = true;
      saveConfig();
      return;
    }
    // Sonuç: yeni ayarla dokunulur
    bool good = vMax <= 6;
    for (;;) {
      calResult(vErr, fit.rms, vMax, good);
      int x = 0, y = 0;
      while (!getTouch(x, y)) {
        if (digitalRead(PIN_BOOT) == LOW) {  // çık: yeni ayar kaydedilir
          while (digitalRead(PIN_BOOT) == LOW) delay(10);
          x = 300, y = 200;
          break;
        }
        delay(10);
      }
      while (tsTouched()) delay(5);
      if (y < 186) continue;
      if (x < 160) break;  // yeniden
      Lock l;
      calToLegacy(tcal, cfg.swapXY, cfg.x1, cfg.x2, cfg.y1, cfg.y2);  // eski bir sürüme dönülürse de doğru dokunsun
      cfg.calibrated = true;
      saveConfig();
      return;
    }
  }
}

