#pragma once
// Bilgisayarda çalıştırma: sistem işlevlerinin sahteleri, örnek veri, dokunma ve ekran yardımcıları.
// Önizleme (preview.cpp) ve birim testleri (tests.cpp) ortak kullanır.
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
extern uint32_t g_ms;
extern uint16_t g_ldr;
#include "config.h"
#include "daxen.h"

// ---------------------------------------------------------------------------
// Sistem işlevlerinin sahteleri
// ---------------------------------------------------------------------------
static bool g_wifi = true, g_ble = true;
static std::vector<Item> sampleItems() {
  std::vector<Item> v;
  const char* r[][5] = {{"08:30", "Matematik", "Türev", "tamam", "40"},     {"09:20", "Fizik", "Elektrik", "tamam", "40"},
                        {"10:10", "Kimya", "Mol kavramı", "bekliyor", "30"}, {"11:00", "Türkçe", "Paragraf", "ertelendi", "30"},
                        {"13:30", "Biyoloji", "Hücre", "bekliyor", "40"},    {"14:20", "Tarih", "Osmanlı", "bekliyor", "30"},
                        {"15:10", "Geometri", "Üçgenler", "bekliyor", "40"}};
  for (auto& x : r) {
    Item it;
    it.id = String(x[0]);
    it.saat = x[0];
    it.ders = x[1];
    it.konu = x[2];
    it.durum = x[3];
    it.dk = atoi(x[4]);
    v.push_back(it);
  }
  return v;
}
static std::vector<Item> displayItems(int& studied, View& snapshot) {
  snapshot = view;
  studied = view.studiedMin;
  return view.items;
}
static bool planIsToday(const View& v) { return v.valid && v.date == todayStr(); }
static bool hasCreds() { return cfg.dbUrl.length() > 0; }
static int queueCount() { return 3; }
static void saveConfig() {}
static void wifiKnownSave() {}
static int g_backups = 0;
static bool backupSave(bool secrets, String& m) {
  g_backups += secrets ? 10 : 1;
  m = "Yedeklendi: " BACKUP_FILE;
  return true;
}
static bool backupLoad(String& m) {
  m = "Geri yüklendi";
  return true;
}
static void toggleItem(const Item& it) {
  for (auto& x : view.items)
    if (x.id == it.id) x.durum = x.durum == "tamam" ? "bekliyor" : "tamam";
  shellRedraw(false);
  uiToast("Tamamlandı");
}
static void postponeItem(const Item&) { uiToast("Ertelendi"); }
static void startItemTimer(const Item& it) {
  swItemId = it.id;
  swDers = it.ders;
  swKonu = it.konu;
  swDk = it.dk;
  swRunning = true;
  swStart = millis();
}
static void selectForTimer(const Item& it) {
  swItemId = it.id;
  swDers = it.ders;
  swKonu = it.konu;
  swDk = it.dk;
}
static void timerToggleRun() {
  if (swRunning) {
    swAccMs = swElapsedMs();
    swRunning = false;
  } else {
    swStart = millis();
    swRunning = true;
  }
  uiTimerChanged();
}
static void finishTimer() { uiToast("25 dk kaydedildi"); }
static void timerReset() {
  swRunning = false;
  swAccMs = 0;
  uiTimerChanged();
}
static void setPomodoro(bool on) { cfg.pomodoro = on; }
static void bleMediaKey(const char* c) { printf("[medya %s]\n", c); }
static void blePairMode() { blePairUntil = millis() + 120000; }
static void bleForget() { bleHasToken = false; }
static String bleStatusText() { return bleLinked ? "Bağlı" : (bleHasToken ? "Bekleniyor" : "Eşleşmedi"); }
static void wifiScan() {
  wifiNets.clear();
  const char* n[] = {"EvAgi_5G", "EvAgi", "TurkTelekom_ZT4K", "Kafe Misafir", "DIRECT-HP Yazıcı", "Komsu"};
  int rs[] = {-48, -52, -67, -71, -80, -88};
  for (int i = 0; i < 6; i++) {
    WifiNet w;
    w.ssid = n[i];
    w.rssi = rs[i];
    w.secure = i != 3;
    w.known = i == 1;
    wifiNets.push_back(w);
  }
}
static void wifiConnect(const String& s, const String&) { printf("[wifi bağlan %s]\n", s.c_str()); }
static void wifiForget(const String&) {}
static void wifiDisconnect() {}
static bool wifiConnected() { return g_wifi; }
static String wifiSsid() { return "EvAgi"; }
static String wifiIp() { return "192.168.1.22"; }
static int wifiRssi() { return -54; }
static void sysRequestOta() {}
static void sysRestart() {}
static void sysSetBacklight(int) {}
static int sysLight() { return g_ldr; }
static int sysLightPct() { return 100 - g_ldr * 100 / 4095; }
static void sysLed(uint8_t, uint8_t, uint8_t) {}
static void sysCalibrate() {}
static uint32_t sysHeapFree() { return 96 * 1024; }
static uint32_t sysHeapMin() { return 61 * 1024; }
static uint32_t sysHeapMaxAlloc() { return 52 * 1024; }
static uint32_t sysSketchSize() { return 1858037; }
static uint32_t sysSketchFree() { return 1966080; }
static String sysChipText() { return "ESP32 rev3 · 240 MHz · 4 MB"; }
static void weatherRefresh() {}
static void accountSet(const String& db, const String& key, const String& mail, const String& pass) {
  cfg.dbUrl = db;
  cfg.apiKey = key;
  cfg.email = mail;
  cfg.password = pass;
}
static bool webOn = false;
static bool webStart() { return webOn = true; }
static void webStop() { webOn = false; }
static bool webRunning() { return webOn; }
static String g_webErr;
static String webError() { return g_webErr; }
static String webStatus() { return "Alındı: Sezen Aksu - Gidiyorum.mp3 (3,3 MB)"; }
static int webProgress() { return -1; }
static int webUploadCount() { return 4; }
static String webCode() { return webOn ? "482913" : ""; }
// Ağ teşhisi: bilgisayarda gerçek ağ yok; işler anında biter ve sabit, gerçekçi çıktı üretir
static bool g_ndBusy = false;
static String g_ndErr;
static bool ndBusy() { return g_ndBusy; }
static void ndPoll() {}
static bool netLiveOpen() { return false; }
static void ndCancel() { g_ndBusy = false; }
static String ndError() { return g_ndErr; }
static bool ndStart(uint8_t job, const String& host, int arg) {
  if (!g_wifi && job != ND_SCAN) {
    ndEmit("Wi-Fi'ye bağlı değil");
    return true;
  }
  switch (job) {
    case ND_SCAN: {
      std::vector<WifiAp> aps(3);
      aps[0].ssid = "EvAgi", aps[0].rssi = -54, aps[0].ch = 6, aps[0].auth = 3;
      aps[1].ssid = "Komsu_5G", aps[1].rssi = -71, aps[1].ch = 11, aps[1].auth = 7;
      aps[2].ssid = "", aps[2].rssi = -88, aps[2].ch = 1, aps[2].auth = 0;
      for (int i = 0; i < 3; i++)
        for (int k = 0; k < 6; k++) aps[i].bssid[k] = 0x10 * (i + 1) + k;
      wifiAps = aps;
      ndEmit("3 erişim noktası (güçlüden zayıfa):");
      break;
    }
    case ND_PING: {
      PingStats st;
      ndEmit("ping " + host);
      for (int i = 0; i < arg; i++) {
        st.add(i != 2, 3 + i);
        ndEmit(i != 2 ? "  yanıt 192.168.1.1  " + String(3 + i) + " ms" : String("  yanıt yok"));
      }
      ndEmit("  " + st.summary());
      break;
    }
    case ND_TEST:
      ndEmit("Bağlantı testi");
      ndEmit("1) Ağ geçidi 192.168.1.1");
      ndEmit("  3 gönderildi, 3 alındı, %0 kayıp · en az/ort/en çok 2/3/5 ms");
      ndEmit("2) DNS");
      ndEmit("  github.com → 140.82.121.4  38 ms");
      ndEmit("3) İnternet");
      ndEmit("  tcp 140.82.121.4:443  bağlandı 61 ms");
      ndEmit("Sonuç: ağ geçidi var · DNS çalışıyor · internet var");
      break;
    default:
      ndEmit(host + " " + String(arg));
  }
  return true;
}
static void ndConnInfo(std::vector<String>& o) {
  if (!g_wifi) {
    o.push_back("Wi-Fi'ye bağlı değil");
    return;
  }
  o.push_back("Ağ: EvAgi");
  o.push_back("Sinyal: -54 dBm · %92 · iyi");
  o.push_back("BSSID: 10:11:12:13:14:15 · kanal 6");
  o.push_back("Adres: 192.168.1.22 / 255.255.255.0");
  o.push_back("Ağ geçidi: 192.168.1.1");
  o.push_back("DNS: 192.168.1.1");
  o.push_back("Kartın MAC'i: 24:6F:28:AA:BB:CC");
}
static void ndSaved(std::vector<String>& o) { o.push_back("  EvAgi  (şifreli)"); }
// Bluetooth keşfi: gerçek radyo yok; tarama anında biter ve aşağıdaki reklam paketleri gerçek
// biçimleriyle çözücüden (core/blecalc.h) geçirilir
static BleDev g_bs[BLE_LIST_CAP];
static int g_bsN = 0;
static uint16_t g_bsDrop = 0;
static uint32_t g_bsPk = 0;
static bool g_bsBusy = false, g_bsDone = false, g_bsOk = true;
static void bsFeed(const char* mac, uint8_t type, int8_t rssi, uint8_t evt, const std::vector<uint8_t>& ad) {
  BleDev d;
  bleDevInit(d);
  d.addrType = type;
  for (int i = 0; i < 6; i++) d.addr[i] = strtol(mac + i * 3, nullptr, 16);
  d.rssi = rssi;
  d.flags |= evt <= 1 ? BD_CONN : evt <= 3 ? BD_NOCONN : 0;
  bleParseAd(ad.data(), ad.size(), d);
  bleMerge(g_bs, g_bsN, BLE_LIST_CAP, d, millis(), g_bsDrop);
  g_bsPk++;
}
static void bsSample() {
  g_bsN = 0, g_bsDrop = 0, g_bsPk = 0;
  // Mi Band: ad, Huami üretici verisi, 0xFEE0 servis
  bsFeed("C8:0F:10:4A:22:91", 0, -58, 0, {2, 1, 6, 10, 9, 'M', 'i', ' ', 'B', 'a', 'n', 'd', ' ', '7', 5, 0xFF, 0x57, 0x01, 1, 2, 3, 3, 0xE0, 0xFE});
  // iPhone: yalnız Apple üretici verisi, gizlilik adresi
  bsFeed("5E:33:A1:07:9C:4B", 1, -67, 0, {2, 1, 0x1A, 7, 0xFF, 0x4C, 0x00, 0x10, 0x05, 0x01, 0x18});
  bsFeed("5E:33:A1:07:9C:4B", 1, -63, 0, {2, 1, 0x1A, 7, 0xFF, 0x4C, 0x00, 0x10, 0x05, 0x01, 0x18});
  // Kulaklık: ad tarama yanıtında, görünüm "Kulaklık"
  bsFeed("F4:4E:FD:12:34:56", 0, -71, 0, {2, 1, 6, 3, 0x19, 0x41, 0x09, 3, 3, 0x0F, 0x18});
  bsFeed("F4:4E:FD:12:34:56", 0, -70, 4, {13, 9, 'J', 'B', 'L', ' ', 'T', 'u', 'n', 'e', ' ', '5', '2', '0', 2, 0x0A, 0xF8});
  // Eddystone işaretçi (bağlanamaz, servis verisi)
  bsFeed("D1:22:33:44:55:66", 1, -84, 3, {2, 1, 6, 3, 3, 0xAA, 0xFE, 6, 0x16, 0xAA, 0xFE, 0x10, 0xF4, 0x00});
}
static bool bsStart(uint32_t, bool report) {
  if (!g_bsOk) return false;
  bsSample();
  g_bsDone = report;
  return true;
}
static void bsStop() { g_bsBusy = false; }
static bool bsBusy() { return g_bsBusy; }
static String bsError() { return "Bluetooth hazır değil"; }
static int bsCount() { return g_bsN; }
static int bsGet(int first, BleDev* out, int max) {
  uint8_t idx[BLE_LIST_CAP];
  bleOrder(g_bs, g_bsN, idx);
  int got = 0;
  for (int i = first; i >= 0 && i < g_bsN && got < max; i++) out[got++] = g_bs[idx[i]];
  return got;
}
static bool bsFind(const uint8_t* a, uint8_t t, BleDev& out) {
  for (int i = 0; i < g_bsN; i++)
    if (g_bs[i].addrType == t && !memcmp(g_bs[i].addr, a, 6)) return out = g_bs[i], true;
  return false;
}
static void bsStats(uint32_t& p, uint16_t& d) { p = g_bsPk, d = g_bsDrop; }
static bool bsTakeDone() { return g_bsDone ? (g_bsDone = false, true) : false; }
static void bsRelease(bool) { g_bsN = 0, g_bsBusy = false; }
static void bsInfo(std::vector<String>& o) {
  o.push_back("Telefon: bağlı · MTU 247");
  o.push_back("Kartın adresi: 24:6F:28:AA:BB:CE · ad " BLE_NAME);
  o.push_back("Reklam: kapalı");
  o.push_back("Tarama: yok (bluetooth scan)");
}
static void bsPoll() {}
static bool weatherSearch(const String& q, std::vector<GeoHit>& out) {
  out.push_back({"İstanbul", "İstanbul, Türkiye", 41.01f, 28.97f});
  out.push_back({"Istanbul", "Ohio, ABD", 40.f, -83.f});
  return true;
}
static void uiSplash(const String&) {}
static void uiOtaProgress() {}
static bool sysWake() { return false; }

#include "core/touch.h"
#include "os/calibrate.h"

// ---------------------------------------------------------------------------
// Çıktı ve dokunma
// ---------------------------------------------------------------------------
extern bool g_ms_hook;
extern std::string g_sdroot, g_outdir;

// --- Önizleme SD yardımcıları (DAXEN_SD altında) ---
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
static void sdMkdirs(const char* vp) {
  std::string p = g_sdroot;
  std::string v = vp;
  size_t i = 1;
  while (i <= v.size()) {
    size_t j = v.find('/', i);
    if (j == std::string::npos) j = v.size();
    p = g_sdroot + v.substr(0, j);
    mkdir(p.c_str(), 0755);
    i = j + 1;
  }
}
static void sdWrite(const char* vp, const char* text) {
  FILE* f = fopen((g_sdroot + vp).c_str(), "wb");
  if (!f) return;
  fputs(text, f);
  fclose(f);
}
static void sdRemoveTreeAbs(const std::string& p) {
  DIR* d = opendir(p.c_str());
  if (!d) {
    unlink(p.c_str());
    return;
  }
  while (dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n == "." || n == "..") continue;
    sdRemoveTreeAbs(p + "/" + n);
  }
  closedir(d);
  rmdir(p.c_str());
}
static void sdRemoveTree(const char* vp) {
  if (g_sdroot.size() < 2 || !vp || vp[0] != '/' || !vp[1]) return;  // kök asla silinmez
  sdRemoveTreeAbs(g_sdroot + vp);
}

static void shot(const char* name) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s.ppm", g_outdir.c_str(), name);
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6 320 240 255\n");
  uint16_t* b = tft.buf();
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t c = b[i];
    uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}
static void step(int ms) {
  for (int i = 0; i < ms; i += 20) {
    g_ms += 20;
    shellTick();
  }
}
static void tap(int x, int y) {
  shellTouch(true, x, y);
  g_ms += 30;
  shellTouch(true, x, y);
  g_ms += 60;
  shellTouch(false, 0, 0);
  g_ms += 50;
  shellTouch(false, 0, 0);
}
static void hold(int x, int y) {
  shellTouch(true, x, y);
  for (int i = 0; i < 40; i++) {
    g_ms += 20;
    shellTouch(true, x, y);
  }
  g_ms += 60;
  shellTouch(false, 0, 0);
  g_ms += 50;
  shellTouch(false, 0, 0);
}
static void drag(int x0, int y0, int x1, int y1) {
  shellTouch(true, x0, y0);
  for (int i = 1; i <= 10; i++) {
    g_ms += 20;
    shellTouch(true, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10);
  }
  g_ms += 60;
  shellTouch(false, 0, 0);
  g_ms += 50;
  shellTouch(false, 0, 0);
}

static void setupState() {
  setenv("TZ", "<+03>-3", 1);
  tzset();
  mtx = xSemaphoreCreateRecursiveMutex();
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  sprInit();
  themeApply();
  sdOk = true;
  packOpen();  // run.sh geçici SD'ye varlık paketini koyar
  cfg.dbUrl = "https://calisma-asistani-default-rtdb.europe-west1.firebasedatabase.app";
  cfg.apiKey = "AIzaSyB-örnekanahtar1234";
  cfg.email = "ornek@posta.com";
  cfg.password = "gizli";
  view.valid = true;
  view.date = todayStr();
  view.items = sampleItems();
  view.studiedMin = 125;
  view.goalMin = 300;
  view.streak = 12;
  view.examDays = 205;
  weather.valid = true;
  weather.temp = 9.4f;
  weather.code = 2;
  weather.isDay = false;
  bleLinked = true;
  bleHasToken = true;
  netMsg = "Telefon üzerinden (Bluetooth)";
  cloudOk = true;
  bootReason = "Son açılış: yazılım (güncelleme) · önceki 21 sa 4 dk, en az 58 KB";
  Notif n;
  n.id = "1";
  n.app = "WhatsApp";
  n.title = "Ayşe";
  n.text = "Yarın kütüphaneye geliyor musun? Saat 10'da kapıda buluşalım.";
  n.at = g_ms;
  notifs.push_back(n);
  n.id = "2";
  n.app = "Instagram";
  n.title = "yks.motivasyon";
  n.text = "Yeni bir gönderi paylaştı";
  notifs.push_back(n);
  media.on = true;
  media.playing = true;
  media.app = "Spotify";
  media.title = "Yalnızlık Senfonisi";
  media.artist = "Sezen Aksu";
  media.dur = 245000;
  media.pos = 92000;
  media.at = g_ms;
  media.vol = 9;
  wifiKnown.push_back({"EvAgi", "x"});
}

// Lua uygulamasını açar; izin penceresi çıkarsa "İzin ver"e basılmış sayılır
static void luaOpenOk(int i) {
  luaAppOpen(i);
  if (ov.kind == OV_DIALOG && ov.cb) {
    auto cb = ov.cb;
    ov = Overlay();
    cb(1);
  }
}
