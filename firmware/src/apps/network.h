#pragma once
// Ağ: bağlı Wi-Fi'nin ayrıntıları ve bağlantı testi (ağ geçidi, DNS, internet), çevredeki erişim
// noktalarının taranması (her BSSID ayrı: sinyal, kanal, güvenlik) ve yakındaki Bluetooth
// cihazları (yalnız reklam paketleri; sys/blescan.h). Wi-Fi işleri sys/netdiag.h'deki "ağ-araç"
// sürecinde çalışır; ekrandan çıkınca işler durur. Daha fazla araç Terminal'de (wifi …, bluetooth …).
#include "../os/shell.h"

static uint8_t nwTab = 0;              // 0 bağlantı, 1 Wi-Fi, 2 Bluetooth
static bool nwDev = false;             // Bluetooth sekmesinde bir cihazın ayrıntısı açık
static uint8_t nwDevAddr[6], nwDevType = 0;
static uint32_t nwBtAt = 0;            // Bluetooth listesinin son çizimi
static std::vector<String> nwLines;    // bağlantı sekmesinde gösterilen satırlar
static bool nwTested = false;          // satırlar test çıktısı mı
static int nwTop = 0;
static bool nwWas = false;             // son çizimde iş sürüyor muydu
static const int NW_Y = HDR_H + 28, NW_BTN_Y = 204, NW_RH = 14, NW_ROWS = 11, NW_AP_H = 34, NW_AP_ROWS = 4;

static void nwAdd(const std::vector<String>& l) {  // ekrana sığmayan satırlar bölünür
  for (auto& s : l)
    for (auto& part : wrapText(s.length() ? s : String(" "), F_XS, 296)) nwLines.push_back(part);
}

static void nwInfo() {
  nwLines.clear();
  nwTested = false;
  std::vector<String> l;
  ndConnInfo(l);
  nwAdd(l);
}

static void nwDrawTabs() {
  fillBg(0, HDR_H, SCR_W, 28);
  const char* t[] = {"Bağlantı", "Wi-Fi", "Bluetooth"};
  for (int i = 0; i < 3; i++) {
    int x = 4 + i * 104;
    text(t[i], x + 52, HDR_H + 12, F_SM, i == nwTab ? C_TEXT : C_TEXT3, C_BG, MC_DATUM);
    if (i == nwTab) tft.fillRect(x + 12, HDR_H + 24, 80, 2, C_ACC);
  }
  hline(0, HDR_H + 27, SCR_W);
}

static void nwDrawLines(const std::vector<String>& lines, uint16_t col) {
  int total = lines.size();
  nwTop = constrain(nwTop, 0, std::max(0, total - NW_ROWS));
  for (int r = 0; r < NW_ROWS && nwTop + r < total; r++) text(lines[nwTop + r], 10, NW_Y + 2 + r * NW_RH, F_XS, col, C_BG);
  if (total > NW_ROWS) scrollBar(NW_Y, NW_ROWS * NW_RH, nwTop, NW_ROWS, total);
}

// Tarama listesinde bir satır (Wi-Fi ve Bluetooth ortak): ad, sinyal çubuğu, dBm, alt satır
static void nwRow(int r, const char* title, bool dim, int rssi, const String& sub) {
  int y = NW_Y + r * NW_AP_H;
  char db[12];
  snprintf(db, sizeof db, "%d dBm", rssi);
  text(fit(title, F_SM, 168), 10, y + 3, F_SM, dim ? C_TEXT3 : C_TEXT, C_BG);
  progress(184, y + 11, 44, 3, rssiQuality(rssi), rssi >= -72 ? C_ACC : C_TEXT3);
  text(db, 300, y + 3, F_SM, C_TEXT, C_BG, TR_DATUM);
  text(fit(sub, F_XS, 290), 10, y + 19, F_XS, C_TEXT3, C_BG);
  hline(10, y + NW_AP_H - 1, SCR_W - 20);
}

static void nwDrawBt() {
  if (nwDev) {
    BleDev d;
    if (bsFind(nwDevAddr, nwDevType, d)) {
      std::vector<String> l, w;
      bleDetail(d, millis(), l);
      for (auto& s : l)
        for (auto& part : wrapText(s, F_XS, 296)) w.push_back(part);
      nwDrawLines(w, C_TEXT);
      return;
    }
    nwDev = false;
  }
  int total = bsCount();
  if (!total) {
    text(bsBusy() ? "Taranıyor…" : "Taramak için aşağıdaki düğmeye dokun", SCR_W / 2, NW_Y + 60, F_SM, C_TEXT3, C_BG, MC_DATUM);
    text(BLE_ONLY_ADS, SCR_W / 2, NW_Y + 84, F_XS, C_TEXT3, C_BG, MC_DATUM);
    return;
  }
  nwTop = constrain(nwTop, 0, std::max(0, total - NW_AP_ROWS));
  BleDev d[NW_AP_ROWS];
  int got = bsGet(nwTop, d, NW_AP_ROWS);
  for (int r = 0; r < got; r++) nwRow(r, bleTitle(d[r]), !d[r].name[0], d[r].rssi, bleSub(d[r]));
  if (total > NW_AP_ROWS) scrollBar(NW_Y, NW_AP_ROWS * NW_AP_H, nwTop, NW_AP_ROWS, total);
}

static void nwDrawBody() {
  fillBg(0, NW_Y, SCR_W, NW_BTN_Y - NW_Y - 2);
  nwBtAt = millis();
  if (nwTab == 2) {
    nwDrawBt();
    return;
  }
  if (nwTab == 0) {
    nwDrawLines(nwLines, nwTested ? C_TEXT2 : C_TEXT);
    return;
  }
  std::vector<WifiAp> aps;
  {
    Lock l;
    aps = wifiAps;
  }
  if (aps.empty()) {
    text(ndBusy() ? "Taranıyor…" : "Taramak için aşağıdaki düğmeye dokun", SCR_W / 2, NW_Y + 70, F_SM, C_TEXT3, C_BG, MC_DATUM);
    return;
  }
  int total = aps.size();
  nwTop = constrain(nwTop, 0, std::max(0, total - NW_AP_ROWS));
  for (int r = 0; r < NW_AP_ROWS && nwTop + r < total; r++) {
    const WifiAp& a = aps[nwTop + r];
    char sub[64];
    snprintf(sub, sizeof sub, "kanal %u · %s · %s", a.ch, authText(a.auth), macText(a.bssid).c_str());
    nwRow(r, a.ssid.length() ? a.ssid.c_str() : "(gizli ağ)", !a.ssid.length(), a.rssi, sub);
  }
  if (total > NW_AP_ROWS) scrollBar(NW_Y, NW_AP_ROWS * NW_AP_H, nwTop, NW_AP_ROWS, total);
}

static void nwDrawButton() {
  fillBg(0, NW_BTN_Y - 2, SCR_W, SCR_H - NW_BTN_Y + 2);
  if (nwTab == 2) {
    bool b = bsBusy();
    char label[32];
    snprintf(label, sizeof label, nwDev ? "Listeye dön" : b ? "Durdur · %d cihaz" : "Tara (10 sn)", bsCount());
    button(12, NW_BTN_Y, SCR_W - 24, 30, label, b || nwDev ? B_NORMAL : B_PRIMARY, nwDev ? I_BACK : b ? I_CLOSE : I_BT);
    nwWas = b;
    return;
  }
  bool busy = ndBusy();
  const char* label = busy ? "Durdur" : nwTab == 1 ? "Tara" : nwTested ? "Bağlantı bilgisine dön" : "Bağlantıyı test et";
  button(12, NW_BTN_Y, SCR_W - 24, 30, label, busy ? B_NORMAL : B_PRIMARY, busy ? I_CLOSE : nwTab == 1 ? I_WIFI : I_PULSE);
  nwWas = busy;
}

static void nwOpen() {
  nwTab = 0;
  nwTop = 0;
  nwDev = false;
  nwInfo();
}

static void nwDraw(bool full) {
  if (full) {
    header("Ağ");
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  nwDrawTabs();
  nwDrawBody();
  nwDrawButton();
}

static void nwSetTab(uint8_t t) {
  if (t == nwTab || t > 2) return;
  if (nwTab == 2) bsStop();  // Bluetooth taraması sekmeyle biter
  nwTab = t;
  nwTop = 0;
  nwDev = false;
  nwDraw(false);
}

static void nwRun(uint8_t job) {
  auto go = [job]() {
    ndToSerial = false;
    std::vector<String> drop;
    ndDrain(drop);  // önceki işten kalanlar
    if (!ndStart(job, "", 0)) {
      toast(ndError());
      return;
    }
    if (job == ND_TEST) {
      nwLines.clear();
      nwTested = true;
    }
    nwTop = 0;
    nwDrawBody();
    nwDrawButton();
  };
  if (ndConsented) {
    go();
    return;
  }
  dialog("Ağ araçları", "Yalnızca kullanma iznin olan ağlarda teşhis için kullan. Devam edilsin mi?", {"Vazgeç", "Devam"}, [go](int c) {
    if (c != 1) return;
    ndConsented = true;
    go();
  });
}

static void nwTapBt(int y) {
  if (y < NW_BTN_Y - 2) {  // satıra dokunuldu: ayrıntı
    if (nwDev || y < NW_Y) return;
    BleDev d;
    if (bsGet(nwTop + (y - NW_Y) / NW_AP_H, &d, 1) != 1) return;
    memcpy(nwDevAddr, d.addr, 6);
    nwDevType = d.addrType;
    nwDev = true;
    nwTop = 0;
  } else if (nwDev) {
    nwDev = false;
    nwTop = 0;
  } else if (bsBusy()) {
    bsStop();
  } else if (!bsStart(10000, false)) {
    toast(bsError());
  }
  nwDrawBody();
  nwDrawButton();
}

static void nwTap(int x, int y) {
  if (y >= HDR_H && y < HDR_H + 28) {
    nwSetTab(std::min(2, std::max(0, (x - 4) / 104)));
    return;
  }
  if (nwTab == 2) {
    nwTapBt(y);
    return;
  }
  if (y < NW_BTN_Y - 2) return;
  if (ndBusy()) ndCancel();
  else if (nwTab == 1) nwRun(ND_SCAN);
  else if (nwTested) {
    nwInfo();
    nwTop = 0;
    nwDrawBody();
  } else nwRun(ND_TEST);
  nwDrawButton();
}

static void nwSwipe(int dir) {
  if (dir == SW_LEFT || dir == SW_RIGHT) {
    if (dir == SW_LEFT) nwSetTab(nwTab + 1);
    else if (nwTab) nwSetTab(nwTab - 1);
    return;
  }
  int old = nwTop;
  nwTop += dir == SW_UP ? (nwTab == 2 && !nwDev ? 2 : 3) : (nwTab == 2 && !nwDev ? -2 : -3);
  nwTop = std::max(0, nwTop);
  if (nwTop != old) nwDrawBody();
}

static void nwTick() {
  static uint32_t at = 0;
  if (millis() - at < 200) return;
  at = millis();
  if (nwTab == 2) {  // canlı liste: tarama sürerken saniyede bir çizilir
    bool b = bsBusy();
    if (b != nwWas || (b && millis() - nwBtAt > 1000)) {
      nwDrawBody();
      nwDrawButton();
    }
    return;
  }
  std::vector<String> l;
  if (!ndToSerial) ndDrain(l);
  bool busy = ndBusy();
  if (nwTested && !l.empty()) {
    nwAdd(l);
    nwTop = std::max(0, (int)nwLines.size() - NW_ROWS);
  }
  if (!l.empty() || busy != nwWas) {
    nwDrawBody();
    nwDrawButton();
  }
}

static void nwClose() {
  if (!ndToSerial) ndCancel();
  bsRelease(false);
  nwLines.clear();
  nwLines.shrink_to_fit();
}

// Dark Radar (gizli): Bluetooth sekmesinin adına uzun basış
static bool nwHold(int x, int y) {
  if (y < HDR_H || y >= HDR_H + 28 || x < 212) return false;
  gizliAc("radar.lua");
  return true;
}

static const App APP_NETWORK = {"Ağ", "Ağ", I_GLOBE, nwOpen, nwDraw, nwTap, nwHold, nwSwipe, nullptr, nwTick, nullptr, nwClose};
