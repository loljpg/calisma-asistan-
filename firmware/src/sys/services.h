#pragma once
// DAXEN VANE sistem katmanı · Sistem hizmetleri ve açılış ekranı.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// Sistem hizmetleri (arayüzün çağırdıkları)
// ============================================================================
static void wifiScan() {
  if (!wifiScanBusy) wifiScanReq = true;
}

static void wifiConnect(const String& ssid, const String& pass) {
  {
    Lock l;
    wifiConnSsid = ssid;
    wifiConnPass = pass;
  }
  wifiConnStart = millis();
  wifiConnReq = true;
}

static void wifiForget(const String& ssid) {
  {
    Lock l;
    wifiKnown.erase(std::remove_if(wifiKnown.begin(), wifiKnown.end(), [&](const WifiCred& c) { return c.ssid == ssid; }), wifiKnown.end());
    for (auto& n : wifiNets)
      if (n.ssid == ssid) n.known = false;
  }
  wifiKnownSave();
  if (WiFi.SSID() == ssid) {
    wifiManualOff = true;
    WiFi.disconnect(false, true);  // kayıtlı yapılandırmayı da sil
  }
}

static void wifiDisconnect() {
  wifiManualOff = true;
  WiFi.disconnect(false);
}

static bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
static String wifiSsid() { return WiFi.SSID(); }
static String wifiIp() { return WiFi.localIP().toString(); }
static int wifiRssi() { return WiFi.RSSI(); }

static void sysRequestOta() {
  {
    Lock l;
    prefs.begin("cs", false);
    prefs.putBool("otaman", true);
    prefs.end();
  }
  uiSplash("Yeniden başlatılıp denetlenecek…");
  delay(700);
  esp_ota_mark_app_valid_cancel_rollback();
  ESP.restart();
}

static void sysRestart() {
  uiSplash("Yeniden başlatılıyor…");
  delay(500);
  esp_ota_mark_app_valid_cancel_rollback();
  ESP.restart();
}

static uint32_t sysHeapFree() { return ESP.getFreeHeap(); }
static uint32_t sysHeapMin() { return ESP.getMinFreeHeap(); }
static uint32_t sysHeapMaxAlloc() { return ESP.getMaxAllocHeap(); }
static float sysTemp() { return temperatureRead(); }
static void weatherRefresh() { wantWeather = true; }

static void accountSet(const String& db, const String& key, const String& mail, const String& pass) {
  {
    Lock l;
    cfg.dbUrl = db;
    cfg.apiKey = key;
    cfg.email = mail;
    cfg.password = pass;
  }
  saveConfig();
  credsChanged = true;
}

static void runCalibration();
static void sysCalibrate() { runCalibration(); }

// ============================================================================
// Açılış ekranı ve güncelleme ilerlemesi (DAXEN VANE logosu)
// ============================================================================
static void splashLogo() {
  fillBg(0, 0, SCR_W, SCR_H);
  drawLogo(LOGO_L, LOGO_L_W, LOGO_L_H, (SCR_W - LOGO_L_W) / 2, 74);
}

// Logonun altında ince yükleme çizgisi ve kısa durum yazısı
static void uiSplash(const String& msg) {
  static bool drawn = false;
  static uint8_t stepN = 0;
  if (!drawn) {
    splashLogo();
    drawn = true;
  }
  stepN++;
  tft.fillRect(110, 168, 100, 2, C_LINE);
  int seg = 24, x = 110 + (stepN * 19) % (100 - seg);
  tft.fillRect(x, 168, seg, 2, C_ACC);
  fillBg(0, 180, SCR_W, 22);
  text(msg, 160, 190, F_XS, C_TEXT3, C_BG, MC_DATUM);
  fillBg(0, 214, SCR_W, 20);
  text("fw-" + String(FW_BUILD), 160, 224, F_XS, C_TEXT3, C_BG, MC_DATUM);
}

static void uiOtaProgress() {
  static int lastShown = -2;
  int p = otaProgress;
  if (p == lastShown) return;
  if (lastShown < 0) {
    splashLogo();
    text("Yazılım güncelleniyor", 160, 172, F_SM, C_TEXT, C_BG, MC_DATUM);
    text("Kapatma, bitince yeniden başlayacak", 160, 222, F_XS, C_TEXT3, C_BG, MC_DATUM);
  }
  lastShown = p;
  progress(60, 190, 200, 2, p, C_ACC);
  fillBg(60, 198, 200, 16);
  text("%" + String(p), 160, 205, F_XS, C_TEXT2, C_BG, MC_DATUM);
}
