#pragma once
// Ortak durum. İki görev (ağ: çekirdek 0, arayüz: çekirdek 1) arasında paylaşılanlar kilitle korunur.
#include <Preferences.h>
#include "types.h"

static SemaphoreHandle_t mtx;
struct Lock {
  Lock() { xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(mtx); }
};

static Config cfg;
static View view;
static std::vector<LocalEvent> localEvents;
static std::vector<String> outbox;
static Weather weather;
static String netMsg = "Başlatılıyor";
static bool cloudOk = false;
static String otaMsg;

static volatile bool dataDirty = true;
static volatile bool netBusy = false;
static volatile bool wantView = false;
static volatile bool credsChanged = false;
static volatile int otaProgress = -1;
static volatile bool wantTimer = false;
static volatile bool liveOn = false;  // canlı akış açık ve veri geliyor
static volatile bool wantWeather = false;

static SharedTimer outTimer, remoteTimer;
static bool outTimerPending = false, remoteTimerDirty = false;
static long long timerRevSeen = 0;

static bool sdOk = false;
static volatile bool assetsFresh = false;  // varlık paketi yeni indi: ekran baştan çizilsin (sys/assets.h)
static Preferences prefs;

// Bluetooth: bağlıyken telefon buluta aracılık eder, kart kendi bulut işlerini durdurur.
static volatile bool bleConnected = false;  // telefon bağlı
static volatile bool bleLinked = false;     // eşleşme anahtarı doğrulandı
static volatile bool fbFlushing = false;    // kuyruk şu an buluta gönderiliyor
static int bleInflight = 0;                 // Bluetooth'tan gönderilip onayı beklenen olay sayısı
static bool bleTimerPending = false;
static uint32_t blePairUntil = 0;           // eşleşme kipi bitişi (millis)
static bool bleHasToken = false;            // bir telefonla eşleşmiş mi

// Yeniden başlama tanısı: RTC belleği yazılım sıfırlamalarında ve çökmelerde silinmez.
RTC_NOINIT_ATTR uint32_t rtcMagic, rtcUpSec, rtcMinHeap;
static String bootReason;
static const char* volatile netPhase = "boşta";
// Bulut bağlantısını geçici durduran kullanıcı işleri (bit: 1 ağ aracı, 2 web sunucusu). Canlı akışın
// açık TLS bağlantısı on binlerce bayt tutar; kullanıcının başlattığı iş için bırakılır, iş bitince döner.
enum : uint8_t { HOLD_NETTOOL = 1, HOLD_WEB = 2 };
static volatile uint8_t netHold = 0;  // ağ görevi o an ne yapıyor (bellek günlüğü için)
static String diagLastCrash;  // önceki çökmenin özeti (açılışta çekirdek dökümünden)

// Hava durumu
static int weatherHttp = 0;
static uint32_t weatherAt = 0;

// Kronometre (YKS)
static bool swRunning = false;
static uint32_t swStart = 0, swAccMs = 0;
static String swItemId, swDers, swKonu;
static int swDk = 0;
static int pomoCount = 0;
static bool pomoBreak = false;
static uint32_t pomoBreakUntil = 0;
static uint32_t swElapsedMs() { return swAccMs + (swRunning ? millis() - swStart : 0); }

// Telefondan gelen bildirimler
static std::vector<Notif> notifs;    // en yeni başta
static std::vector<IconImg*> icons;  // en fazla 12 uygulama simgesi
static Notif banner;

// Telefonda çalan müzik
static MediaState media;
static uint16_t* artPx = nullptr;
static uint32_t artId = 0;
static bool accentOn = false;  // kapak rengi geldi
static uint8_t accRgb[3] = {120, 146, 165}, darkRgb[3] = {21, 21, 21};

// Ekran ışığı ve etkileşim
static uint32_t wakeUntil = 0;
static bool blDim = false;
static uint32_t lastInteract = 0;  // son dokunuş ya da BOOT
static bool upcomingSoon = false;

// Wi-Fi (Ayarlar → Wi-Fi). Tarama ve bağlanma ağ görevinde yapılır.
static std::vector<WifiNet> wifiNets;
static std::vector<WifiCred> wifiKnown;
static volatile bool wifiScanReq = false, wifiScanBusy = false, wifiConnReq = false;
static volatile uint32_t wifiScanAt = 0;  // son taramanın bittiği an
static String wifiConnSsid, wifiConnPass;
static volatile uint32_t wifiConnStart = 0;  // kullanıcının istediği bağlantı denemesi başladı
static String wifiLastError;
