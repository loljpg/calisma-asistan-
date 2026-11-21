#pragma once
// Dosya aktarımı: kart bir web sayfası açar; aynı Wi-Fi'deki telefonun tarayıcısından SD karta
// dosya (resim, metin, uygulama) gönderilir, indirilir ya da silinir. Yalnızca bu ekran açıkken çalışır.
// Tarayıcı ekranda gösterilen 6 haneli kodla girer (sys/web.h).
#include "../os/shell.h"

static uint32_t trWaitSince = 0;  // bellek açılması bekleniyor (bulut duraklatıldı)

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
    if (!trWaitSince) trWaitSince = millis();
    bool waiting = millis() - trWaitSince < 8000;
    emptyState(HDR_H, SCR_H - HDR_H, I_TRANSFER, waiting ? "Bellek açılıyor…" : "Sunucu açılamadı",
               waiting ? "Bulut bağlantısı sunucu açıkken duraklatılıyor." : "Bellek yetmedi; kartı yeniden başlatıp dene.");
    return;
  }
  trWaitSince = 0;
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
  String code = webCode();
  text("Giriş kodu", 12, HDR_H + 108, F_SM, C_TEXT2, C_BG);
  text(code.substring(0, 3) + " " + code.substring(3), 308, HDR_H + 104, F_MD, C_ACC, C_BG, TR_DATUM);
  trDrawStatus();
}

static void trTap(int x, int y) {
  if (!wifiConnected() && y >= 186 && y < 230) settingsChild(SP_WIFI);
}

static void trTick() {
  static uint32_t last = 0, lastStat = 0;
  static bool lastConn = true;
  static String lastSig;
  if (millis() - last > (trWaitSince ? 500 : 1000)) {
    last = millis();
    bool c = wifiConnected();
    if (c != lastConn || (trWaitSince && sdOk && c && !webRunning() && millis() - trWaitSince < 9000)) {
      lastConn = c;
      trDraw(true);
      return;
    }
  }
  // Yükleme web sürecinde sürer; ilerleme buradan izlenir
  if (millis() - lastStat > 300 && sdOk && wifiConnected() && webRunning()) {
    lastStat = millis();
    String sig = webStatus() + "|" + String(webProgress()) + "|" + String(webUploadCount());
    if (sig != lastSig) {
      lastSig = sig;
      trDrawStatus();
    }
  }
}

static void trClose() {
  trWaitSince = 0;
  webStop();
}

static const App APP_TRANSFER = {"Dosya aktarımı", "Aktarım", I_TRANSFER, nullptr, trDraw, trTap, nullptr, nullptr, nullptr, trTick, nullptr, trClose};
