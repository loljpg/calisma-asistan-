#pragma once
// Arayüz ile sistem çekirdeği arasındaki sözleşme.
// Sistem işlevleri main.cpp'de tanımlıdır; arayüz yalnızca bunları çağırır (donanıma dokunmaz).
// Arayüz kancaları (ui...) arayüz dosyalarında tanımlıdır; sistem onları çağırır.
#include "types.h"

// --- Plan ve kronometre ---
static std::vector<Item> displayItems(int& studied, View& snapshot);
static bool planIsToday(const View& v);
static bool hasCreds();
static int queueCount();
static void saveConfig();
static void toggleItem(const Item& it);
static void postponeItem(const Item& it);
static void startItemTimer(const Item& it);
static void selectForTimer(const Item& it);
static void timerToggleRun();
static void finishTimer();
static void timerReset();
static void setPomodoro(bool on);

// --- Telefon (Bluetooth) ---
static void bleMediaKey(const char* cmd);
static void blePairMode();
static void bleForget();
static String bleStatusText();

// --- Wi-Fi ---
static void wifiScan();
static void wifiConnect(const String& ssid, const String& pass);
static void wifiForget(const String& ssid);
static void wifiDisconnect();
static bool wifiConnected();
static String wifiSsid();
static String wifiIp();
static int wifiRssi();

// --- Sistem ---
static void sysRequestOta();
static void sysRestart();
static void sysSetBacklight(int level);
static int sysLight();
static int sysLightPct();  // ortam ışığı %0 (karanlık) - %100 (aydınlık)
static void sysLed(uint8_t r, uint8_t g, uint8_t b);
static void sysCalibrate();
static bool sysWake();  // gece kısıkken ekranı açar; kısıktıysa true
static uint32_t sysHeapFree();
static uint32_t sysHeapMin();
static uint32_t sysHeapMaxAlloc();
static String sysChipText();
static uint32_t sysSketchSize();  // çalışan yazılımın boyutu
static uint32_t sysSketchFree();  // güncelleme yuvasının boyutu  // ör. "ESP32 rev3 · 240 MHz · 4 MB"
static void weatherRefresh();
struct GeoHit {
  String name, region;
  float lat = 0, lon = 0;
};
static bool weatherSearch(const String& q, std::vector<GeoHit>& out);  // Open-Meteo şehir arama (engelleyici)
static void accountSet(const String& db, const String& key, const String& mail, const String& pass);

// --- Web sunucusu (dosya aktarımı ve hesap sayfası) ---
static bool webStart();
static void webStop();
static bool webRunning();
static String webStatus();
static int webProgress();  // yükleme sürüyorsa yüzde, yoksa -1
static int webUploadCount();
static String webCode();

// --- Ağ teşhisi (sys/netdiag.h) ---
static bool ndStart(uint8_t job, const String& host, int arg);  // false: iş sürüyor ya da bellek yok (ndError)
static bool ndBusy();
static void ndCancel();
static String ndError();
static void ndConnInfo(std::vector<String>& out);  // bağlı ağın ayrıntıları (anında)
static void ndSaved(std::vector<String>& out);     // kayıtlı ağlar  // tarayıcının giriş kodu (sunucu kapalıysa boş)

// --- Arayüz kancaları ---
static void uiToast(const String& msg);
static void uiNotif(const Notif& n);
static void uiNotifsChanged();
static void uiMediaChanged(bool full);
static void uiArtChanged();
static void uiTimerChanged();
static void uiPaletteChanged();
static void uiOtaProgress();
static void uiSplash(const String& msg);
static void uiDataChanged();
