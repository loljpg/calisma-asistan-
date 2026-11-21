#pragma once
// Müzik: SD karttan müzik, Bluetooth kulaklığa ya da kartın hoparlörüne. Çalma ayrı "müzik modu" yazılımında yapılır (klasik Bluetooth
// ve MP3 çözücü bu yazılıma sığmıyor); bu ekran o yazılımı hazırlar ve açar.
#include "../os/shell.h"
#include <SD.h>

static int muCount = -1;  // Muzik klasöründeki şarkılar
static int muState = 1;
static String muInfo;

static int muCountDir(const String& dir, int depth, int limit) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return 0;
  int n = 0;
  for (File f = d.openNextFile(); f && n < limit; f = d.openNextFile()) {
    String nm = baseName(f.name());
    if (f.isDirectory()) {
      if (depth > 0 && !nm.startsWith(".")) n += muCountDir(joinPath(dir, nm), depth - 1, limit - n);
    } else if (extOf(nm) == "mp3") {
      n++;
    }
    f.close();
  }
  d.close();
  return n;
}

static void muDraw(bool full) {
  if (full) header("Müzik");
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  drawIcon(I_NOTE, 12, HDR_H + 10, 32, C_ACC, C_BG);
  text("SD karttan müzik", 54, HDR_H + 8, F_MD, C_TEXT, C_BG);
  String cnt = muCount < 0 ? String("") : (muCount ? String(muCount) + (muCount >= 500 ? "+" : "") + " şarkı bulundu" : String("Muzik klasöründe şarkı yok"));
  text(cnt, 54, HDR_H + 30, F_XS, muCount > 0 ? C_TEXT2 : C_ACC, C_BG);
  auto ls = wrapText("Kulaklıktan ya da kartın hoparlöründen çalar. Müzik açıkken Wi-Fi ve telefon bağlantısı kapanır; çıkınca DAXEN kaldığı yerden devam eder.",
                     F_SM, 296, 4);
  for (size_t i = 0; i < ls.size(); i++) text(ls[i], 12, HDR_H + 58 + i * 19, F_SM, C_TEXT2, C_BG);
  int y = HDR_H + 58 + ls.size() * 19 + 6;
  tft.fillRect(12, y + 4, 6, 6, muState == 0 ? C_ACC : C_TEXT3);
  text(fit(muInfo, F_XS, 280), 24, y, F_XS, muState == 0 ? C_TEXT : C_TEXT2, C_BG);
  if (muState == 2) return;
  if (muState == 0) button(12, 196, 160, 38, "Müziği aç", B_PRIMARY, I_PLAY);
  else button(12, 196, 160, 38, "Modülü indir", wifiConnected() ? B_PRIMARY : B_DISABLED, I_UPDATE);
  button(180, 196, 128, 38, "Şarkı ekle", B_NORMAL, I_TRANSFER);
}

static void muOpen() {
  muInfo = "Denetleniyor…";
  muState = musicState();
  muInfo = musicInfo();
  muCount = sdOk ? muCountDir("/Muzik", 2, 500) : 0;
  if (!muCount && sdOk) muCount = muCountDir("/Müzik", 2, 500) + muCountDir("/Music", 2, 500);
}

static void muTap(int x, int y) {
  if (y < 192 || muState == 2) return;
  if (x >= 176) {
    appOpenChild(appTable()[AID_TRANSFER]);
    return;
  }
  if (muState == 0) {
    if (!muCount) {
      alertBox("Şarkı yok", "MP3 dosyalarını SD karttaki Muzik klasörüne koy. Dosya aktarımı ile telefondan gönderebilirsin.");
      return;
    }
    dialog("Müzik açılsın mı?", "Kart müzik moduna geçer (birkaç saniye). Bildirimler ve Wi-Fi müzikten çıkana kadar kapalı.",
           {"Vazgeç", "Aç"}, [](int c) {
             if (c != 1) return;
             musicLaunch();
             muInfo = musicInfo();  // buraya dönüldüyse bir sorun oldu
             muState = musicState();
             shellRedraw(true);
           });
  } else if (wifiConnected()) {
    dialog("Müzik modülü indirilsin mi?", "Kart yeniden başlar ve modülü (1,3 MB) indirip SD karta kaydeder, sonra müziği açar.",
           {"Vazgeç", "İndir"}, [](int c) {
             if (c == 1) musicDownload();
           });
  } else {
    toast("İndirmek için Wi-Fi'ye bağlan");
  }
}

static const App APP_MUSIC = {"Müzik", "Müzik", I_HEADPHONES, muOpen, muDraw, muTap, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
