#pragma once
// Dosya aktarımı: kart bir web sayfası açar; aynı Wi-Fi'deki telefonun tarayıcısından SD karta
// dosya (şarkı, resim, metin) gönderilir, indirilir ya da silinir. Yalnızca bu ekran açıkken çalışır.
#include "../os/shell.h"

static void trDrawStatus() {
  fillBg(0, 168, SCR_W, SCR_H - 168);
  String st = webStatus();
  int pct = webProgress();
  if (pct >= 0) {
    text("Alınıyor", 12, 170, F_XS, C_TEXT2, C_BG);
    text(fit(st, F_SM, 296), 12, 184, F_SM, C_TEXT, C_BG);
    progress(12, 206, 296, 3, pct, C_ACC);
  } else if (st.length()) {
    text("Son işlem", 12, 170, F_XS, C_TEXT2, C_BG);
    text(fit(st, F_SM, 296), 12, 184, F_SM, C_TEXT, C_BG);
  }
  int n = webUploadCount();
  if (n) text(String(n) + " dosya alındı", 12, 222, F_XS, C_TEXT3, C_BG);
  text("Ekrandan çıkınca sayfa kapanır", 308, 222, F_XS, C_TEXT3, C_BG, TR_DATUM);
}

static void trDraw(bool full) {
  if (full) header("Dosya aktarımı");
  if (!sdOk) {
    emptyState(HDR_H, SCR_H - HDR_H, I_SD, "SD kart takılı değil", "Dosya göndermek için SD kart gerekir.");
    return;
  }
  if (!wifiConnected()) {
    emptyState(HDR_H, 150, I_WIFI_OFF, "Wi-Fi'ye bağlı değil", "Kart ve telefon aynı Wi-Fi ağında olmalı.");
    fillBg(0, HDR_H + 150, SCR_W, SCR_H - HDR_H - 150);
    button(60, 190, 200, 36, "Wi-Fi ayarları", B_PRIMARY, I_WIFI);
    return;
  }
  if (!webRunning() && !webStart()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_TRANSFER, "Sunucu açılamadı", "Bellek yetmedi; kartı yeniden başlatıp dene.");
    return;
  }
  if (!full) {
    trDrawStatus();
    return;
  }
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  drawIcon(I_TRANSFER, 12, HDR_H + 12, 24, C_ACC, C_BG);
  text("Telefonun tarayıcısında aç", 46, HDR_H + 8, F_SM, C_TEXT2, C_BG);
  text("Aynı Wi-Fi: " + wifiSsid(), 46, HDR_H + 28, F_XS, C_TEXT3, C_BG);
  panel(12, HDR_H + 50, 296, 46, C_SURF, C_ACC);
  text("http://" + wifiIp(), 160, HDR_H + 73, F_MD, C_TEXT, C_SURF, MC_DATUM);
  auto ls = wrapText("Şarkıları Muzik klasörüne gönder; Müzik uygulaması oradan çalar.", F_XS, 296, 2);
  for (size_t i = 0; i < ls.size(); i++) text(ls[i], 12, HDR_H + 102 + i * 15, F_XS, C_TEXT2, C_BG);
  trDrawStatus();
}

static void trTap(int x, int y) {
  if (!wifiConnected() && y >= 186 && y < 230) settingsChild(SP_WIFI);
}

static void trTick() {
  static uint32_t last = 0;
  static bool lastConn = true;
  if (millis() - last > 1000) {
    last = millis();
    bool c = wifiConnected();
    if (c != lastConn) {
      lastConn = c;
      trDraw(true);
    }
  }
}

static void trClose() { webStop(); }

static const App APP_TRANSFER = {"Dosya aktarımı", "Aktarım", I_TRANSFER, nullptr, trDraw, trTap, nullptr, nullptr, nullptr, trTick, nullptr, trClose};
