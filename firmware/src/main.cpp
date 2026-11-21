// DAXEN VANE — ESP32 Cheap Yellow Display (ESP32-2432S028R) işletim yazılımı
//
// Masaüstü bir cihaz: ana ekran (saat, hava, kedi, YKS kartı), uygulamalar (YKS, Notlar, Dosyalar,
// Ayarlar, Medya, Bildirimler, Hesap makinesi, Saat, Hava durumu, Yapılacaklar, Oyunlar, Fotoğraflar,
// Okuyucu, Çizim, Bilgi kartları, Işık ölçer, Dosya aktarımı, Sistem, Görevler, Lua uygulamaları).
// Yazılım tek çeviri birimidir. Bu dosya açılışı ve çekirdeğin servislerini içerir; geri kalanı
// sırayla eklenir:
//   daxen.h   arayüz: core/ (ortak durum, yardımcılar), ui/ (tasarım sistemi), os/ (çekirdek, kabuk),
//             apps/ (uygulamalar)
//   sys/      sistem katmanı: kalıcı ayarlar, SD kart, ağ ve Firebase (docs/firebase.md), Bluetooth
//             (docs/bluetooth.md), web sunucusu, ışıklar, sistem hizmetleri
// Zamanlama os/kernel.h'tedir: 1 kHz zamanlayıcı kesmesi, servisler ve süreçler (Görevler uygulaması).

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <Update.h>
#include <NimBLEDevice.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <lwip/sockets.h>
#include <sys/time.h>
#include <time.h>
#include <vector>
#include <memory>
#include <algorithm>

#include "config.h"
#include "lua.hpp"  // lib/lua (PlatformIO kitaplık bulucu için burada da)
#include "daxen.h"
#include "core/touch.h"

#define STR_(x) #x
#define STR(x) STR_(x)
// SD karttan güncellemede dosyanın hangi sürüm olduğunu anlamak için yazılıma gömülen işaret.
static const char FW_MARKER[] = "CSFW:" FW_VARIANT ":" STR(FW_BUILD) ";";

// --- Sistem katmanı (sırası önemlidir) ---
#include "sys/store.h"
#include "sys/sdlog.h"
#include "sys/diag.h"
#include "sys/ota_roots.h"
#include "sys/net.h"
#include "sys/sdupdate.h"
#include "sys/events.h"
#include "sys/ble.h"
#include "os/calibrate.h"  // dokunmatik ayarı (9 nokta, doğrulama)
#include "sys/web.h"
#include "sys/power.h"
#include "sys/services.h"

// ============================================================================
// Başlangıç ve döngü
// ============================================================================
// Güncellemeden sonra yeni yazılım hemen "sağlam" sayılmaz: 45 sn sorunsuz çalışınca işaretlenir.
// Daha önce çökerse ESP32 açılışta bir önceki sürüme döner (kart USB'siz kurtulur).
extern "C" bool verifyRollbackLater() { return true; }

// Açılışta, Bluetooth başlamadan güncelleme denetimi: bellek bol, şifreli bağlantı rahat açılır.
static void bootOtaCheck() {
  bool manual;
  {
    Lock l;
    prefs.begin("cs", false);
    manual = prefs.getBool("otaman", false);
    if (manual) prefs.remove("otaman");
    prefs.end();
  }
  if (FW_BUILD == 0) return;
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < (manual ? 15000UL : 8000UL)) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    if (manual) setOtaMsg("Wi-Fi yok, güncelleme denetlenemedi");
    return;
  }
  uiSplash("Güncelleme denetleniyor…");
  otaInSetup = true;
  netPhase = "güncelleme";
  checkOta(manual);
  netPhase = "boşta";
  otaInSetup = false;
  otaProgress = -1;
}

static void diagBoot() {
  esp_reset_reason_t r = esp_reset_reason();
  const char* t;
  switch (r) {
    case ESP_RST_POWERON: t = "güç verildi"; break;
    case ESP_RST_EXT: t = "RST tuşu"; break;
    case ESP_RST_SW: t = "yazılım (güncelleme)"; break;
    case ESP_RST_PANIC: t = "çökme"; break;
    case ESP_RST_INT_WDT: t = "kesme bekçisi"; break;
    case ESP_RST_TASK_WDT: t = "görev bekçisi"; break;
    case ESP_RST_WDT: t = "bekçi"; break;
    case ESP_RST_BROWNOUT: t = "voltaj düştü"; break;
    case ESP_RST_DEEPSLEEP: t = "uyku"; break;
    default: t = "bilinmiyor"; break;
  }
  bootReason = String("Son açılış: ") + t;
  if (rtcMagic == 0xCA7D1A60 && r != ESP_RST_POWERON) {
    uint32_t m = rtcUpSec / 60;
    bootReason += " · önceki " + (m >= 60 ? String(String(m / 60) + " sa ") : String("")) + String(m % 60) + " dk, en az " +
                  String(rtcMinHeap / 1024) + " KB";
  }
  rtcMagic = 0xCA7D1A60;
  rtcUpSec = 0;
  rtcMinHeap = ESP.getFreeHeap();
}

// Kurulum portalıyla (eski sürümler) kaydedilmiş ağ kayıtlı ağlar listesine alınır.
static void wifiImportStored() {
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK || !conf.sta.ssid[0]) return;
  String s = String((const char*)conf.sta.ssid), p = String((const char*)conf.sta.password);
  bool have = false;
  {
    Lock l;
    for (auto& k : wifiKnown)
      if (k.ssid == s) have = true;
  }
  if (!have) wifiRemember(s, p);
}

static void svcTouch();
static void svcBle();
static void svcClock();
static void svcLight();

void setup() {
  diagBoot();
  // SPEAK soketindeki yükseltecin girişi boşta kalıp vızıldamasın (ses çıkışı yok). fw-v40 ve
  // öncesi yeniden başlarken bu pini sıfırda kilitliyordu; kilit burada çözülür.
  gpio_hold_dis((gpio_num_t)PIN_SPK);
  pinMode(PIN_SPK, OUTPUT);
  digitalWrite(PIN_SPK, LOW);
  Serial.begin(115200);
  mtx = xSemaphoreCreateRecursiveMutex();
  pinMode(PIN_BOOT, INPUT_PULLUP);

  ledcSetup(1, 5000, 8);
  ledcSetup(2, 5000, 8);
  ledcSetup(3, 5000, 8);
  ledcAttachPin(PIN_LED_R, 1);
  ledcAttachPin(PIN_LED_G, 2);
  ledcAttachPin(PIN_LED_B, 3);
  setLed(0, 0, 0);

  loadConfig();
  {
    Lock l;
    storeMigrate(prefs);  // bir kerelik ayar geçişleri (core/migrate.h)
  }
  wifiKnownLoad();
  themeApply();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(cfg.invert);
  tft.setSwapBytes(true);  // bütün görüntü dizileri sayısal RGB565
  fillBg(0, 0, SCR_W, SCR_H);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BL, 0);
  sysSetBacklight(cfg.bright);

  tsBegin();

  spr.setColorDepth(16);
  sprOk = spr.createSprite(SPR_W, SPR_H) != nullptr;
  if (sprOk) spr.loadFont(F_XL);

  Serial.println(FW_MARKER);
  uiSplash("SD kart denetleniyor…");
  sdOk = sdBegin();
  diagStart();  // günlük (RAM + SD), önceki çökmenin özeti
  if (sdOk) {
    sdTryUpdate();
    queueMigrate();
  }
  // Son alınan plan internet beklemeden gösterilir.
  String cached = loadCachedView();
  if (cached.length()) {
    JsonDocument d;
    if (!deserializeJson(d, cached)) {
      Lock l;
      parseView(d.as<JsonVariantConst>(), view);
    }
  }
  sentLoad();
  rebuildLocalEvents();
  pruneLocalEvents();
  timerRestore();

  // Açılışta 2 sn içinde BOOT'a basılırsa dokunmatik yeniden ayarlanır.
  bool calib = !cfg.calibrated;
  if (!calib) {
    uiSplash("Dokunmatik ayarı için BOOT'a bas");
    uint32_t t0 = millis();
    while (millis() - t0 < 2000) {
      if (digitalRead(PIN_BOOT) == LOW) {
        while (digitalRead(PIN_BOOT) == LOW) delay(10);
        calib = true;
        break;
      }
      delay(10);
    }
  }
  if (calib) runCalibration();

  uiSplash("Wi-Fi'ye bağlanıyor…");
  WiFi.mode(WIFI_STA);
  // Bluetooth açıkken Wi-Fi güç tasarrufu kipinde kalmalı (ikisi aynı anteni paylaşıyor).
  wifiImportStored();
  bool haveWifi;
  {
    Lock l;
    haveWifi = !wifiKnown.empty();
  }
  if (haveWifi) {
    WiFi.begin();  // son bağlanılan ağ; olmazsa ağ görevi kayıtlı ağlardan görüneni seçer
  }
  configTzTime("<+03>-3", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  if (haveWifi) bootOtaCheck();
  bleBegin();

  TaskHandle_t netH = nullptr;
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, &netH, 0);
  lastInteract = millis();
  homeShow();
  if (!haveWifi && !bleHasToken) settingsOpenPage(SP_WIFI);  // ilk kurulum: önce Wi-Fi
  else fOfferOldMusicModule();  // SD'de kalan eski müzik modülü bir kez sorulur

  // Çekirdek: zamanlayıcı kesmesi ve servisler (kayıt sırası öncelik sırasıdır)
  kernelBegin();
  kService("dokunma", 10, svcTouch);
  kService("kabuk", 16, shellTick);
  kService("bluetooth", 10, svcBle);
  kService("saat", 200, svcClock);
  kService("ışık", 50, svcLight);
  kAdopt("ağ", netH, 1, 0, 16384, "çoğunlukla ağı bekler");
  diagAdoptSystemTasks();  // Wi-Fi, TCP/IP ve Bluetooth görevleri Görevler'de görünsün (sys/diag.h)
}

// ============================================================================
// Çekirdek servisleri (os/kernel.h zamanlar)
// ============================================================================
static void svcTouch() {  // 10 ms: BOOT tuşu ve dokunmatik
  handleBoot();
  int tx = 0, ty = 0;
  bool t = getTouch(tx, ty);
  shellTouch(t, tx, ty);
}

static void svcBle() {  // 10 ms: telefondan gelenler
  bleProcess();
  applyRemoteTimer();
  if (dataDirty) {
    dataDirty = false;
    uiDataChanged();
  }
}

static void svcClock() {  // 200 ms: dakika işleri, Pomodoro, bakım, tanı
  static int lastMinute = -1;
  static uint32_t lastSec = 0;
  static String lastDay;
  static uint32_t lastDiag = 0;
  static bool markedValid = false;

  if (millis() - lastDiag > 5000) {
    lastDiag = millis();
    rtcUpSec = millis() / 1000;
    rtcMinHeap = ESP.getMinFreeHeap();
  }
  diagTick();
  if (!markedValid && millis() > 45000) {
    markedValid = true;
    esp_ota_mark_app_valid_cancel_rollback();
    sysLog("güncelleme", "45 sn sorunsuz: yazılım sağlam işaretlendi");
  }

  struct tm tmv;
  int minute = localNow(tmv) ? tmv.tm_hour * 60 + tmv.tm_min : -1;
  String day = todayStr();
  if (day != lastDay) {
    lastDay = day;
    dataDirty = true;
  }
  if (minute != lastMinute) {
    lastMinute = minute;
    // Gece bakımı: 04:00–04:30 arası, 20 saatten uzun açıksa ve kimse kullanmıyorsa yeniden başlar.
    // Açılışta güncelleme denetlenir, bellek de tazelenir. Kronometre ve kayıtlar korunur.
    if (minute >= 240 && minute < 270 && millis() > 20UL * 3600UL * 1000UL && !swRunning && swElapsedMs() == 0 &&
        millis() - lastInteract > 10UL * 60UL * 1000UL && !bleInflight && !kbm.active) {
      esp_ota_mark_app_valid_cancel_rollback();
      sysLog("açılış", "gece bakımı: yeniden başlatılıyor");
      ESP.restart();
    }
    // Yaklaşan oturum (2 dk içinde) LED ile haber verilir.
    upcomingSoon = false;
    int studied;
    View v;
    std::vector<Item> items = displayItems(studied, v);
    if (planIsToday(v) && minute >= 0 && !swRunning) {
      for (auto& it : items) {
        int s = toMin(it.saat);
        if (it.durum != "tamam" && s - minute >= 0 && s - minute <= 2) upcomingSoon = true;
      }
    }
  }
  uint32_t sec = swElapsedMs() / 1000;
  if (sec != lastSec) {
    lastSec = sec;
    if (swRunning && cfg.pomodoro) {
      if (sec >= (uint32_t)(pomoCount + 1) * POMODORO_WORK_SEC) {
        pomoCount++;
        pomoBreak = true;
        pomoBreakUntil = millis() + 5UL * 60UL * 1000UL;
        uiToast("Mola zamanı: 5 dakika ara ver");
      }
    }
    if (pomoBreak && (int32_t)(millis() - pomoBreakUntil) > 0) pomoBreak = false;
  }
}

static void svcLight() {  // 50 ms: RGB ışık ve ekran parlaklığı
  updateLeds();
  updateBacklight();
}

void loop() {
  if (otaProgress >= 0) {  // SD'den güncelleme sürerken yalnızca ilerleme
    uiOtaProgress();
    updateLeds();
    delay(50);
    return;
  }
  kernelLoop();
}
