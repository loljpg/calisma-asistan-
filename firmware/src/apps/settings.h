#pragma once
// Ayarlar: Wi-Fi (tarama, şifre, kayıtlı ağlar), Bluetooth, Ekran, Bulut hesabı, Depolama,
// Güncelleme, Sistem bilgisi.
#include "../os/shell.h"
#include <SD.h>

enum SetPage : uint8_t { SP_MAIN, SP_WIFI, SP_BT, SP_DISPLAY, SP_ACCOUNT, SP_STORAGE, SP_UPDATE, SP_SYSTEM };
static uint8_t setPage = SP_MAIN;
static bool setDirect = false;  // doğrudan bir sayfadan açıldı (geri: uygulamadan çık)
static int wifiFirst = 0;
static const int SET_ROW = 30;
static const char* const SET_TITLES[] = {"Ayarlar", "Wi-Fi", "Bluetooth", "Ekran", "Bulut hesabı", "Depolama", "Güncelleme", "Sistem"};

// --- Ana liste ---
struct SetRow {
  uint8_t icon;
  const char* title;
};
static const SetRow SET_MAIN[] = {{I_WIFI, "Wi-Fi"},       {I_BT, "Bluetooth"},      {I_DISPLAY, "Ekran"}, {I_CLOUD, "Bulut hesabı"},
                                  {I_SD, "Depolama"},      {I_UPDATE, "Güncelleme"}, {I_CHIP, "Sistem"}};

static String setMainValue(int i) {
  switch (i) {
    case 0: return wifiConnected() ? wifiSsid() : String("Bağlı değil");
    case 1: return bleStatusText();
    case 2: return String(cfg.bright * 100 / 255) + "%";
    case 3: {
      bool ok;
      {
        Lock l;
        ok = cloudOk;
      }
      return !hasCreds() ? String("Girilmedi") : (bleLinked ? String("Telefon üzerinden") : (ok ? String("Bağlı") : String("Bağlanamadı")));
    }
    case 4: return sdOk ? String("SD kart takılı") : String("SD kart yok");
    case 5: return "fw-" + String(FW_BUILD);
    default: return "";
  }
}

static void setDrawMain() {
  for (int i = 0; i < 7; i++) listRow(HDR_H + i * SET_ROW, SET_ROW, SET_MAIN[i].icon, SET_MAIN[i].title, "", setMainValue(i), true);
}

// --- Wi-Fi ---
static const int WIFI_LIST_Y = 96, WIFI_ROW = 28, WIFI_ROWS = 5;

static void setDrawWifiStatus() {
  fillBg(0, HDR_H, SCR_W, WIFI_LIST_Y - HDR_H);
  bool conn = wifiConnected();
  bool trying = wifiConnStart && millis() - wifiConnStart < 20000;
  String l1, l2;
  if (trying) {
    l1 = wifiConnSsid;
    l2 = "Bağlanıyor…";
  } else if (conn) {
    l1 = wifiSsid();
    l2 = "Bağlı · " + wifiIp() + " · " + String(wifiRssi()) + " dBm";
  } else {
    l1 = "Bağlı değil";
    l2 = wifiLastError.length() ? wifiLastError : String("Listeden bir ağ seç");
  }
  drawIcon(conn ? I_WIFI : I_WIFI_OFF, 12, HDR_H + 9, 24, conn ? C_ACC : C_TEXT3, C_BG);
  text(fit(l1, F_MD, 260), 46, HDR_H + 4, F_MD, C_TEXT, C_BG);
  text(fit(l2, F_XS, 260), 46, HDR_H + 26, F_XS, C_TEXT2, C_BG);
  sectionLabel(HDR_H + 44, wifiScanBusy ? "Ağlar aranıyor…" : "Yakındaki ağlar");
}

static std::vector<WifiNet> setNets;  // ekrandaki liste (kilit dışında kopya)
static void setDrawWifiList() {
  {
    Lock l;
    setNets = wifiNets;
  }
  int n = setNets.size() + 1;  // son satır: gizli ağ ekle
  wifiFirst = constrain(wifiFirst, 0, std::max(0, n - WIFI_ROWS));
  String cur = wifiConnected() ? wifiSsid() : String("");
  for (int r = 0; r < WIFI_ROWS; r++) {
    int y = WIFI_LIST_Y + r * WIFI_ROW, i = wifiFirst + r;
    fillBg(0, y, SCR_W, WIFI_ROW);
    if (i >= n) continue;
    if (i == n - 1) {
      drawIcon(I_PLUS, 14, y + 6, 16, C_TEXT2, C_BG);
      text("Gizli ağ ekle", 40, y + WIFI_ROW / 2, F_SM, C_TEXT2, C_BG, ML_DATUM);
      continue;
    }
    const WifiNet& w = setNets[i];
    bool isCur = w.ssid == cur;
    hline(40, y + WIFI_ROW - 1, SCR_W - 52);
    drawSignal(14, y + 8, w.rssi, isCur ? C_ACC : C_TEXT2, C_LINE);
    int right = SCR_W - 12;
    if (w.secure) {
      drawIcon(I_LOCK, right - 14, y + 7, 14, C_TEXT3, C_BG);
      right -= 22;
    }
    if (isCur || w.known) {
      String tag = isCur ? "Bağlı" : "Kayıtlı";
      text(tag, right, y + WIFI_ROW / 2 + 1, F_XS, isCur ? C_ACC : C_TEXT3, C_BG, MR_DATUM);
      right -= textW(tag, F_XS) + 8;
    }
    text(fit(w.ssid, F_SM, right - 42), 40, y + WIFI_ROW / 2 + 1, F_SM, C_TEXT, C_BG, ML_DATUM);
  }
  scrollBar(WIFI_LIST_Y, WIFI_ROW * WIFI_ROWS, wifiFirst, WIFI_ROWS, n);
}

static void setDrawWifi() {
  setDrawWifiStatus();
  setDrawWifiList();
}

static void setWifiAskPass(const String& ssid) {
  textInput(ssid + " şifresi", "", [ssid](const String& p) {
    wifiConnect(ssid, p);
    toast(ssid + " ağına bağlanılıyor…");
  }, true, 63, "Şifre");
}

static void setWifiTapNet(const WifiNet& w) {
  String cur = wifiConnected() ? wifiSsid() : String("");
  String ssid = w.ssid;
  if (ssid == cur) {
    menu(ssid, {"Bağlantıyı kes", "Ağı unut", "Vazgeç"}, [ssid](int c) {
      if (c == 0) wifiDisconnect();
      else if (c == 1) wifiForget(ssid);
    });
  } else if (w.known) {
    menu(ssid, {"Bağlan", "Şifreyi değiştir", "Ağı unut", "Vazgeç"}, [ssid](int c) {
      if (c == 0) {
        String pass;
        for (auto& k : wifiKnown)
          if (k.ssid == ssid) pass = k.pass;
        wifiConnect(ssid, pass);
        toast(ssid + " ağına bağlanılıyor…");
      } else if (c == 1) {
        setWifiAskPass(ssid);
      } else if (c == 2) {
        wifiForget(ssid);
      }
    }, 0);
  } else if (w.secure) {
    setWifiAskPass(ssid);
  } else {
    wifiConnect(ssid, "");
    toast(ssid + " ağına bağlanılıyor…");
  }
}

// --- Bluetooth ---
static void setDrawBt() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  sectionLabel(HDR_H + 2, "Telefon");
  bool pairing = blePairUntil && (int32_t)(blePairUntil - millis()) > 0;
  drawIcon(I_PHONE, 12, HDR_H + 30, 24, bleLinked ? C_ACC : C_TEXT2, C_BG);
  text(bleStatusText(), 46, HDR_H + 27, F_MD, C_TEXT, C_BG);
  String sub = bleLinked ? "Bildirimler, müzik ve plan telefondan geliyor"
                         : (pairing ? "Uygulamada \"Masa saatini bul\"a dokun" : "Telefondaki uygulamayla internetsiz bağlantı");
  text(fit(sub, F_XS, 262), 46, HDR_H + 49, F_XS, C_TEXT2, C_BG);
  button(12, HDR_H + 72, 144, 32, pairing ? "Eşleşme açık" : "Eşleşme kipi", pairing ? B_ON : (bleLinked ? B_DISABLED : B_PRIMARY), I_BT);
  button(164, HDR_H + 72, 144, 32, "Eşleşmeyi sil", bleHasToken ? B_NORMAL : B_DISABLED, I_TRASH);
  sectionLabel(HDR_H + 118, "Kulaklık");
  listRow(HDR_H + 140, 40, I_HEADPHONES, "Bluetooth kulaklık", "Müzik uygulamasından bağlanır", "", true);
}

// --- Ekran ---
static const int DISP_ROW = 32;
static const char* const DISP_ROWS[] = {"Gece ekranı kıs", "Işığa göre ayarla", "Kapak rengini vurgu yap", "Renkleri ters çevir", "Dokunmatiği ayarla"};
static bool setDispVal(int i) {
  switch (i) {
    case 0: return cfg.nightDim;
    case 1: return cfg.autoBright;
    case 2: return cfg.coverTheme;
    case 3: return cfg.invert;
    default: return false;
  }
}
static void setDrawDispSlider() {
  slider(16, HDR_H + 22, 288, (cfg.bright - 16) * 100 / (255 - 16), C_BG);
}
static void setDrawDisplay() {
  fillBg(0, HDR_H, SCR_W, 50);
  text("Parlaklık", 12, HDR_H + 6, F_XS, C_TEXT2, C_BG);
  text(String(cfg.bright * 100 / 255) + "%", 308, HDR_H + 6, F_XS, C_TEXT2, C_BG, TR_DATUM);
  setDrawDispSlider();
  for (int i = 0; i < 5; i++) {
    int y = HDR_H + 50 + i * DISP_ROW;
    fillBg(0, y, SCR_W, DISP_ROW);
    hline(12, y, 296);
    text(DISP_ROWS[i], 12, y + DISP_ROW / 2 + 1, F_SM, C_TEXT, C_BG, ML_DATUM);
    if (i < 4) toggleSw(272, y + 6, setDispVal(i), C_BG);
    else drawIcon(I_RIGHT, 294, y + 9, 14, C_TEXT3, C_BG);
  }
}

// --- Bulut hesabı ---
static void setDrawAccount() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  String msg;
  bool ok;
  {
    Lock l;
    msg = netMsg;
    ok = cloudOk;
  }
  drawIcon(I_CLOUD, 12, HDR_H + 8, 24, ok ? C_ACC : C_TEXT3, C_BG);
  text(fit(msg, F_MD, 262), 46, HDR_H + 6, F_MD, C_TEXT, C_BG);
  text("Firebase · telefondaki uygulamayla aynı bilgiler", 46, HDR_H + 27, F_XS, C_TEXT2, C_BG);
  String db = cfg.dbUrl;
  db.replace("https://", "");
  String rows[4][2] = {{"Veritabanı", db.length() ? db : String("—")},
                       {"API anahtarı", cfg.apiKey.length() ? String("••••") + cfg.apiKey.substring(std::max(0, (int)cfg.apiKey.length() - 4)) : String("—")},
                       {"E-posta", cfg.email.length() ? cfg.email : String("—")},
                       {"Şifre", cfg.password.length() ? String("••••••••") : String("—")}};
  for (int i = 0; i < 4; i++) {
    int y = HDR_H + 50 + i * 32;
    hline(12, y, 296);
    text(rows[i][0], 12, y + 17, F_SM, C_TEXT, C_BG, ML_DATUM);
    text(fit(rows[i][1], F_SM, 170), 290, y + 17, F_SM, C_TEXT2, C_BG, MR_DATUM);
    drawIcon(I_RIGHT, 294, y + 10, 12, C_TEXT3, C_BG);
  }
  button(12, 210 - 4, 296, 30, "Telefon tarayıcısından gir", B_NORMAL, I_PHONE);
}

static void setEditAccount(int i) {
  const char* titles[] = {"Veritabanı adresi", "Web API anahtarı", "E-posta", "Şifre"};
  String cur = i == 0 ? cfg.dbUrl : (i == 1 ? cfg.apiKey : (i == 2 ? cfg.email : String("")));
  textInput(titles[i], cur, [i](const String& v) {
    String db = cfg.dbUrl, key = cfg.apiKey, mail = cfg.email, pass = cfg.password;
    String t = v;
    t.trim();
    if (i == 0) db = normalizeDbUrl(t);
    else if (i == 1) key = t;
    else if (i == 2) mail = t;
    else pass = v;
    accountSet(db, key, mail, pass);
    toast("Kaydedildi");
  }, i == 3, i == 0 ? 140 : 100);
}

// --- Depolama ---
static void setDrawStorage() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  drawIcon(I_SD, 12, HDR_H + 10, 24, sdOk ? C_ACC : C_TEXT3, C_BG);
  if (sdOk) {
    uint64_t tot = SD.totalBytes(), used = SD.usedBytes();
    text("SD kart", 46, HDR_H + 6, F_MD, C_TEXT, C_BG);
    text(fmtBytes(used) + " / " + fmtBytes(tot) + " kullanılıyor", 46, HDR_H + 27, F_XS, C_TEXT2, C_BG);
    progress(12, HDR_H + 50, 296, 4, tot ? (int)(used * 100 / tot) : 0, C_ACC);
  } else {
    text("SD kart takılı değil", 46, HDR_H + 6, F_MD, C_TEXT, C_BG);
    text("Notlar, müzik ve dosyalar için SD kart gerekir", 46, HDR_H + 27, F_XS, C_TEXT2, C_BG);
  }
  int q = queueCount();
  listRow(HDR_H + 64, 40, I_REFRESH, "Gönderilmeyi bekleyen", "Çalışma kayıtları (telefona ya da buluta)", String(q), false);
  listRow(HDR_H + 104, 40, I_FOLDER, "Dosyalar", "SD karttaki klasörler", "", true);
  listRow(HDR_H + 144, 40, I_TRANSFER, "Dosya aktarımı", "Telefondan tarayıcıyla dosya gönder", "", true);
}

// --- Güncelleme ---
static void setDrawUpdate() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  drawIcon(I_UPDATE, 12, HDR_H + 10, 24, C_ACC, C_BG);
  text("fw-" + String(FW_BUILD), 46, HDR_H + 6, F_MD, C_TEXT, C_BG);
  text("DAXEN VANE · " FW_VARIANT, 46, HDR_H + 27, F_XS, C_TEXT2, C_BG);
  String m;
  {
    Lock l;
    m = otaMsg;
  }
  if (!m.length()) m = "Kart her açılışta ve gece 04:00'te kendiliğinden denetler.";
  auto ls = wrapText(m, F_SM, 296, 3);
  for (size_t i = 0; i < ls.size(); i++) text(ls[i], 12, HDR_H + 58 + i * 19, F_SM, C_TEXT2, C_BG);
  auto info = wrapText("SD kartın köküne firmware-" FW_VARIANT ".bin koyarsan açılışta oradan yüklenir.", F_XS, 296, 3);
  for (size_t i = 0; i < info.size(); i++) text(info[i], 12, 150 + i * 15, F_XS, C_TEXT3, C_BG);
  button(12, 198, 296, 36, "Güncellemeyi denetle", B_PRIMARY, I_REFRESH);
}

// --- Sistem ---
static String uptimeStr() {
  uint32_t m = millis() / 60000;
  if (m < 60) return String(m) + " dk";
  if (m < 1440) return String(m / 60) + " sa " + String(m % 60) + " dk";
  return String(m / 1440) + " gün " + String((m % 1440) / 60) + " sa";
}
static String bootShort() {
  String b = bootReason;
  int c = b.indexOf(": ");
  if (c >= 0) b = b.substring(c + 2);
  int d = b.indexOf(" · ");
  if (d > 0) b = b.substring(0, d);
  return b;
}
static void setDrawSystem() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  drawLogo(LOGO_S, LOGO_S_W, LOGO_S_H, 12, HDR_H + 10);
  text("fw-" + String(FW_BUILD) + " · " FW_VARIANT, 308, HDR_H + 14, F_XS, C_TEXT2, C_BG, TR_DATUM);
  text("ESP32 · 240 MHz · 4 MB", 308, HDR_H + 30, F_XS, C_TEXT3, C_BG, TR_DATUM);
  String rows[5][2] = {
      {"Çalışma süresi", uptimeStr()},
      {"Son açılış", bootShort()},
      {"Bellek", String(sysHeapFree() / 1024) + " KB boş · en az " + String(sysHeapMin() / 1024) + " KB"},
      {"Sıcaklık", String(sysTemp(), 0) + " °C"},
      {"Ağ", wifiConnected() ? wifiIp() : String("—")},
  };
  for (int i = 0; i < 5; i++) {
    int y = HDR_H + 46 + i * 25;
    hline(12, y, 296);
    text(rows[i][0], 12, y + 13, F_SM, C_TEXT, C_BG, ML_DATUM);
    text(fit(rows[i][1], F_XS, 190), 308, y + 13, F_XS, C_TEXT2, C_BG, MR_DATUM);
  }
  button(12, 206, 296, 30, "Yeniden başlat", B_NORMAL, I_POWER);
}

static void setDrawPage() {
  switch (setPage) {
    case SP_MAIN: setDrawMain(); break;
    case SP_WIFI: setDrawWifi(); break;
    case SP_BT: setDrawBt(); break;
    case SP_DISPLAY: setDrawDisplay(); break;
    case SP_ACCOUNT: setDrawAccount(); break;
    case SP_STORAGE: setDrawStorage(); break;
    case SP_UPDATE: setDrawUpdate(); break;
    case SP_SYSTEM: setDrawSystem(); break;
  }
}

static void setDraw(bool full) {
  if (full) {
    header(SET_TITLES[setPage]);
    if (setPage == SP_WIFI) headerAction(0, I_REFRESH, wifiScanBusy);
  }
  setDrawPage();
}

static void setGo(uint8_t p) {
  setPage = p;
  wifiFirst = 0;
  if (p == SP_WIFI) wifiScan();
  setDraw(true);
}

static void setTap(int x, int y) {
  if (y < HDR_H) {
    if (setPage == SP_WIFI && headerHit(x, y) == 0) {
      wifiScan();
      headerAction(0, I_REFRESH, true);
      setDrawWifiStatus();
    }
    return;
  }
  switch (setPage) {
    case SP_MAIN: {
      int i = (y - HDR_H) / SET_ROW;
      const uint8_t pages[] = {SP_WIFI, SP_BT, SP_DISPLAY, SP_ACCOUNT, SP_STORAGE, SP_UPDATE, SP_SYSTEM};
      if (i >= 0 && i < 7) setGo(pages[i]);
      break;
    }
    case SP_WIFI: {
      if (y < WIFI_LIST_Y) {
        if (wifiConnected())
          menu(wifiSsid(), {"Bağlantıyı kes", "Vazgeç"}, [](int c) {
            if (c == 0) wifiDisconnect();
          });
        return;
      }
      int i = wifiFirst + (y - WIFI_LIST_Y) / WIFI_ROW;
      if (i < (int)setNets.size()) {
        setWifiTapNet(setNets[i]);
      } else if (i == (int)setNets.size()) {
        textInput("Ağ adı (SSID)", "", [](const String& ssid) {
          String s = ssid;
          textInput(s + " şifresi", "", [s](const String& p) {
            wifiConnect(s, p);
            toast(s + " ağına bağlanılıyor…");
          }, true, 63, "Şifresizse boş bırak");
        }, false, 32);
      }
      break;
    }
    case SP_BT:
      if (y >= HDR_H + 72 && y < HDR_H + 108) {
        if (x < 160) {
          if (bleLinked) toast("Telefon zaten bağlı");
          else {
            blePairMode();
            setDrawBt();
            toast("Uygulamada \"Masa saatini bul\"a dokun");
          }
        } else if (bleHasToken) {
          dialog("Eşleşme silinsin mi?", "Telefon yeniden eşleşene kadar bildirim ve müzik gelmez.", {"Vazgeç", "Sil"}, [](int c) {
            if (c == 1) {
              bleForget();
              toast("Bluetooth eşleşmesi silindi");
            }
          });
        }
      } else if (y >= HDR_H + 140) {
        openApp(AID_MUSIC);
      }
      break;
    case SP_DISPLAY: {
      if (y < HDR_H + 50) {
        cfg.bright = 16 + sliderVal(x, 16, 288) * (255 - 16) / 100;
        sysSetBacklight(cfg.bright);
        saveConfig();
        setDrawDisplay();
        break;
      }
      int i = (y - HDR_H - 50) / DISP_ROW;
      if (i == 0) cfg.nightDim = !cfg.nightDim;
      else if (i == 1) cfg.autoBright = !cfg.autoBright;
      else if (i == 2) {
        cfg.coverTheme = !cfg.coverTheme;
        themeApply();
      } else if (i == 3) {
        cfg.invert = !cfg.invert;
        tft.invertDisplay(cfg.invert);
      } else if (i == 4) {
        dialog("Dokunmatik ayarı", "Ekranda çıkan üç artının ortasına sırayla dokun. Kalemle daha doğru olur.", {"Vazgeç", "Başla"},
               [](int c) {
                 if (c == 1) {
                   sysCalibrate();
                   shellRedraw(true);
                 }
               });
        break;
      }
      saveConfig();
      if (i == 2) setDraw(true);
      else setDrawDisplay();
      break;
    }
    case SP_ACCOUNT:
      if (y >= 200) {
        if (!wifiConnected()) {
          alertBox("Wi-Fi gerekli", "Tarayıcıdan girmek için kart ve telefon aynı Wi-Fi ağında olmalı.");
        } else if (webStart()) {
          alertBox("Tarayıcıdan gir", "Telefonun tarayıcısında şu adresi aç:\nhttp://" + wifiIp() + "/hesap");
        }
      } else if (y >= HDR_H + 50) {
        int i = (y - HDR_H - 50) / 32;
        if (i >= 0 && i < 4) setEditAccount(i);
      }
      break;
    case SP_STORAGE:
      if (y >= HDR_H + 104 && y < HDR_H + 144) {
        openApp(AID_FILES);
      } else if (y >= HDR_H + 144) {
        openApp(AID_TRANSFER);
      }
      break;
    case SP_UPDATE:
      if (y >= 194)
        dialog("Güncelleme denetlensin mi?", "Kart yeniden başlar, yeni sürüm varsa indirip kurar. Kronometre ve kayıtlar korunur.",
               {"Vazgeç", "Denetle"}, [](int c) {
                 if (c == 1) sysRequestOta();
               });
      break;
    case SP_SYSTEM:
      if (y >= 202)
        dialog("Yeniden başlatılsın mı?", "Kronometre ve kayıtlar korunur.", {"Vazgeç", "Başlat"}, [](int c) {
          if (c == 1) sysRestart();
        });
      break;
  }
}

static void setSwipe(int dir) {
  if (setPage == SP_WIFI && (dir == SW_UP || dir == SW_DOWN)) {
    int n = setNets.size() + 1;
    int nf = constrain(wifiFirst + (dir == SW_UP ? WIFI_ROWS - 1 : -(WIFI_ROWS - 1)), 0, std::max(0, n - WIFI_ROWS));
    if (nf != wifiFirst) {
      wifiFirst = nf;
      setDrawWifiList();
    }
  }
}

static void setTick() {
  static uint32_t last = 0;
  static bool lastBusy = false, lastConn = false;
  if (millis() - last < 700) return;
  last = millis();
  if (setPage == SP_WIFI) {
    bool busy = wifiScanBusy, conn = wifiConnected();
    if (busy != lastBusy || conn != lastConn) {
      if (lastBusy && !busy) setDrawWifiList();
      lastBusy = busy;
      lastConn = conn;
      headerAction(0, I_REFRESH, busy);
      setDrawWifiStatus();
    } else if (wifiConnStart) {
      setDrawWifiStatus();
      if (millis() - wifiConnStart > 20000) wifiConnStart = 0;
    }
  } else if (setPage == SP_BT) {
    static bool lastPair = false;
    bool pairing = blePairUntil && (int32_t)(blePairUntil - millis()) > 0;
    if (pairing != lastPair) {
      lastPair = pairing;
      setDrawBt();
    }
  } else if (setPage == SP_SYSTEM && millis() % 10000 < 700) {
    setDrawSystem();
  }
}

static bool setBack() {
  if (setPage != SP_MAIN && !setDirect) {
    setGo(SP_MAIN);
    return true;
  }
  return false;
}

static void setOpen() {
  if (!setDirect) setPage = SP_MAIN;
}
static void setClose() { setDirect = false; }

static const App APP_SETTINGS = {"Ayarlar", "Ayarlar", I_SETTINGS, setOpen, setDraw, setTap, nullptr, setSwipe, nullptr, setTick, setBack, setClose};

// Ayarların bir sayfasını doğrudan açar (ör. BOOT 6 sn → Wi-Fi, Sistem uygulaması)
static void settingsOpenPage(uint8_t p) {
  setDirect = true;
  setPage = p;
  appOpen(&APP_SETTINGS);
  if (p == SP_WIFI) wifiScan();
}

static void sysInfoOpen() {
  setDirect = true;
  setPage = SP_SYSTEM;
}
static const App APP_SYSINFO = {"Sistem", "Sistem", I_CHIP, sysInfoOpen, setDraw, setTap, nullptr, nullptr, nullptr, setTick, setBack, setClose};
