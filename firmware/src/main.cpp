// Çalışma Saati — ESP32 Cheap Yellow Display (ESP32-2432S028R) yazılımı
//
// Ekranlar: Ana (saat, tarih, hava durumu, şimdiki/sıradaki oturum), Program (günün
// listesi, tik, basılı tutunca menü), Kronometre (derse bağlı, Pomodoro, telefonla ortak), Ayarlar.
// Veri: Firebase Realtime Database (docs/firebase.md). Değişiklikler canlı akışla (SSE)
// saniyesinde gelir. Ağ işleri ayrı bir görevde (çekirdek 0) çalışır; ekran ve dokunmatik
// ana döngüde (çekirdek 1). SD kart takılıysa gönderilemeyen kayıtlar orada bekler.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <Update.h>
#include <sys/time.h>
#include <time.h>
#include <vector>
#include <memory>
#include <algorithm>

#include "config.h"
#include "fonts.h"

// ============================================================================
// Ortak veri (iki görev arasında, kilitle korunur)
// ============================================================================
struct Item {
  String id, saat, ders, konu, durum;
  int dk = 0;
};

struct View {
  bool valid = false;
  String date;
  std::vector<Item> items;
  int studiedMin = 0, goalMin = 300, streak = 0, examDays = -1;
  long long updatedAt = 0;
};

// Gönderilmiş ama telefonun henüz işlemediği olaylar; ekranda hemen görünsün diye.
struct LocalEvent {
  String type, itemId, date;
  int sec = 0, dk = 0;
  long long at = 0;
};

struct Weather {
  bool valid = false;
  float temp = 0;
  int code = 0;
};

// Telefonla ortak kronometre: users/{uid}/device/timer (telefondaki logic.js sharedTimer ile aynı)
struct SharedTimer {
  bool on = false;
  String itemId, date, ders, konu, by;
  int dk = 0;
  long long startedAt = 0, acc = 0, rev = 0;  // startedAt: duvar saati (ms), duraklatıldıysa 0
};

struct Config {
  String dbUrl, apiKey, email, password;
  bool invert = DEFAULT_INVERT;
  bool calibrated = false, swapXY = false;
  int x1 = 400, x2 = 3480, y1 = 520, y2 = 3520;
  bool pomodoro = false;
  int theme = 0;  // 0 otomatik (gece koyu), 1 açık, 2 koyu
};

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
static volatile bool portalActive = false;
static volatile bool netBusy = false;
static volatile bool wantOta = false;
static volatile bool wantView = false;
static volatile bool credsChanged = false;
static volatile int otaProgress = -1;
static volatile bool wantTimer = false;
static volatile bool liveOn = false;  // canlı akış açık ve veri geliyor

static SharedTimer outTimer, remoteTimer;
static bool outTimerPending = false, remoteTimerDirty = false;
static long long timerRevSeen = 0;

static bool sdOk = false;
static Preferences prefs;

#define STR_(x) #x
#define STR(x) STR_(x)
// SD karttan güncellemede dosyanın hangi sürüm olduğunu anlamak için yazılıma gömülen işaret.
static const char FW_MARKER[] = "CSFW:" FW_VARIANT ":" STR(FW_BUILD) ";";

// ============================================================================
// Yardımcılar
// ============================================================================
static bool timeOk() { return time(nullptr) > 1700000000; }

static bool localNow(struct tm& t) {
  if (!timeOk()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return true;
}

static String todayStr() {
  struct tm t;
  if (!localNow(t)) return "";
  char b[11];
  strftime(b, sizeof b, "%Y-%m-%d", &t);
  return String(b);
}

static long long nowMs() {
  if (!timeOk()) return 0;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static int minutesOfDay() {
  struct tm t;
  if (!localNow(t)) return -1;
  return t.tm_hour * 60 + t.tm_min;
}

static int toMin(const String& hhmm) {
  int c = hhmm.indexOf(':');
  if (c < 0) return 0;
  return hhmm.substring(0, c).toInt() * 60 + hhmm.substring(c + 1).toInt();
}

static String fmtMin(int m) {
  if (m < 0) m = 0;
  if (m < 60) return String(m) + " dk";
  int h = m / 60, r = m % 60;
  return r ? String(h) + " sa " + String(r) + " dk" : String(h) + " sa";
}

static String fmtClock(uint32_t sec) {
  char b[12];
  uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (h) snprintf(b, sizeof b, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  else snprintf(b, sizeof b, "%02u:%02u", (unsigned)m, (unsigned)s);
  return String(b);
}

// Telefondaki uygulamayla aynı ders renkleri (logic.js DERS_RENK)
static uint16_t dersColor(const String& d) {
  if (d == "Türkçe") return RGB(217, 80, 142);
  if (d == "Edebiyat") return RGB(180, 85, 201);
  if (d == "Matematik") return RGB(47, 134, 209);
  if (d == "Geometri") return RGB(75, 104, 214);
  if (d == "Fizik") return RGB(115, 87, 214);
  if (d == "Kimya") return RGB(30, 156, 116);
  if (d == "Biyoloji") return RGB(90, 158, 54);
  if (d == "Tarih") return RGB(217, 115, 43);
  if (d == "Coğrafya") return RGB(201, 148, 21);
  if (d == "Felsefe") return RGB(142, 98, 201);
  if (d == "Din Kültürü") return RGB(168, 133, 54);
  if (d == "İngilizce") return RGB(28, 147, 168);
  return RGB(138, 143, 152);
}

static String normalizeDbUrl(String s) {
  s.trim();
  while (s.endsWith("/")) s.remove(s.length() - 1);
  if (s.length() && !s.startsWith("https://")) {
    if (s.startsWith("http://")) s = "https://" + s.substring(7);
    else s = "https://" + s;
  }
  return s;
}

// ============================================================================
// Kalıcı ayarlar
// ============================================================================
static void loadConfig() {
  Lock l;
  prefs.begin("cs", true);
  cfg.dbUrl = prefs.getString("db", "");
  cfg.apiKey = prefs.getString("key", "");
  cfg.email = prefs.getString("mail", "");
  cfg.password = prefs.getString("pass", "");
  cfg.invert = prefs.getBool("inv", DEFAULT_INVERT);
  cfg.calibrated = prefs.getBool("cal", false);
  cfg.swapXY = prefs.getBool("cswap", false);
  cfg.x1 = prefs.getInt("cx1", cfg.x1);
  cfg.x2 = prefs.getInt("cx2", cfg.x2);
  cfg.y1 = prefs.getInt("cy1", cfg.y1);
  cfg.y2 = prefs.getInt("cy2", cfg.y2);
  cfg.pomodoro = prefs.getBool("pomo", false);
  cfg.theme = constrain(prefs.getInt("theme", 0), 0, 2);
  // Kuyruk: fw-4 metin olarak ("outbox"), fw-5 bayt dizisi olarak ("ob2") saklar.
  String ob = prefs.getString("outbox", "");
  size_t n = prefs.getBytesLength("ob2");
  if (n) {
    std::unique_ptr<char[]> buf(new char[n + 1]);
    prefs.getBytes("ob2", buf.get(), n);
    buf[n] = 0;
    ob += buf.get();
  }
  prefs.end();
  outbox.clear();
  int start = 0;
  while (start < (int)ob.length()) {
    int nl = ob.indexOf('\n', start);
    if (nl < 0) nl = ob.length();
    String line = ob.substring(start, nl);
    line.trim();
    if (line.length()) outbox.push_back(line);
    start = nl + 1;
  }
}

static void saveConfig() {
  Lock l;
  prefs.begin("cs", false);
  prefs.putString("db", cfg.dbUrl);
  prefs.putString("key", cfg.apiKey);
  prefs.putString("mail", cfg.email);
  prefs.putString("pass", cfg.password);
  prefs.putBool("inv", cfg.invert);
  prefs.putBool("cal", cfg.calibrated);
  prefs.putBool("cswap", cfg.swapXY);
  prefs.putInt("cx1", cfg.x1);
  prefs.putInt("cx2", cfg.x2);
  prefs.putInt("cy1", cfg.y1);
  prefs.putInt("cy2", cfg.y2);
  prefs.putBool("pomo", cfg.pomodoro);
  prefs.putInt("theme", cfg.theme);
  prefs.end();
}

// SD kart yokken kuyruk NVS'de durur (en fazla 40 kayıt).
static void saveOutboxNvs() {
  Lock l;
  String s;
  for (auto& e : outbox) {
    s += e;
    s += '\n';
  }
  prefs.begin("cs", false);
  if (s.length()) prefs.putBytes("ob2", s.c_str(), s.length());
  else prefs.remove("ob2");
  prefs.remove("outbox");
  prefs.end();
}

static bool hasCreds() {
  Lock l;
  return cfg.dbUrl.length() && cfg.apiKey.length() && cfg.email.length() && cfg.password.length();
}

static void setNet(const String& msg, bool ok) {
  Lock l;
  if (netMsg != msg || cloudOk != ok) {
    netMsg = msg;
    cloudOk = ok;
    dataDirty = true;
  }
}

// ============================================================================
// SD kart: gönderilecek kayıtlar, çalışma defteri, plan önbelleği
// ============================================================================
// /calisma/kuyruk.txt      buluta henüz gitmemiş olaylar (satır başına bir JSON)
// /calisma/kayit-AAAA-AA.txt  cihazda yapılan her şeyin kalıcı kaydı
// /calisma/plan.json       son alınan plan; açılışta internet beklemeden gösterilir
#define SD_DIR "/calisma"
#define SD_QUEUE SD_DIR "/kuyruk.txt"
#define SD_QUEUE_TMP SD_DIR "/kuyruk.tmp"
#define SD_PLAN SD_DIR "/plan.json"
static const int QUEUE_MAX_SD = 3000, QUEUE_MAX_NVS = 40;
static int sdQueueCount = 0;
static SPIClass sdSPI(VSPI);

static bool sdBegin() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool ok = SD.begin(SD_CS, sdSPI, 10000000, "/sd", 4);
  if (!ok) {
    SD.end();
    ok = SD.begin(SD_CS, sdSPI, 4000000, "/sd", 4);
  }
  if (!ok || SD.cardType() == CARD_NONE) {
    SD.end();
    return false;
  }
  if (!SD.exists(SD_DIR)) SD.mkdir(SD_DIR);
  return true;
}

static int sdCountLines(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  int n = 0;
  uint8_t buf[256];
  size_t r;
  while ((r = f.read(buf, sizeof buf)) > 0)
    for (size_t i = 0; i < r; i++)
      if (buf[i] == '\n') n++;
  f.close();
  return n;
}

// Baştaki n satırı siler.
static void sdDropLines(int n) {
  File in = SD.open(SD_QUEUE, FILE_READ);
  if (!in) {
    sdQueueCount = 0;
    return;
  }
  File out = SD.open(SD_QUEUE_TMP, FILE_WRITE);
  int line = 0, kept = 0;
  while (in.available()) {
    String s = in.readStringUntil('\n');
    s.trim();
    if (!s.length()) continue;
    if (line++ < n) continue;
    if (out) {
      out.print(s);
      out.print('\n');
    }
    kept++;
  }
  in.close();
  if (out) out.close();
  SD.remove(SD_QUEUE);
  if (kept) SD.rename(SD_QUEUE_TMP, SD_QUEUE);
  else SD.remove(SD_QUEUE_TMP);
  sdQueueCount = kept;
}

static void queueAppend(const String& line) {
  Lock l;
  if (sdOk) {
    File f = SD.open(SD_QUEUE, FILE_APPEND);
    if (f && f.print(line + "\n") > 0) {
      f.close();
      if (++sdQueueCount > QUEUE_MAX_SD) sdDropLines(sdQueueCount - QUEUE_MAX_SD);
      return;
    }
    if (f) f.close();
    sdOk = false;  // kart çıkarıldı ya da bozuk: bundan sonra NVS
  }
  outbox.push_back(line);
  while ((int)outbox.size() > QUEUE_MAX_NVS) outbox.erase(outbox.begin());
  saveOutboxNvs();
}

static std::vector<String> queuePeek(int max) {
  Lock l;
  std::vector<String> out;
  if (!sdOk) {
    for (auto& e : outbox) {
      if ((int)out.size() >= max) break;
      out.push_back(e);
    }
    return out;
  }
  if (sdQueueCount <= 0) return out;
  File f = SD.open(SD_QUEUE, FILE_READ);
  while (f && f.available() && (int)out.size() < max) {
    String s = f.readStringUntil('\n');
    s.trim();
    if (s.length()) out.push_back(s);
  }
  if (f) f.close();
  if (out.empty()) sdQueueCount = 0;
  return out;
}

static void queueDrop(int n) {
  Lock l;
  if (sdOk) {
    sdDropLines(n);
    return;
  }
  outbox.erase(outbox.begin(), outbox.begin() + std::min((int)outbox.size(), n));
  saveOutboxNvs();
}

static int queueCount() {
  Lock l;
  return sdOk ? sdQueueCount : (int)outbox.size();
}

// Kart takılıysa NVS'de kalan kayıtlar SD'ye taşınır.
static void queueMigrate() {
  Lock l;
  sdQueueCount = sdCountLines(SD_QUEUE);
  if (outbox.empty()) return;
  File f = SD.open(SD_QUEUE, FILE_APPEND);
  if (!f) return;
  for (auto& e : outbox) {
    f.print(e);
    f.print('\n');
    sdQueueCount++;
  }
  f.close();
  outbox.clear();
  saveOutboxNvs();
}

static void sdLog(const String& line, const String& date) {
  Lock l;
  if (!sdOk || date.length() < 7) return;
  File f = SD.open(String(SD_DIR "/kayit-") + date.substring(0, 7) + ".txt", FILE_APPEND);
  if (!f) return;
  f.print(line);
  f.print('\n');
  f.close();
}

static void cacheView(const String& raw) {
  Lock l;
  if (sdOk) {
    File f = SD.open(SD_PLAN, FILE_WRITE);
    if (f) {
      f.print(raw);
      f.close();
    }
    return;
  }
  if (raw.length() > 3800) return;
  prefs.begin("cs", false);
  prefs.putBytes("view", raw.c_str(), raw.length());
  prefs.end();
}

static String loadCachedView() {
  Lock l;
  String raw;
  if (sdOk) {
    File f = SD.open(SD_PLAN, FILE_READ);
    if (f) {
      raw = f.readString();
      f.close();
    }
    if (raw.length()) return raw;
  }
  prefs.begin("cs", true);
  size_t n = prefs.getBytesLength("view");
  if (n) {
    std::unique_ptr<char[]> buf(new char[n + 1]);
    prefs.getBytes("view", buf.get(), n);
    buf[n] = 0;
    raw = buf.get();
  }
  prefs.end();
  return raw;
}

// ============================================================================
// Ağ (çekirdek 0)
// ============================================================================
static String httpsRequest(const char* method, const String& url, const String& body, int& code,
                           const char* contentType = "application/json") {
  WiFiClientSecure client;
  client.setInsecure();  // sertifika doğrulaması yok; ev ağı için yeterli
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  String resp;
  if (!http.begin(client, url)) {
    code = -1;
    return resp;
  }
  http.setUserAgent("calisma-saati/" + String(FW_BUILD));
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else {
    http.addHeader("Content-Type", contentType);
    code = http.sendRequest(method, body);
  }
  if (code > 0) resp = http.getString();
  http.end();
  return resp;
}

// --- Firebase ---
static String idToken, fbUid;
static uint32_t tokenAt = 0, tokenLife = 0, authRetryAt = 0;
static String lastViewBody;

static String authErrorText(const String& resp) {
  if (resp.indexOf("INVALID_LOGIN_CREDENTIALS") >= 0 || resp.indexOf("INVALID_PASSWORD") >= 0 ||
      resp.indexOf("EMAIL_NOT_FOUND") >= 0)
    return "E-posta ya da şifre yanlış";
  if (resp.indexOf("API key not valid") >= 0 || resp.indexOf("API_KEY_INVALID") >= 0) return "Web API anahtarı geçersiz";
  if (resp.indexOf("OPERATION_NOT_ALLOWED") >= 0) return "Firebase'de e-posta girişi kapalı";
  if (resp.indexOf("TOO_MANY_ATTEMPTS") >= 0) return "Çok fazla deneme, bekleniyor";
  return "Buluta giriş yapılamadı";
}

static bool fbSignIn() {
  String key, email, pass;
  {
    Lock l;
    key = cfg.apiKey;
    email = cfg.email;
    pass = cfg.password;
  }
  JsonDocument req;
  req["email"] = email;
  req["password"] = pass;
  req["returnSecureToken"] = true;
  String body;
  serializeJson(req, body);
  int code = 0;
  String resp = httpsRequest("POST", "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + key, body, code);
  if (code != 200) {
    setNet(code < 0 ? String("Buluta bağlanılamadı") : authErrorText(resp), false);
    return false;
  }
  JsonDocument r;
  if (deserializeJson(r, resp)) {
    setNet("Giriş cevabı okunamadı", false);
    return false;
  }
  idToken = String((const char*)(r["idToken"] | ""));
  fbUid = String((const char*)(r["localId"] | ""));
  int exp = String((const char*)(r["expiresIn"] | "3600")).toInt();
  tokenAt = millis();
  tokenLife = (uint32_t)std::max(300, exp - 300) * 1000UL;
  return idToken.length() > 0 && fbUid.length() > 0;
}

static bool ensureAuth() {
  if (idToken.length() && millis() - tokenAt < tokenLife) return true;
  if ((int32_t)(millis() - authRetryAt) < 0) return false;
  if (fbSignIn()) return true;
  authRetryAt = millis() + 60000;
  return false;
}

static String fbUrl(const char* path) {
  String db;
  {
    Lock l;
    db = cfg.dbUrl;
  }
  return db + "/users/" + fbUid + "/" + path + ".json?auth=" + idToken;
}

static void pruneLocalEvents() {
  Lock l;
  localEvents.erase(std::remove_if(localEvents.begin(), localEvents.end(),
                                   [](const LocalEvent& e) { return e.date != view.date || e.at <= view.updatedAt; }),
                    localEvents.end());
}

// Plan JSON'u (REST, canlı akış ya da SD önbelleği) View'a çevirir.
static void parseView(JsonVariantConst d, View& v) {
  v = View();
  if (!d.is<JsonObjectConst>()) return;
  v.valid = true;
  v.date = String((const char*)(d["date"] | ""));
  v.studiedMin = d["studiedMin"] | 0;
  v.goalMin = d["goalMin"] | 300;
  v.streak = d["streak"] | 0;
  v.examDays = d["examDays"].isNull() ? -1 : d["examDays"].as<int>();
  v.updatedAt = d["updatedAt"].as<long long>();
  for (JsonObjectConst it : d["items"].as<JsonArrayConst>()) {
    Item x;
    x.id = String((const char*)(it["id"] | ""));
    x.saat = String((const char*)(it["saat"] | ""));
    x.ders = String((const char*)(it["ders"] | ""));
    x.konu = String((const char*)(it["konu"] | ""));
    x.durum = String((const char*)(it["durum"] | "bekliyor"));
    x.dk = it["dk"] | 0;
    v.items.push_back(x);
  }
}

static String lastViewRaw;
static void applyView(JsonVariantConst d) {
  String raw;
  serializeJson(d, raw);
  if (raw == lastViewRaw) return;
  View v;
  parseView(d, v);
  {
    Lock l;
    view = v;
  }
  lastViewRaw = raw;
  pruneLocalEvents();
  dataDirty = true;
  if (v.valid) cacheView(raw);
}

static bool fbFetchView() {
  int code = 0;
  String resp = httpsRequest("GET", fbUrl("device/view"), "", code);
  if (code == 401) {
    idToken = "";
    return false;
  }
  if (code != 200) {
    setNet("Plan alınamadı (" + String(code) + ")", false);
    return false;
  }
  if (resp == lastViewBody) return true;
  JsonDocument d;
  if (deserializeJson(d, resp)) {
    setNet("Plan okunamadı", false);
    return false;
  }
  applyView(d.as<JsonVariantConst>());
  lastViewBody = resp;
  return true;
}

static void fbPushStatus() {
  JsonDocument d;
  d["lastSeen"][".sv"] = "timestamp";
  d["fw"] = "fw-" + String(FW_BUILD);
  d["variant"] = FW_VARIANT;
  d["ip"] = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  d["heap"] = ESP.getFreeHeap();
  d["sd"] = sdOk;
  d["live"] = (bool)liveOn;
  d["queue"] = queueCount();
  String body;
  serializeJson(d, body);
  int code = 0;
  httpsRequest("PUT", fbUrl("device/status"), body, code);
  if (code == 401) idToken = "";
}

// Olayın anahtarı içeriğinden üretilir: aynı olay iki kez gönderilse de tek kayıt olur.
static String eventKey(const String& line) {
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < line.length(); i++) {
    h ^= (uint8_t)line[i];
    h *= 1099511628211ULL;
  }
  char b[20];
  snprintf(b, sizeof b, "c%016llx", (unsigned long long)h);
  return String(b);
}

// Bekleyen olaylar 20'şerli paketlerle tek istekte gönderilir (internet uzun süre yoksa
// SD'de yüzlerce kayıt birikebilir).
static void fbFlushOutbox() {
  for (int round = 0; round < 10; round++) {
    std::vector<String> batch = queuePeek(20);
    if (batch.empty()) return;
    String body = "{";
    int valid = 0;
    for (auto& line : batch) {
      JsonDocument t;
      if (deserializeJson(t, line) || !t.is<JsonObject>()) continue;  // yarım yazılmış satır
      if (valid++) body += ',';
      body += '"';
      body += eventKey(line);
      body += "\":";
      body += line;
    }
    body += '}';
    if (valid) {
      int code = 0;
      httpsRequest("PATCH", fbUrl("device/events"), body, code);
      if (code == 401) {
        idToken = "";
        return;
      }
      if (code != 200 && code != 400) return;  // 400: bozuk paket, takılı kalmasın diye atlanır
    }
    queueDrop(batch.size());
    wantView = true;
  }
}

// --- Ortak kronometre ---
static void parseTimer(JsonVariantConst v, SharedTimer& t) {
  t = SharedTimer();
  if (!v.is<JsonObjectConst>()) return;
  t.on = v["on"] | false;
  t.itemId = String((const char*)(v["itemId"] | ""));
  t.date = String((const char*)(v["date"] | ""));
  t.ders = String((const char*)(v["ders"] | ""));
  t.konu = String((const char*)(v["konu"] | ""));
  t.by = String((const char*)(v["by"] | ""));
  t.dk = v["dk"] | 0;
  t.startedAt = v["startedAt"].as<long long>();
  t.acc = v["acc"].as<long long>();
  t.rev = v["rev"].as<long long>();
}

// Buluttaki kronometre bizim gördüğümüzden yeniyse ana döngüye bırakılır.
static void takeRemoteTimer(JsonVariantConst v) {
  SharedTimer r;
  parseTimer(v, r);
  Lock l;
  if (r.rev <= timerRevSeen) return;  // kendi yazdığımız ya da eski
  timerRevSeen = r.rev;
  remoteTimer = r;
  remoteTimerDirty = true;
  outTimerPending = false;
}

static void fbFetchTimer() {
  int code = 0;
  String resp = httpsRequest("GET", fbUrl("device/timer"), "", code);
  if (code == 401) idToken = "";
  if (code != 200) return;
  JsonDocument d;
  if (deserializeJson(d, resp)) return;
  takeRemoteTimer(d.as<JsonVariantConst>());
}

static void fbPushTimer() {
  SharedTimer t;
  {
    Lock l;
    if (!outTimerPending) return;
    t = outTimer;
    outTimerPending = false;
  }
  JsonDocument d;
  d["on"] = t.on;
  if (t.on) {
    d["itemId"] = t.itemId;
    d["date"] = t.date;
    d["ders"] = t.ders;
    d["konu"] = t.konu;
    d["dk"] = t.dk;
    d["startedAt"] = t.startedAt;
    d["acc"] = t.acc;
  }
  d["by"] = "device";
  d["rev"] = t.rev;
  String body;
  serializeJson(d, body);
  int code = 0;
  httpsRequest("PUT", fbUrl("device/timer"), body, code);
  if (code == 401) idToken = "";
  if (code != 200) {
    Lock l;
    if (!outTimerPending && timerRevSeen == t.rev) outTimerPending = true;  // sonra yeniden dene
  }
}

// --- Canlı akış (Realtime Database REST streaming, Server-Sent Events) ---
// users/{uid}/device dinlenir: plan (view) ve kronometre (timer) değişince saniyesinde gelir.
// Akış kopuksa plan 30 sn, kronometre 10 sn arayla sorulur.
struct LiveConn {
  WiFiClientSecure* c = nullptr;
  bool chunked = false, ready = false, overflow = false;
  long chunkLeft = 0;
  int skipCrlf = 0;
  String sizeLine, line, evType, evData;
  uint32_t lastData = 0, retryAt = 0, backoff = 5000;
};
static LiveConn live;

static void liveClose() {
  if (live.c) {
    live.c->stop();
    delete live.c;
    live.c = nullptr;
  }
  live.ready = false;
  live.line = live.evType = live.evData = live.sizeLine = "";
  liveOn = false;
}

static void liveFail() {
  liveClose();
  live.retryAt = millis() + live.backoff;
  live.backoff = std::min<uint32_t>(live.backoff * 2, 120000);
}

static bool readLineT(WiFiClientSecure* c, String& out, uint32_t ms) {
  out = "";
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (c->available()) {
      int ch = c->read();
      if (ch < 0) break;
      if (ch == '\n') {
        out.trim();
        return true;
      }
      if (out.length() < 1500) out += (char)ch;
    }
    if (!c->connected()) return false;
    delay(5);
  }
  return false;
}

static bool liveConnect() {
  if (ESP.getMaxAllocHeap() < 45000) {  // bellek azsa akış açılmaz, sorarak devam edilir
    live.retryAt = millis() + 60000;
    return false;
  }
  String url = fbUrl("device");
  for (int hop = 0; hop < 3; hop++) {
    if (!url.startsWith("https://")) break;
    int slash = url.indexOf('/', 8);
    String host = url.substring(8, slash < 0 ? url.length() : slash);
    String path = slash < 0 ? String("/") : url.substring(slash);
    uint16_t port = 443;
    int colon = host.indexOf(':');
    if (colon >= 0) {
      port = host.substring(colon + 1).toInt();
      host = host.substring(0, colon);
    }
    WiFiClientSecure* c = new WiFiClientSecure();
    c->setInsecure();
    if (!c->connect(host.c_str(), port)) {
      delete c;
      break;
    }
    c->print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host +
             "\r\nAccept: text/event-stream\r\nUser-Agent: calisma-saati/" + String(FW_BUILD) + "\r\n\r\n");
    String st, h, location;
    bool chunked = false;
    int code = 0;
    if (readLineT(c, st, 10000) && st.length() > 12) code = st.substring(9, 12).toInt();
    while (code && readLineT(c, h, 10000) && h.length()) {
      String lower = h;
      lower.toLowerCase();
      if (lower.startsWith("location:")) {
        location = h.substring(9);
        location.trim();
      } else if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") > 0) {
        chunked = true;
      }
    }
    if ((code == 307 || code == 302 || code == 301) && location.length()) {
      c->stop();
      delete c;
      url = location;
      continue;
    }
    if (code != 200) {
      c->stop();
      delete c;
      if (code == 401) idToken = "";
      break;
    }
    live.c = c;
    live.chunked = chunked;
    live.chunkLeft = 0;
    live.skipCrlf = 0;
    live.ready = false;
    live.overflow = false;
    live.line.reserve(256);
    live.lastData = millis();
    return true;
  }
  liveFail();
  return false;
}

static void liveApply(bool patch, const String& path, JsonVariantConst v) {
  if (path == "/") {
    if (patch) {
      for (JsonPairConst kv : v.as<JsonObjectConst>()) liveApply(false, String("/") + kv.key().c_str(), kv.value());
      return;
    }
    applyView(v["view"]);
    takeRemoteTimer(v["timer"]);
    return;
  }
  if (path == "/view") {
    if (patch) wantView = true;
    else applyView(v);
  } else if (path.startsWith("/view/")) {
    wantView = true;
  } else if (path == "/timer") {
    if (patch) wantTimer = true;
    else takeRemoteTimer(v);
  } else if (path.startsWith("/timer/")) {
    wantTimer = true;
  }
  // events ve status bizim yazdıklarımız; yok sayılır.
}

static void liveDispatch() {
  String type = live.evType, data = live.evData;
  bool overflow = live.overflow;
  live.evType = live.evData = "";
  live.overflow = false;
  if (type == "keep-alive" || !type.length()) return;
  if (type == "cancel") {  // izin yok ya da kurallar değişti
    liveClose();
    live.retryAt = millis() + 60000;
    return;
  }
  if (type == "auth_revoked") {  // oturum anahtarının süresi doldu
    idToken = "";
    liveClose();
    live.retryAt = millis() + 1000;
    return;
  }
  if (type != "put" && type != "patch") return;
  if (!live.ready) {
    live.ready = true;
    live.backoff = 5000;
    liveOn = true;
    setNet("Bağlı", true);
  }
  if (overflow) {  // çok büyük mesaj: ayrı ayrı sorulur
    wantView = wantTimer = true;
    return;
  }
  JsonDocument d;
  if (deserializeJson(d, data)) {
    wantView = wantTimer = true;
    return;
  }
  liveApply(type == "patch", String((const char*)(d["path"] | "/")), d["data"].as<JsonVariantConst>());
}

static void liveByte(char ch) {
  if (ch == '\r') return;
  if (ch != '\n') {
    if (live.line.length() < 12000) live.line += ch;
    else live.overflow = true;
    return;
  }
  if (!live.line.length()) {
    liveDispatch();
    return;
  }
  if (live.line.startsWith("event:")) {
    live.evType = live.line.substring(6);
    live.evType.trim();
  } else if (live.line.startsWith("data:")) {
    String part = live.line.substring(5);
    if (part.startsWith(" ")) part.remove(0, 1);
    if (live.evData.length()) live.evData += '\n';
    if (live.evData.length() + part.length() < 12000) live.evData += part;
    else live.overflow = true;
  }
  live.line = "";
}

static void livePump() {
  if (!live.c) return;
  if (!live.c->connected()) {
    liveFail();
    return;
  }
  int budget = 6000;
  while (budget-- > 0 && live.c && live.c->available()) {
    int b = live.c->read();
    if (b < 0) break;
    live.lastData = millis();
    if (!live.chunked) {
      liveByte((char)b);
    } else if (live.chunkLeft > 0) {
      liveByte((char)b);
      if (--live.chunkLeft == 0) live.skipCrlf = 2;
    } else if (live.skipCrlf > 0) {
      live.skipCrlf--;
    } else if (b == '\n') {
      long n = strtol(live.sizeLine.c_str(), nullptr, 16);
      live.sizeLine = "";
      if (n <= 0) {
        liveFail();
        return;
      }
      live.chunkLeft = n;
    } else if (b != '\r' && live.sizeLine.length() < 16) {
      live.sizeLine += (char)b;
    }
  }
  // Firebase 30 sn'de bir keep-alive yollar; uzun sessizlik kopukluk demektir.
  if (live.c && millis() - live.lastData > 75000) liveFail();
}

// --- Hava durumu (Open-Meteo, anahtar gerekmez) ---
static void fetchWeather() {
  int code = 0;
  String resp = httpsRequest("GET",
                             "https://api.open-meteo.com/v1/forecast?latitude=" WEATHER_LAT "&longitude=" WEATHER_LON
                             "&current=temperature_2m,weather_code&timezone=auto",
                             "", code);
  if (code != 200) return;
  JsonDocument d;
  if (deserializeJson(d, resp)) return;
  Lock l;
  weather.valid = !d["current"]["temperature_2m"].isNull();
  weather.temp = d["current"]["temperature_2m"] | 0.0f;
  weather.code = d["current"]["weather_code"] | 0;
  dataDirty = true;
}

// --- Kablosuz güncelleme (GitHub Releases: fw-vN etiketleri) ---
static void setOtaMsg(const String& m) {
  Lock l;
  otaMsg = m;
  dataDirty = true;
}

static void checkOta(bool manual) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  if (!http.begin(client, "https://api.github.com/repos/" GITHUB_REPO "/releases?per_page=30")) return;
  http.setUserAgent("calisma-saati");
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (manual) setOtaMsg("Güncelleme bilgisi alınamadı (" + String(code) + ")");
    return;
  }
  JsonDocument filter;
  filter[0]["tag_name"] = true;
  filter[0]["assets"][0]["name"] = true;
  filter[0]["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    if (manual) setOtaMsg("Güncelleme bilgisi okunamadı");
    return;
  }
  String want = String("firmware-") + FW_VARIANT + ".bin";
  int best = 0;
  String url;
  for (JsonObject rel : doc.as<JsonArray>()) {
    String tag = String((const char*)(rel["tag_name"] | ""));
    if (!tag.startsWith("fw-v")) continue;
    int n = tag.substring(4).toInt();
    if (n <= best) continue;
    for (JsonObject a : rel["assets"].as<JsonArray>()) {
      if (want == String((const char*)(a["name"] | ""))) {
        best = n;
        url = String((const char*)(a["browser_download_url"] | ""));
      }
    }
  }
  if (best <= FW_BUILD || !url.length()) {
    if (manual) setOtaMsg("En güncel sürüm yüklü (fw-" + String(FW_BUILD) + ")");
    return;
  }
  otaProgress = 0;
  dataDirty = true;
  WiFiClientSecure dl;
  dl.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) otaProgress = (int)((long long)cur * 100 / total);
  });
  t_httpUpdate_return r = httpUpdate.update(dl, url);
  if (r == HTTP_UPDATE_OK) {
    otaProgress = 100;
    delay(1500);
    ESP.restart();
  }
  otaProgress = -1;
  setOtaMsg("Güncelleme başarısız: " + httpUpdate.getLastErrorString());
}

static void netTask(void*) {
  uint32_t lastView = 0, lastStatus = 0, nextWeather = 0, lastOta = 0, lastReconnect = 0, lastTimerPoll = 0;
  const uint32_t bootAt = millis();
  bool viewOnce = false, statusOnce = false, otaOnce = false;
  for (;;) {
    netBusy = false;
    vTaskDelay(pdMS_TO_TICKS(live.c ? 80 : 250));
    if (portalActive) {
      liveClose();
      continue;
    }
    netBusy = true;
    if (WiFi.status() != WL_CONNECTED) {
      liveClose();
      setNet("Wi-Fi bağlantısı yok", false);
      if (millis() - lastReconnect > 15000) {
        lastReconnect = millis();
        WiFi.reconnect();
      }
      continue;
    }
    if (credsChanged) {
      credsChanged = false;
      idToken = "";
      authRetryAt = millis();
      viewOnce = statusOnce = false;
      lastViewBody = "";
      liveClose();
      live.retryAt = 0;
    }
    livePump();
    const uint32_t now = millis();
    if ((int32_t)(now - nextWeather) >= 0) {
      fetchWeather();
      bool ok;
      {
        Lock l;
        ok = weather.valid;
      }
      nextWeather = now + (ok ? WEATHER_EVERY_MS : 30000UL);
    }
    if (hasCreds()) {
      if (ensureAuth()) {
        fbPushTimer();
        fbFlushOutbox();
        bool liveOk = live.c && live.ready;
        if (wantView || !viewOnce || now - lastView > (liveOk ? VIEW_POLL_LIVE_MS : VIEW_EVERY_MS)) {
          wantView = false;
          viewOnce = true;
          lastView = now;
          if (fbFetchView()) setNet("Bağlı", true);
        }
        if (wantTimer || (!liveOk && now - lastTimerPoll > TIMER_POLL_MS)) {
          wantTimer = false;
          lastTimerPoll = now;
          fbFetchTimer();
        }
        if (!statusOnce || now - lastStatus > STATUS_EVERY_MS) {
          statusOnce = true;
          lastStatus = now;
          fbPushStatus();
        }
        if (!live.c && idToken.length() && (int32_t)(millis() - live.retryAt) >= 0) liveConnect();
      }
    } else {
      setNet("Hesap bilgisi girilmedi", false);
    }
    bool autoOta = FW_BUILD > 0 && now - bootAt > 20000 && (!otaOnce || now - lastOta > OTA_EVERY_MS);
    if (wantOta || autoOta) {
      bool manual = wantOta;
      wantOta = false;
      otaOnce = true;
      lastOta = now;
      liveClose();  // indirme için bellek boşalsın; akış sonra yeniden açılır
      checkOta(manual);
    }
  }
}

// ============================================================================
// Ekran ve dokunmatik (çekirdek 1)
// ============================================================================
// "Sentez" tasarımı. Sıra: bg, grid, surface, line, lineStrong, text, muted, faint, red, redSoft,
// redLine, ink, onInk, inkMuted, ink2, inkLine, track, ok, cloud
static const Palette PAL_DAY = {
    RGB(245, 244, 240), RGB(232, 231, 226), RGB(255, 255, 255), RGB(226, 224, 217), RGB(207, 205, 198),
    RGB(22, 23, 26),    RGB(110, 111, 119), RGB(154, 155, 162), RGB(207, 68, 40),   RGB(251, 241, 237),
    RGB(229, 152, 137), RGB(22, 23, 26),    RGB(255, 255, 255), RGB(169, 170, 178), RGB(38, 39, 44),
    RGB(52, 53, 59),    RGB(236, 234, 228), RGB(47, 158, 107),  RGB(200, 202, 208)};
static const Palette PAL_NIGHT = {
    RGB(17, 18, 20),    RGB(27, 28, 31),    RGB(27, 28, 32),    RGB(44, 45, 51),    RGB(58, 59, 65),
    RGB(237, 237, 239), RGB(142, 143, 151), RGB(110, 111, 119), RGB(226, 92, 64),   RGB(42, 30, 27),
    RGB(136, 63, 50),   RGB(38, 39, 44),    RGB(255, 255, 255), RGB(160, 161, 169), RGB(52, 53, 59),
    RGB(68, 69, 76),    RGB(44, 45, 51),    RGB(61, 190, 133),  RGB(96, 98, 106)};
const Palette* th = &PAL_DAY;
static bool darkNow = false;

static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static bool sprOk = false;

enum Screen { SCR_HOME = 0, SCR_PLAN = 1, SCR_TIMER = 2, SCR_SETTINGS = 3 };
static Screen screen = SCR_HOME;
static int planScroll = 0;

// Kronometre
static bool swRunning = false;
static uint32_t swStart = 0, swAccMs = 0;
static String swItemId, swDers, swKonu;
static int swDk = 0;
static int pomoCount = 0;
static bool pomoBreak = false;
static uint32_t pomoBreakUntil = 0;
static uint32_t swElapsedMs() { return swAccMs + (swRunning ? millis() - swStart : 0); }

static uint32_t toastUntil = 0;
static uint32_t wakeUntil = 0;
static bool blDim = false;

// Gece ekran kısıkken ilk dokunuş yalnızca ekranı açar.
static bool wakeOnly() {
  bool wasDim = blDim;
  wakeUntil = millis() + 30000;
  if (wasDim) {
    blDim = false;
    ledcWrite(0, 255);
  }
  return wasDim;
}
static volatile bool portalRequested = false, calibRequested = false;
static bool upcomingSoon = false;

// --- Tema ---
static const char* THEME_NAMES[] = {"Otomatik", "Açık", "Koyu"};

static bool isNightHour() {
  struct tm t;
  return localNow(t) && (t.tm_hour >= NIGHT_FROM || t.tm_hour < NIGHT_TO);
}
static bool wantDark() { return cfg.theme == 2 || (cfg.theme == 0 && isNightHour()); }
static void applyPalette() {
  darkNow = wantDark();
  th = darkNow ? &PAL_NIGHT : &PAL_DAY;
}

// --- Kareli zemin ---
// (x, y, w, h) ekran koordinatıdır; (ox, oy) çizilen yüzeyin ekrandaki sol üst köşesi.
template <typename G>
static void gridLines(G& g, int ox, int oy, int x, int y, int w, int h) {
  for (int gx = ((x + GRID_STEP - 1) / GRID_STEP) * GRID_STEP; gx < x + w; gx += GRID_STEP)
    g.drawFastVLine(gx - ox, y - oy, h, COL_GRID);
  for (int gy = ((y + GRID_STEP - 1) / GRID_STEP) * GRID_STEP; gy < y + h; gy += GRID_STEP)
    g.drawFastHLine(x - ox, gy - oy, w, COL_GRID);
}

static void clearBg(int x, int y, int w, int h) {
  tft.fillRect(x, y, w, h, COL_BG);
  gridLines(tft, 0, 0, x, y, w, h);
}

// --- Yazı ---
static const uint8_t* curFont = nullptr;
static void useFont(const uint8_t* f) {
  if (curFont == f) return;
  if (curFont) tft.unloadFont();
  tft.loadFont(f);
  curFont = f;
}

static void text(const String& s, int x, int y, const uint8_t* f, uint16_t fg, uint16_t bg, uint8_t datum = TL_DATUM) {
  useFont(f);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(datum);
  tft.drawString(s, x, y);
}

static int textW(const String& s, const uint8_t* f) {
  useFont(f);
  return tft.textWidth(s);
}

static String utf8Pop(const String& s) {
  int i = (int)s.length() - 1;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
  return s.substring(0, i < 0 ? 0 : i);
}

static String fit(const String& s, const uint8_t* f, int maxW) {
  if (maxW <= 0) return "";
  if (textW(s, f) <= maxW) return s;
  String t = s;
  while (t.length() && textW(t + "…", f) > maxW) t = utf8Pop(t);
  return t + "…";
}

// Büyük rakamlar (saat, kronometre) titremesin diye ara bellekte çizilir.
static const int BIG_H = 56;
static void bigText(const String& s, int x, int y, int w, uint16_t fg, uint16_t bg, bool center, bool grid) {
  if (!sprOk) {
    if (grid) clearBg(x, y, w, BIG_H);
    else tft.fillRect(x, y, w, BIG_H, bg);
    text(s, center ? x + w / 2 : x, y, F_BIG, fg, bg, center ? TC_DATUM : TL_DATUM);
    return;
  }
  spr.fillSprite(bg);
  if (grid) gridLines(spr, x, y, x, y, w, BIG_H);
  spr.setTextColor(fg, bg);
  spr.setTextDatum(center ? TC_DATUM : TL_DATUM);
  spr.drawString(s, center ? w / 2 : 0, 0);
  spr.pushSprite(x, y, 0, 0, w, BIG_H);
}

// --- Şekiller ---
static void card(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border, uint16_t under) {
  tft.fillSmoothRoundRect(x, y, w, h, r, border, under);
  if (fill != border) tft.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, fill, border);
}
static void card(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border) { card(x, y, w, h, r, fill, border, COL_BG); }

static void progressBar(int x, int y, int w, int h, int pct, uint16_t col, uint16_t track, uint16_t bg) {
  pct = constrain(pct, 0, 100);
  tft.fillSmoothRoundRect(x, y, w, h, h / 2, track, bg);
  int fw = w * pct / 100;
  if (fw >= h) tft.fillSmoothRoundRect(x, y, fw, h, h / 2, col, track);
}

static void checkMark(int cx, int cy, uint16_t col, uint16_t bg) {
  tft.drawWideLine(cx - 5, cy, cx - 1, cy + 4, 2.5, col, bg);
  tft.drawWideLine(cx - 1, cy + 4, cx + 6, cy - 4, 2.5, col, bg);
}

static void checkBox(int cx, int cy, bool on, uint16_t bg) {
  if (on) {
    tft.fillSmoothRoundRect(cx - 10, cy - 10, 20, 20, 5, COL_TEXT, bg);
    checkMark(cx, cy, COL_SURFACE, COL_TEXT);
  } else {
    tft.fillSmoothRoundRect(cx - 10, cy - 10, 20, 20, 5, COL_LINE_STRONG, bg);
    tft.fillSmoothRoundRect(cx - 8, cy - 8, 16, 16, 4, bg, COL_LINE_STRONG);
  }
}

static void drawGear(int cx, int cy, uint16_t col, uint16_t bg) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    tft.fillSmoothCircle(cx + (int)(7 * cos(a)), cy + (int)(7 * sin(a)), 2, col, bg);
  }
  tft.fillSmoothCircle(cx, cy, 6, col, bg);
  tft.fillSmoothCircle(cx, cy, 2, bg, col);
}

// Renkli nokta + küçük büyük harfli etiket
static void dotLabel(const String& s, int x, int y, uint16_t dot, uint16_t col, uint16_t bg) {
  tft.fillSmoothCircle(x + 3, y + 9, 3, dot, bg);
  text(s, x + 12, y, F_SMALL, col, bg);
}

static void cloudShape(int cx, int cy, uint16_t col, uint16_t bg) {
  tft.fillSmoothCircle(cx - 5, cy + 1, 6, col, bg);
  tft.fillSmoothCircle(cx + 4, cy - 2, 7, col, bg);
  tft.fillSmoothRoundRect(cx - 12, cy + 1, 24, 8, 4, col, bg);
}

static void sunRing(int cx, int cy, int r, uint16_t bg) {
  tft.fillSmoothCircle(cx, cy, r, COL_RED, bg);
  tft.fillSmoothCircle(cx, cy, r - 3, bg, COL_RED);
}

static void weatherIcon(int cx, int cy, int code, uint16_t bg) {
  if (code == 0) {
    sunRing(cx, cy, 8, bg);
    for (int i = 0; i < 8; i++) {
      float a = i * PI / 4;
      tft.drawWideLine(cx + 12 * cos(a), cy + 12 * sin(a), cx + 15 * cos(a), cy + 15 * sin(a), 2, COL_RED, bg);
    }
  } else if (code <= 2) {
    sunRing(cx - 5, cy - 4, 7, bg);
    cloudShape(cx + 3, cy + 3, COL_CLOUD, bg);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 3; i++) tft.drawWideLine(cx - 11, cy - 5 + i * 6, cx + 11, cy - 5 + i * 6, 2.5, COL_CLOUD, bg);
  } else {
    cloudShape(cx, cy - 3, COL_CLOUD, bg);
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
      for (int i = -1; i <= 1; i++) tft.drawWideLine(cx + i * 6, cy + 9, cx + i * 6 - 2, cy + 14, 2, COL_MUTED, bg);
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
      for (int i = -1; i <= 1; i++) tft.fillSmoothCircle(cx + i * 6, cy + 12, 2, COL_MUTED, bg);
    } else if (code >= 95) {
      tft.drawWideLine(cx + 1, cy + 7, cx - 3, cy + 12, 2.5, COL_RED, bg);
      tft.drawWideLine(cx - 3, cy + 12, cx + 3, cy + 12, 2.5, COL_RED, bg);
      tft.drawWideLine(cx + 3, cy + 12, cx - 1, cy + 17, 2.5, COL_RED, bg);
    }
  }
}

// BTN_SURFACE: kâğıt üstünde beyaz, BTN_PRIMARY: kâğıt üstünde koyu,
// BTN_INK / BTN_INK_PRIMARY: koyu kart üstünde. under: düğmenin altındaki renk.
enum BtnStyle { BTN_SURFACE, BTN_PRIMARY, BTN_INK, BTN_INK_PRIMARY };
static void button(int x, int y, int w, int h, const String& label, BtnStyle st, int32_t under = -1) {
  uint16_t u = under >= 0 ? (uint16_t)under : (st == BTN_SURFACE || st == BTN_PRIMARY ? COL_BG : COL_INK);
  uint16_t fill, border, fg;
  switch (st) {
    case BTN_PRIMARY: fill = border = COL_INK; fg = COL_ON_INK; break;
    case BTN_INK: fill = COL_INK2; border = COL_INK_LINE; fg = COL_ON_INK; break;
    case BTN_INK_PRIMARY: fill = border = COL_ON_INK; fg = INK_TEXT; break;
    default: fill = COL_SURFACE; border = COL_LINE_STRONG; fg = COL_TEXT; break;
  }
  card(x, y, w, h, 10, fill, border, u);
  text(fit(label, F_TEXT, w - 16), x + w / 2, y + h / 2, F_TEXT, fg, fill, MC_DATUM);
}

// --- Veriyi ekrana hazırlama ---
static std::vector<Item> displayItems(int& studied, View& snapshot) {
  Lock l;
  snapshot = view;
  std::vector<Item> items = view.items;
  studied = view.studiedMin;
  for (auto& e : localEvents) {
    if (e.date != view.date || e.at <= view.updatedAt) continue;
    for (auto& it : items) {
      if (it.id != e.itemId) continue;
      if (e.type == "complete" && it.durum != "tamam") {
        it.durum = "tamam";
        studied += it.dk;
      } else if (e.type == "uncomplete" && it.durum == "tamam") {
        it.durum = "bekliyor";
        studied -= it.dk;
      } else if (e.type == "postpone" && it.durum != "tamam") {
        it.durum = "ertelendi";
      } else if (e.type == "session") {
        it.durum = "tamam";
      }
    }
    if (e.type == "session") studied += e.sec / 60;
  }
  return items;
}

static bool planIsToday(const View& v) { return v.valid && v.date.length() && v.date == todayStr(); }

// --- Ortak parçalar ---
static const int TAB_Y = 202;
static const char* TAB_LABELS[] = {"Ana", "Program", "Kronometre"};
static const int TAB_CX[] = {46, 126, 218};

static void drawTabs() {
  tft.fillRect(0, TAB_Y, SCREEN_W, SCREEN_H - TAB_Y, COL_SURFACE);
  tft.drawFastHLine(0, TAB_Y, SCREEN_W, COL_LINE);
  for (int i = 0; i < 3; i++) {
    bool on = (int)screen == i;
    text(TAB_LABELS[i], TAB_CX[i], 219, F_SMALL, on ? COL_TEXT : COL_FAINT, COL_SURFACE, MC_DATUM);
    if (on) tft.fillSmoothCircle(TAB_CX[i], 233, 2, COL_RED, COL_SURFACE);
  }
  bool on = screen == SCR_SETTINGS;
  drawGear(295, 219, on ? COL_TEXT : COL_FAINT, COL_SURFACE);
  if (on) tft.fillSmoothCircle(295, 233, 2, COL_RED, COL_SURFACE);
}

static void toast(const String& msg) {
  card(16, 6, 288, 32, 10, COL_INK, COL_INK);
  text(fit(msg, F_SMALL, 268), 160, 22, F_SMALL, COL_ON_INK, COL_INK, MC_DATUM);
  toastUntil = millis() + 2500;
}

static void fullClear() {
  clearBg(0, 0, SCREEN_W, TAB_Y);
  drawTabs();
}

// --- Ana ekran ---
static const char* GUN_UP[] = {"PAZAR", "PAZARTESİ", "SALI", "ÇARŞAMBA", "PERŞEMBE", "CUMA", "CUMARTESİ"};
static const char* AY_UP[] = {"OCAK", "ŞUBAT", "MART", "NİSAN", "MAYIS", "HAZİRAN", "TEMMUZ", "AĞUSTOS", "EYLÜL", "EKİM", "KASIM", "ARALIK"};

// Tarih satırı 6 saniyede bir sınava kalan gün ve seriyle yer değiştirir.
static bool datePhase = false;
static void drawDate() {
  clearBg(10, 6, 222, 24);
  int exam, streak;
  {
    Lock l;
    exam = view.valid ? view.examDays : -1;
    streak = view.valid ? view.streak : 0;
  }
  String extra;
  if (exam == 0) extra = "SINAV BUGÜN";
  else if (exam > 0) extra = "YKS'YE " + String(exam) + " GÜN";
  if (streak > 0) extra += (extra.length() ? " · " : "") + String(streak) + " GÜN SERİ";
  struct tm t;
  String s;
  if (datePhase && extra.length()) s = extra;
  else s = localNow(t) ? String(GUN_UP[t.tm_wday]) + ", " + String(t.tm_mday) + " " + AY_UP[t.tm_mon] : String("SAAT AYARLANIYOR…");
  bool alt = datePhase && extra.length();
  text(fit(s, F_SMALL, 216), 14, 10, F_SMALL, alt ? COL_RED : COL_MUTED, COL_BG);
}

static void drawClock() {
  struct tm t;
  char b[6] = "--:--";
  if (localNow(t)) strftime(b, sizeof b, "%H:%M", &t);
  bigText(b, 10, 32, 222, COL_TEXT, COL_BG, false, true);
}

static void drawWeatherCard() {
  card(236, 8, 76, 84, 14, COL_SURFACE, COL_LINE);
  Weather w;
  {
    Lock l;
    w = weather;
  }
  if (w.valid) {
    weatherIcon(274, 26, w.code, COL_SURFACE);
    text(String((int)roundf(w.temp)) + "°", 274, 42, F_TITLE, COL_TEXT, COL_SURFACE, TC_DATUM);
  } else {
    text("–", 274, 30, F_TITLE, COL_FAINT, COL_SURFACE, TC_DATUM);
  }
  text(WEATHER_CITY, 274, 72, F_SMALL, COL_MUTED, COL_SURFACE, TC_DATUM);
}

// Ana ekrandaki koyu kronometre kartında yalnızca değişen kısım (saniyede bir).
static void drawNowTimerTick() {
  const int X = 8, Y = 100, W = 304;
  int wMax = textW("0:00:00", F_TITLE);
  tft.fillRect(X + W - 14 - wMax - 2, Y + 24, wMax + 2, 34, COL_INK);
  text(fmtClock(swElapsedMs() / 1000), X + W - 14, Y + 26, F_TITLE, COL_ON_INK, COL_INK, TR_DATUM);
  if (swDk > 0) progressBar(X + 14, Y + 78, W - 28, 4, (int)(swElapsedMs() / 600 / swDk), COL_ON_INK, COL_INK_LINE, COL_INK);
}

static void drawNowCard() {
  const int X = 8, Y = 100, W = 304, H = 94;
  int studied = 0;
  View v;
  std::vector<Item> items = displayItems(studied, v);

  if (swRunning || swElapsedMs() > 0) {
    card(X, Y, W, H, 14, COL_INK, COL_INK);
    dotLabel(swRunning ? "KRONOMETRE" : "DURAKLATILDI", X + 14, Y + 10, swRunning ? COL_RED : COL_INK_MUTED, COL_INK_MUTED, COL_INK);
    int maxW = W - 28 - textW("0:00:00", F_TITLE) - 14;
    text(fit(swDers.length() ? swDers : String("Serbest çalışma"), F_TEXT, maxW), X + 14, Y + 32, F_TEXT, COL_ON_INK, COL_INK);
    text(fit(swKonu, F_SMALL, maxW), X + 14, Y + 54, F_SMALL, COL_INK_MUTED, COL_INK);
    drawNowTimerTick();
    return;
  }

  card(X, Y, W, H, 14, COL_SURFACE, COL_LINE);
  String label, line1, line2;
  uint16_t dot = COL_FAINT;
  if (!hasCreds()) {
    label = "KURULUM";
    line1 = "Telefonla eşleştirilmedi";
    line2 = "Ayarlar → Wi-Fi ve hesap";
  } else if (!planIsToday(v)) {
    bool ok;
    String msg;
    {
      Lock l;
      ok = cloudOk;
      msg = netMsg;
    }
    if (!ok && millis() > 45000) {  // plan yoksa sebebi çoğu zaman bağlantıdır; açıkça söylenir
      label = "BULUT";
      dot = COL_RED;
      line1 = msg;
      line2 = WiFi.status() == WL_CONNECTED ? "Ayarlar ekranında ayrıntı var" : "BOOT'u 6 sn basılı tut: kurulum";
    } else {
      label = "PLAN";
      line1 = v.valid ? "Bugünkü plan henüz gelmedi" : "Bugün için plan yok";
      line2 = v.valid ? "Telefondaki uygulamayı bir kez aç" : "Telefondaki uygulamadan plan oluştur";
    }
  } else {
    int now = minutesOfDay();
    const Item* pick = nullptr;
    bool current = false;
    for (auto& it : items) {
      if (it.durum == "tamam") continue;
      int s = toMin(it.saat);
      if (now >= 0 && s + it.dk <= now) continue;
      pick = &it;
      current = now >= s;
      break;
    }
    if (pick) {
      label = current ? "ŞİMDİ" : "SIRADAKİ";
      dot = current ? COL_RED : COL_MUTED;
      line1 = pick->saat + " · " + pick->ders;
      line2 = pick->konu.length() ? pick->konu : String(pick->dk) + " dk";
    } else {
      label = "BUGÜN";
      dot = COL_OK;
      line1 = items.empty() ? "Plan boş" : "Bugünkü oturumlar bitti";
      if (v.streak > 0) line2 = String(v.streak) + " gün seri";
    }
  }
  dotLabel(label, X + 14, Y + 10, dot, COL_MUTED, COL_SURFACE);
  text(fmtMin(studied) + " / " + fmtMin(v.goalMin), X + W - 14, Y + 10, F_SMALL, COL_MUTED, COL_SURFACE, TR_DATUM);
  text(fit(line1, F_TEXT, W - 28), X + 14, Y + 32, F_TEXT, COL_TEXT, COL_SURFACE);
  text(fit(line2, F_SMALL, W - 28), X + 14, Y + 54, F_SMALL, COL_MUTED, COL_SURFACE);
  progressBar(X + 14, Y + 78, W - 28, 4, v.goalMin > 0 ? studied * 100 / v.goalMin : 0, COL_TEXT, COL_TRACK, COL_SURFACE);
}

static void drawHome(bool full) {
  if (full) fullClear();
  drawDate();
  drawClock();
  drawWeatherCard();
  drawNowCard();
}

// --- Program ekranı ---
static const int ROWS = 4, ROW_Y = 38, ROW_H = 39;
static std::vector<Item> shownItems;

static void scrollButton(int x, bool up, bool enabled) {
  card(x, 6, 30, 26, 9, COL_SURFACE, COL_LINE);
  uint16_t c = enabled ? COL_TEXT : COL_LINE_STRONG;
  int cx = x + 15;
  if (up) tft.fillTriangle(cx, 13, cx - 6, 24, cx + 6, 24, c);
  else tft.fillTriangle(cx, 25, cx - 6, 14, cx + 6, 14, c);
}

static void drawPlan(bool full) {
  if (full) fullClear();
  else clearBg(0, 0, SCREEN_W, TAB_Y);
  int studied = 0;
  View v;
  std::vector<Item> items = displayItems(studied, v);
  if (!planIsToday(v)) items.clear();
  shownItems = items;
  int n = items.size();
  int done = 0;
  for (auto& it : items)
    if (it.durum == "tamam") done++;

  text("Bugünün planı", 12, 4, F_TITLE, COL_TEXT, COL_BG);
  int countRight = 306;
  if (n > ROWS) {
    planScroll = constrain(planScroll, 0, n - ROWS);
    scrollButton(246, true, planScroll > 0);
    scrollButton(282, false, planScroll < n - ROWS);
    countRight = 238;
  } else {
    planScroll = 0;
  }
  if (n) text(String(done) + "/" + String(n), countRight, 13, F_SMALL, COL_MUTED, COL_BG, TR_DATUM);

  if (!n) {
    card(8, 64, 304, 92, 14, COL_SURFACE, COL_LINE);
    text(!hasCreds() ? "Telefonla eşleştirilmedi" : "Bugün için plan yok", 160, 98, F_TEXT, COL_TEXT, COL_SURFACE, MC_DATUM);
    text("Plan telefondaki uygulamadan gelir", 160, 124, F_SMALL, COL_MUTED, COL_SURFACE, MC_DATUM);
    return;
  }

  int vis = std::min(ROWS, n - planScroll);
  card(8, ROW_Y, 304, ROW_H * vis, 12, COL_SURFACE, COL_LINE);
  int now = minutesOfDay();
  for (int i = 0; i < vis; i++) {
    const Item& it = items[planScroll + i];
    int y = ROW_Y + i * ROW_H, cy = y + ROW_H / 2;
    bool isDone = it.durum == "tamam";
    bool active = swItemId.length() && swItemId == it.id;
    bool late = !isDone && now >= 0 && toMin(it.saat) + it.dk < now;
    uint16_t bg = active ? COL_RED_SOFT : COL_SURFACE;
    if (i) tft.drawFastHLine(9, y, 302, COL_LINE);
    if (active) {
      tft.fillSmoothRoundRect(11, y + 3, 298, ROW_H - 6, 8, COL_RED_SOFT, COL_SURFACE);
      tft.fillRect(11, y + 8, 3, ROW_H - 16, COL_RED);
    }
    text(it.saat, 15, cy, F_TEXT, late || it.durum == "ertelendi" ? COL_RED : COL_TEXT, bg, ML_DATUM);
    tft.fillSmoothCircle(86, cy, 4, dersColor(it.ders), bg);
    String ders = fit(it.ders, F_TEXT, 100);
    int dw = textW(ders, F_TEXT);
    text(ders, 96, cy, F_TEXT, isDone ? COL_MUTED : COL_TEXT, bg, ML_DATUM);
    if (isDone) tft.drawFastHLine(96, cy + 1, dw, COL_MUTED);
    String sub = it.durum == "ertelendi" ? String("Ertelendi") : it.konu;
    if (sub.length()) {
      int sx = 96 + dw + 8;
      text(fit(sub, F_SMALL, 266 - sx), sx, cy, F_SMALL, active || it.durum == "ertelendi" ? COL_RED : COL_MUTED, bg, ML_DATUM);
    }
    checkBox(290, cy, isDone, bg);
  }
  tft.fillRect(74, ROW_Y + 1, 2, ROW_H * vis - 2, COL_RED_LINE);  // defter kenar çizgisi
}

// --- Kronometre ekranı ---
static void drawTimerDigits() {
  uint32_t sec = swElapsedMs() / 1000;
  bigText(fmtClock(sec), 14, 64, 292, COL_ON_INK, COL_INK, true, false);
  tft.fillRect(20, 121, 280, 31, COL_INK);
  if (swDk > 0) {
    int pct = (int)(sec * 100 / (uint32_t)(swDk * 60));
    progressBar(22, 125, 276, 4, pct, COL_ON_INK, COL_INK_LINE, COL_INK);
    text("Planlanan " + String(swDk) + " dk", 22, 133, F_SMALL, COL_INK_MUTED, COL_INK);
    text("%" + String(pct), 298, 133, F_SMALL, COL_INK_MUTED, COL_INK, TR_DATUM);
  } else if (pomoBreak) {
    text("Mola zamanı: 5 dakika ara ver", 160, 130, F_SMALL, COL_RED, COL_INK, TC_DATUM);
  }
}

static void drawTimer(bool full) {
  if (full) fullClear();
  card(8, 6, 304, 190, 16, COL_INK, COL_INK);
  bool started = swElapsedMs() > 0;
  dotLabel(swRunning || !started ? "KRONOMETRE" : "DURAKLATILDI", 22, 16, swRunning ? COL_RED : COL_INK_MUTED, COL_INK_MUTED, COL_INK);
  if (cfg.pomodoro) card(210, 11, 92, 26, 9, COL_ON_INK, COL_ON_INK, COL_INK);
  else card(210, 11, 92, 26, 9, COL_INK2, COL_INK_LINE, COL_INK);
  text("Pomodoro", 256, 24, F_SMALL, cfg.pomodoro ? INK_TEXT : COL_INK_MUTED, cfg.pomodoro ? COL_ON_INK : COL_INK2, MC_DATUM);
  String label = "Serbest çalışma";
  if (swDers.length()) {
    label = swDers;
    if (swKonu.length()) label += " · " + swKonu;
  }
  text(fit(label, F_TEXT, 276), 22, 40, F_TEXT, COL_ON_INK, COL_INK);
  drawTimerDigits();
  button(18, 156, 138, 32, swRunning ? "Duraklat" : (started ? "Devam et" : "Başlat"), BTN_INK_PRIMARY);
  button(162, 156, 70, 32, "Bitir", BTN_INK);
  button(238, 156, 64, 32, "Sıfırla", BTN_INK);
}

// --- Ayarlar ekranı ---
static const int SET_Y = 6, SET_RH = 30, SET_N = 5;
static const char* SET_ROWS[] = {"Wi-Fi ve hesap ayarları", "Dokunmatiği yeniden ayarla", "Tema", "Renkleri ters çevir", "Güncellemeyi denetle"};

static void drawSettings(bool full) {
  if (full) fullClear();
  else clearBg(0, 0, SCREEN_W, TAB_Y);
  card(8, SET_Y, 304, SET_RH * SET_N, 12, COL_SURFACE, COL_LINE);
  for (int i = 0; i < SET_N; i++) {
    int y = SET_Y + i * SET_RH;
    if (i) tft.drawFastHLine(20, y, 280, COL_LINE);
    text(SET_ROWS[i], 20, y + SET_RH / 2, F_SMALL, COL_TEXT, COL_SURFACE, ML_DATUM);
    String val;
    if (i == 2) val = THEME_NAMES[constrain(cfg.theme, 0, 2)];
    if (i == 3) val = cfg.invert ? "Açık" : "Kapalı";
    if (val.length()) text(val, 300, y + SET_RH / 2, F_SMALL, COL_MUTED, COL_SURFACE, MR_DATUM);
  }
  String net, ota;
  bool ok;
  {
    Lock l;
    net = netMsg;
    ok = cloudOk;
    ota = otaMsg;
  }
  String l1 = "fw-" + String(FW_BUILD) + " · " FW_VARIANT;
  if (WiFi.status() == WL_CONNECTED) l1 += " · " + WiFi.localIP().toString() + " · " + String(WiFi.RSSI()) + " dBm";
  text(fit(l1, F_SMALL, 296), 12, 162, F_SMALL, COL_MUTED, COL_BG);
  String l2 = ota;
  if (!l2.length()) {
    l2 = "Bulut: " + net + (liveOn ? " (canlı)" : "") + " · SD " + (sdOk ? "var" : "yok");
    int q = queueCount();
    if (q) l2 += " · " + String(q) + " bekliyor";
  }
  text(fit(l2, F_SMALL, 296), 12, 180, F_SMALL, ok || ota.length() ? COL_MUTED : COL_RED, COL_BG);
}

// --- Oturum menüsü (Program ekranında satıra basılı tutunca) ---
enum MenuAct { ACT_START, ACT_POSTPONE, ACT_UNDO, ACT_CLOSE };
static bool menuOpen = false;
static uint32_t menuAt = 0;
static Item menuItem;
static MenuAct menuActs[3];
static int menuN = 0;
static const int MENU_X = 24, MENU_Y = 16, MENU_W = 272, MENU_BTN_Y = 80, MENU_BTN_H = 30, MENU_GAP = 6;

static int menuBtnY(int i) { return MENU_BTN_Y + i * (MENU_BTN_H + MENU_GAP); }

static void drawMenu() {
  int h = (MENU_BTN_Y - MENU_Y) + menuN * (MENU_BTN_H + MENU_GAP) - MENU_GAP + 12;
  card(MENU_X, MENU_Y, MENU_W, h, 14, COL_TEXT, COL_TEXT);  // koyu çerçeve: listenin üstünde ayrı dursun
  card(MENU_X + 2, MENU_Y + 2, MENU_W - 4, h - 4, 12, COL_SURFACE, COL_SURFACE, COL_TEXT);
  tft.fillSmoothCircle(MENU_X + 20, MENU_Y + 25, 4, dersColor(menuItem.ders), COL_SURFACE);
  text(fit(menuItem.saat + " · " + menuItem.ders, F_TEXT, MENU_W - 46), MENU_X + 30, MENU_Y + 14, F_TEXT, COL_TEXT, COL_SURFACE);
  String sub = menuItem.konu.length() ? menuItem.konu : String(menuItem.dk) + " dk";
  if (menuItem.durum == "ertelendi") sub = "Ertelendi · " + sub;
  else if (menuItem.durum == "tamam") sub = "Tamamlandı · " + sub;
  text(fit(sub, F_SMALL, MENU_W - 46), MENU_X + 30, MENU_Y + 40, F_SMALL, COL_MUTED, COL_SURFACE);
  for (int i = 0; i < menuN; i++) {
    const char* l = menuActs[i] == ACT_START ? "Kronometreyle başlat"
                    : menuActs[i] == ACT_POSTPONE ? "Ertele"
                    : menuActs[i] == ACT_UNDO ? "İşareti kaldır"
                                               : "Vazgeç";
    button(MENU_X + 12, menuBtnY(i), MENU_W - 24, MENU_BTN_H, l, i == 0 ? BTN_PRIMARY : BTN_SURFACE, COL_SURFACE);
  }
}

static void drawScreen(bool full) {
  switch (screen) {
    case SCR_HOME: drawHome(full); break;
    case SCR_PLAN: drawPlan(full); break;
    case SCR_TIMER: drawTimer(full); break;
    case SCR_SETTINGS: drawSettings(full); break;
  }
  if (menuOpen && screen == SCR_PLAN) drawMenu();
}

static void go(Screen s) {
  screen = s;
  toastUntil = 0;
  menuOpen = false;
  drawScreen(true);
}

static void drawOta() {
  static int lastShown = -2;
  int p = otaProgress;
  if (p == lastShown) return;
  if (lastShown < 0) clearBg(0, 0, SCREEN_W, SCREEN_H);
  lastShown = p;
  text("Yazılım güncelleniyor", 160, 80, F_TITLE, COL_TEXT, COL_BG, MC_DATUM);
  progressBar(40, 112, 240, 6, p, COL_TEXT, COL_TRACK, COL_BG);
  clearBg(40, 128, 240, 26);
  text("%" + String(p), 160, 140, F_TEXT, COL_MUTED, COL_BG, MC_DATUM);
  text("Kapatma, bitince yeniden başlayacak", 160, 172, F_SMALL, COL_MUTED, COL_BG, MC_DATUM);
}

// ============================================================================
// SD karttan güncelleme (açılışta)
// ============================================================================
// Kartın kökündeki update.bin, firmware.bin ya da firmware-<model>.bin dosyası yazılımın
// içindeki "CSFW:<model>:<sürüm>;" işaretiyle tanınır. Yalnızca aynı modelin daha yeni sürümü
// yüklenir; işlenen dosyanın adı değiştirilir ki her açılışta tekrar denenmesin:
//   .yuklendi  yüklendi        .eski  aynı ya da eski sürüm (işaretsiz eski dosyalar da)
//   .bad       başka model ya da uygulama dosyası değil (ör. ilk-kurulum dosyası)
static bool sdFindMarker(File& f, String& variant, int& build) {
  static const char pat[] = "CSFW:";
  uint8_t buf[512];
  int m = 0;
  bool collecting = false;
  String body;
  size_t r;
  while ((r = f.read(buf, sizeof buf)) > 0) {
    for (size_t i = 0; i < r; i++) {
      char ch = (char)buf[i];
      if (collecting) {
        if (ch == ';') {
          int c = body.lastIndexOf(':');
          if (c > 0 && c < (int)body.length() - 1) {
            variant = body.substring(0, c);
            build = body.substring(c + 1).toInt();
            bool digits = true;
            for (size_t k = c + 1; k < body.length(); k++)
              if (!isdigit((unsigned char)body[k])) digits = false;
            if (digits) return true;
          }
          collecting = false;
        } else if ((isalnum((unsigned char)ch) || ch == '_' || ch == ':') && body.length() < 40) {
          body += ch;
          continue;
        } else {
          collecting = false;
        }
        m = 0;
      }
      if (ch == pat[m]) {
        if (++m == 5) {
          collecting = true;
          body = "";
          m = 0;
        }
      } else {
        m = ch == pat[0] ? 1 : 0;
      }
    }
  }
  return false;
}

static void sdRenameDone(const char* name, const char* suffix) {
  String to = String(name) + suffix;
  if (SD.exists(to)) SD.remove(to);
  if (!SD.rename(String(name), to)) SD.remove(name);
}

static void sdTryUpdate() {
  if (!sdOk) return;
  static const char* names[] = {"/update.bin", "/firmware.bin", "/firmware-" FW_VARIANT ".bin", SD_DIR "/update.bin"};
  for (const char* name : names) {
    if (!SD.exists(name)) continue;
    File f = SD.open(name, FILE_READ);
    if (!f) continue;
    size_t size = f.size();
    int magic = f.read();
    String variant;
    int build = -1;
    bool marked = magic == 0xE9 && size > 100000 && sdFindMarker(f, variant, build);
    f.close();
    if (magic != 0xE9 || (marked && variant != FW_VARIANT)) {
      sdRenameDone(name, ".bad");
      continue;
    }
    if (!marked || build <= FW_BUILD) {
      sdRenameDone(name, ".eski");
      continue;
    }
    f = SD.open(name, FILE_READ);
    if (!f) continue;
    otaProgress = 0;
    drawOta();
    bool ok = Update.begin(size, U_FLASH);
    if (ok) {
      Update.onProgress([](size_t cur, size_t total) {
        if (total) otaProgress = (int)((uint64_t)cur * 100 / total);
        drawOta();
      });
      ok = Update.writeStream(f) == size && Update.end(true);
    }
    f.close();
    if (ok) {
      sdRenameDone(name, ".yuklendi");
      otaProgress = 100;
      drawOta();
      delay(800);
      ESP.restart();
    }
    Update.abort();
    otaProgress = -1;
    sdRenameDone(name, ".bad");
    clearBg(0, 0, SCREEN_W, SCREEN_H);
    text("SD'den güncelleme olmadı", 160, 100, F_TITLE, COL_TEXT, COL_BG, MC_DATUM);
    text("Dosya bozuk olabilir; eski sürümle devam", 160, 134, F_SMALL, COL_MUTED, COL_BG, MC_DATUM);
    delay(3000);
    return;
  }
}

// ============================================================================
// Olaylar
// ============================================================================
static void enqueueEvent(const char* type, const String& itemId, int sec, const String& ders, const String& konu, int dk) {
  String date = todayStr();
  long long at = nowMs();
  JsonDocument d;
  d["type"] = type;
  if (itemId.length()) d["itemId"] = itemId;
  d["date"] = date;
  d["at"] = at;
  if (sec) d["sec"] = sec;
  if (ders.length()) d["ders"] = ders;
  if (konu.length()) d["konu"] = konu;
  String s;
  serializeJson(d, s);
  queueAppend(s);
  sdLog(s, date);
  {
    Lock l;
    LocalEvent e;
    e.type = type;
    e.itemId = itemId;
    e.date = date;
    e.sec = sec;
    e.dk = dk;
    e.at = at;
    localEvents.push_back(e);
    while (localEvents.size() > 60) localEvents.erase(localEvents.begin());
  }
}

// Açılışta kuyrukta bekleyen olaylar ekrana yeniden işlenir (telefon henüz görmedi).
static void rebuildLocalEvents() {
  std::vector<String> q = queuePeek(60);
  Lock l;
  localEvents.clear();
  for (auto& line : q) {
    JsonDocument d;
    if (deserializeJson(d, line)) continue;
    LocalEvent e;
    e.type = String((const char*)(d["type"] | ""));
    e.itemId = String((const char*)(d["itemId"] | ""));
    e.date = String((const char*)(d["date"] | ""));
    e.sec = d["sec"] | 0;
    e.at = d["at"].as<long long>();
    if (e.type.length()) localEvents.push_back(e);
  }
}

// Kronometrenin şimdiki hâli telefona gönderilmek üzere sıraya konur.
static void publishTimer() {
  long long now = nowMs();
  if (!now) return;  // saat ayarlanmadan duvar saati hesaplanamaz
  SharedTimer t;
  t.on = swRunning || swElapsedMs() > 0;
  if (t.on) {
    t.itemId = swItemId;
    t.date = todayStr();
    t.ders = swDers.length() ? swDers : String("Diğer");
    t.konu = swKonu;
    t.dk = swDk;
    if (swRunning) {
      t.startedAt = now - (long long)(millis() - swStart);
      t.acc = swAccMs;
    } else {
      t.startedAt = 0;
      t.acc = swElapsedMs();
    }
  }
  t.by = "device";
  Lock l;
  t.rev = std::max(now, timerRevSeen + 1);
  timerRevSeen = t.rev;
  outTimer = t;
  outTimerPending = true;
  remoteTimerDirty = false;
}

static void resetTimer() {
  swRunning = false;
  swAccMs = 0;
  swItemId = swDers = swKonu = "";
  swDk = 0;
  pomoCount = 0;
  pomoBreak = false;
}

static void finishTimer() {
  uint32_t sec = swElapsedMs() / 1000;
  if (sec < 60) {
    resetTimer();
    drawScreen(true);
    toast("1 dakikadan kısa süreler kaydedilmez");
    return;
  }
  if (!timeOk()) {
    toast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  enqueueEvent("session", swItemId, sec, swDers.length() ? swDers : String("Diğer"), swKonu, swDk);
  resetTimer();
  publishTimer();
  drawScreen(true);
  toast(fmtMin(sec / 60) + " kaydedildi");
}

// Telefondan gelen kronometre (başlatma, duraklatma, bitirme) uygulanır.
static void applyRemoteTimer() {
  SharedTimer r;
  {
    Lock l;
    if (!remoteTimerDirty) return;
    r = remoteTimer;
    remoteTimerDirty = false;
  }
  bool wasActive = swRunning || swElapsedMs() > 0;
  if (!r.on) {
    if (!wasActive) return;
    resetTimer();  // oturum telefonda kaydedildi ya da iptal edildi
    drawScreen(true);
    if (r.by == "phone") toast("Kronometre telefonda kapatıldı");
    return;
  }
  long long now = nowMs();
  bool sameItem = wasActive && swItemId == r.itemId;
  swItemId = r.itemId;
  swDers = r.ders == "Diğer" ? String("") : r.ders;
  swKonu = r.konu;
  swDk = r.dk;
  if (!swDk && swItemId.length()) {
    Lock l;
    for (auto& it : view.items)
      if (it.id == swItemId) swDk = it.dk;
  }
  if (r.startedAt > 0 && now > 0) {
    long long ran = std::max(0LL, now - r.startedAt);
    swAccMs = (uint32_t)std::max(0LL, r.acc);
    swStart = millis() - (uint32_t)ran;  // taşma olsa da fark doğru kalır
    swRunning = true;
  } else {
    swRunning = false;
    swAccMs = (uint32_t)std::max(0LL, r.acc);
  }
  pomoCount = swElapsedMs() / 1000 / POMODORO_WORK_SEC;
  pomoBreak = false;
  menuOpen = false;
  drawScreen(true);
  if (!sameItem && r.by == "phone") toast(swRunning ? "Kronometre telefonda başlatıldı" : "Kronometre telefondan geldi");
}

static void toggleItem(const Item& it) {
  if (!timeOk()) {
    toast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  bool done = it.durum == "tamam";
  if (!done && swItemId == it.id && swElapsedMs() > 0) {
    finishTimer();
    return;
  }
  enqueueEvent(done ? "uncomplete" : "complete", it.id, 0, "", "", it.dk);
  drawScreen(false);
  toast(done ? "Geri alındı" : "Tamamlandı");
}

static void postponeItem(const Item& it) {
  if (!timeOk()) {
    toast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  if (swItemId == it.id) {
    resetTimer();
    publishTimer();
  }
  enqueueEvent("postpone", it.id, 0, "", "", it.dk);
  drawScreen(false);
  toast("Ertelendi");
}

static void selectForTimer(const Item& it) {
  if (swElapsedMs() > 0 && swItemId != it.id) {
    toast("Önce çalışan kronometreyi bitir");
    return;
  }
  swItemId = it.id;
  swDers = it.ders;
  swKonu = it.konu;
  swDk = it.dk;
  go(SCR_TIMER);
}

static void startItemTimer(const Item& it) {
  if (swElapsedMs() > 0 && swItemId != it.id) {
    toast("Önce çalışan kronometreyi bitir");
    return;
  }
  if (swItemId != it.id) {
    swAccMs = 0;
    pomoCount = 0;
    pomoBreak = false;
  }
  swItemId = it.id;
  swDers = it.ders;
  swKonu = it.konu;
  swDk = it.dk;
  if (!swRunning) {
    swStart = millis();
    swRunning = true;
  }
  publishTimer();
  go(SCR_TIMER);
}

static void openMenu(const Item& it) {
  menuItem = it;
  menuN = 0;
  if (it.durum == "tamam") {
    menuActs[menuN++] = ACT_UNDO;
  } else {
    menuActs[menuN++] = ACT_START;
    if (it.durum != "ertelendi") menuActs[menuN++] = ACT_POSTPONE;
  }
  menuActs[menuN++] = ACT_CLOSE;
  menuOpen = true;
  menuAt = millis();
  drawMenu();
}

static void menuTap(int x, int y) {
  menuOpen = false;
  for (int i = 0; i < menuN; i++) {
    int by = menuBtnY(i);
    if (x >= MENU_X + 6 && x < MENU_X + MENU_W - 6 && y >= by - 3 && y < by + MENU_BTN_H + 3) {
      Item it = menuItem;
      switch (menuActs[i]) {
        case ACT_START: startItemTimer(it); return;
        case ACT_POSTPONE: postponeItem(it); return;
        case ACT_UNDO: toggleItem(it); return;
        default: break;
      }
      break;
    }
  }
  drawScreen(false);
}

// ============================================================================
// Dokunmatik ve buton
// ============================================================================
// XPT2046 yazılımsal SPI ile okunur (VSPI SD kartın). Okuma sırası ve ortalama
// XPT2046_Touchscreen kütüphanesiyle aynıdır; eski kalibrasyon geçerli kalır.
struct TouchRaw {
  int x = 0, y = 0, z = 0;
};

static void tsBegin() {
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_CLK, OUTPUT);
  digitalWrite(TOUCH_CLK, LOW);
  pinMode(TOUCH_MOSI, OUTPUT);
  pinMode(TOUCH_MISO, INPUT);
  pinMode(TOUCH_IRQ, INPUT);
}

static uint8_t tsXfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(TOUCH_MOSI, (out >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(TOUCH_CLK, HIGH);
    delayMicroseconds(1);
    in = (in << 1) | (digitalRead(TOUCH_MISO) ? 1 : 0);
    digitalWrite(TOUCH_CLK, LOW);
  }
  return in;
}

static uint16_t tsXfer16(uint16_t out) {
  uint16_t hi = tsXfer(out >> 8);
  return (hi << 8) | tsXfer(out & 0xFF);
}

static int16_t besttwoavg(int16_t x, int16_t y, int16_t z) {
  int16_t da = abs(x - y), db = abs(x - z), dc = abs(z - y);
  if (da <= db && da <= dc) return (x + y) >> 1;
  if (db <= da && db <= dc) return (x + z) >> 1;
  return (y + z) >> 1;
}

static TouchRaw tsRead() {
  static bool awake = false;
  static uint32_t lastAt = 0;
  static TouchRaw last;
  if (millis() - lastAt < 3) return last;
  TouchRaw r;
  // Dokunulmuyorken IRQ yüksektir; bir dokunuş başladıktan sonra parmak kalkana kadar okunur.
  if (!awake && digitalRead(TOUCH_IRQ) == HIGH) {
    last = r;
    return r;
  }
  awake = true;
  int16_t d[6] = {0, 0, 0, 0, 0, 0};
  digitalWrite(TOUCH_CS, LOW);
  tsXfer(0xB1);
  int16_t z1 = tsXfer16(0xC1) >> 3;
  int z = z1 + 4095;
  int16_t z2 = tsXfer16(0x91) >> 3;
  z -= z2;
  if (z >= 400) {
    tsXfer16(0x91);  // ilk ölçüm gürültülü
    d[0] = tsXfer16(0xD1) >> 3;
    d[1] = tsXfer16(0x91) >> 3;
    d[2] = tsXfer16(0xD1) >> 3;
    d[3] = tsXfer16(0x91) >> 3;
  }
  d[4] = tsXfer16(0xD0) >> 3;  // son ölçüm, ardından bekleme kipi (IRQ açık)
  d[5] = tsXfer16(0) >> 3;
  digitalWrite(TOUCH_CS, HIGH);
  lastAt = millis();
  if (z < 400) {
    if (z < 75) awake = false;
    last = r;
    return r;
  }
  r.x = besttwoavg(d[0], d[2], d[4]);
  r.y = besttwoavg(d[1], d[3], d[5]);
  r.z = z;
  last = r;
  return r;
}

static bool tsTouched() { return tsRead().z > 0; }

static bool getTouch(int& x, int& y) {
  TouchRaw p = tsRead();
  if (!p.z) return false;
  int rx = cfg.swapXY ? p.y : p.x, ry = cfg.swapXY ? p.x : p.y;
  int dx = cfg.x2 - cfg.x1, dy = cfg.y2 - cfg.y1;
  if (dx == 0 || dy == 0) return false;
  x = 20 + (long)(rx - cfg.x1) * 280 / dx;
  y = 20 + (long)(ry - cfg.y1) * 200 / dy;
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);
  return true;
}

// Dokunuşu bekler; BOOT'a basılırsa ya da 60 sn dokunulmazsa false döner.
static bool waitTap(int& rx, int& ry) {
  while (tsTouched()) delay(10);
  delay(150);
  uint32_t t0 = millis();
  while (!tsTouched()) {
    if (digitalRead(PIN_BOOT) == LOW || millis() - t0 > 60000) {
      while (digitalRead(PIN_BOOT) == LOW) delay(10);
      return false;
    }
    delay(10);
  }
  long sx = 0, sy = 0;
  int n = 0;
  t0 = millis();
  while (millis() - t0 < 1500) {
    TouchRaw p = tsRead();
    if (!p.z) break;
    sx += p.x;
    sy += p.y;
    n++;
    delay(15);
  }
  while (tsTouched()) delay(10);
  if (!n) return false;
  rx = sx / n;
  ry = sy / n;
  return true;
}

static void runCalibration() {
  const int px[3] = {20, 300, 20}, py[3] = {20, 20, 220};
  int rx[3], ry[3];
  for (int i = 0; i < 3; i++) {
    clearBg(0, 0, SCREEN_W, SCREEN_H);
    text("Dokunmatik ayarı", 160, 92, F_TITLE, COL_TEXT, COL_BG, MC_DATUM);
    text("Artının ortasına dokun (" + String(i + 1) + "/3)", 160, 124, F_SMALL, COL_MUTED, COL_BG, MC_DATUM);
    text("Atlamak için BOOT tuşuna bas", 160, 148, F_SMALL, COL_FAINT, COL_BG, MC_DATUM);
    tft.drawFastHLine(px[i] - 12, py[i], 25, COL_RED);
    tft.drawFastVLine(px[i], py[i] - 12, 25, COL_RED);
    tft.drawCircle(px[i], py[i], 8, COL_RED);
    if (!waitTap(rx[i], ry[i])) {
      Lock l;
      cfg.calibrated = true;  // varsayılan değerlerle devam
      saveConfig();
      return;
    }
  }
  bool swap = abs(ry[1] - ry[0]) > abs(rx[1] - rx[0]);
  int x1 = swap ? ry[0] : rx[0], x2 = swap ? ry[1] : rx[1];
  int y1 = swap ? rx[0] : ry[0], y2 = swap ? rx[2] : ry[2];
  if (abs(x2 - x1) > 300 && abs(y2 - y1) > 300) {
    Lock l;
    cfg.swapXY = swap;
    cfg.x1 = x1;
    cfg.x2 = x2;
    cfg.y1 = y1;
    cfg.y2 = y2;
    cfg.calibrated = true;
  }
  saveConfig();
}

static void onTap(int x, int y) {
  if (menuOpen && y < TAB_Y - 2) {
    menuTap(x, y);
    return;
  }
  if (y >= TAB_Y - 2) {
    if (x < 86) go(SCR_HOME);
    else if (x < 172) go(SCR_PLAN);
    else if (x < 272) go(SCR_TIMER);
    else go(SCR_SETTINGS);
    return;
  }
  switch (screen) {
    case SCR_HOME:
      if (y >= 100) go(swRunning || swElapsedMs() > 0 ? SCR_TIMER : SCR_PLAN);
      break;
    case SCR_PLAN: {
      int n = shownItems.size();
      if (n > ROWS && y < 34) {
        if (x >= 242 && x < 279 && planScroll > 0) {
          planScroll--;
          drawPlan(false);
        } else if (x >= 280 && planScroll < n - ROWS) {
          planScroll++;
          drawPlan(false);
        }
        return;
      }
      for (int i = 0; i < ROWS && planScroll + i < n; i++) {
        int ry = ROW_Y + i * ROW_H;
        if (y >= ry && y < ry + ROW_H) {
          const Item it = shownItems[planScroll + i];
          if (x >= 262) toggleItem(it);
          else selectForTimer(it);
          return;
        }
      }
      break;
    }
    case SCR_TIMER:
      if (y < 42 && x >= 204) {
        cfg.pomodoro = !cfg.pomodoro;
        pomoCount = swElapsedMs() / 1000 / POMODORO_WORK_SEC;
        pomoBreak = false;
        saveConfig();
        drawTimer(false);
      } else if (y >= 150 && y < 194) {
        if (x < 159) {
          if (swRunning) {
            swAccMs = swElapsedMs();
            swRunning = false;
          } else {
            swStart = millis();
            swRunning = true;
          }
          publishTimer();
          drawTimer(false);
        } else if (x < 235) {
          if (swElapsedMs() > 0) finishTimer();
        } else {
          bool was = swRunning || swElapsedMs() > 0;
          resetTimer();
          if (was) publishTimer();
          drawTimer(true);
        }
      }
      break;
    case SCR_SETTINGS:
      if (y >= SET_Y && y < SET_Y + SET_RH * SET_N) {
        int row = (y - SET_Y) / SET_RH;
        if (row == 0) portalRequested = true;
        else if (row == 1) calibRequested = true;
        else if (row == 2) {
          cfg.theme = (cfg.theme + 1) % 3;
          saveConfig();
          applyPalette();
          drawSettings(true);
        } else if (row == 3) {
          cfg.invert = !cfg.invert;
          tft.invertDisplay(cfg.invert);
          saveConfig();
          drawSettings(false);
        } else if (row == 4) {
          setOtaMsg("Güncelleme denetleniyor…");
          wantOta = true;
          drawSettings(false);
        }
      }
      break;
  }
}

// Uzun basış yalnızca Program satırlarında anlamlı: oturum menüsünü açar.
static bool onLongPress(int x, int y) {
  if (screen != SCR_PLAN || menuOpen || y < ROW_Y || y >= TAB_Y - 2) return false;
  int n = shownItems.size();
  for (int i = 0; i < ROWS && planScroll + i < n; i++) {
    int ry = ROW_Y + i * ROW_H;
    if (y >= ry && y < ry + ROW_H) {
      openMenu(shownItems[planScroll + i]);
      return true;
    }
  }
  return false;
}

static void handleTouch() {
  static bool down = false, swallow = false, longFired = false, longUsed = false;
  static int dx = 0, dy = 0;
  static uint32_t downAt = 0, lastSeen = 0, lastUp = 0;
  int x, y;
  bool t = getTouch(x, y);
  uint32_t ms = millis();
  if (t) lastSeen = ms;
  if (t && !down) {
    down = true;
    dx = x;
    dy = y;
    downAt = ms;
    longFired = longUsed = false;
    swallow = wakeOnly();
  } else if (down && t && !swallow && !longFired && ms - downAt > LONG_PRESS_MS) {
    longFired = true;
    longUsed = onLongPress(dx, dy);
  } else if (down && !t && ms - lastSeen > 40) {  // kısa kesintiler bırakma sayılmaz
    down = false;
    if (!swallow && !longUsed && ms - lastUp > 120) onTap(dx, dy);
    lastUp = ms;
  }
}

// BOOT: kısa basış sonraki ekran, 2 sn Ayarlar, 6 sn kurulum (Wi-Fi ve hesap) portalı.
static void handleBoot() {
  static bool down = false;
  static uint32_t downAt = 0;
  static int stage = 0;
  bool pressed = digitalRead(PIN_BOOT) == LOW;
  if (pressed && !down) {
    down = true;
    downAt = millis();
    stage = 0;
  } else if (pressed && down) {
    uint32_t held = millis() - downAt;
    if (stage == 0 && held > 2000) {
      stage = 1;
      go(SCR_SETTINGS);
      toast("Bırakma: 6 sn'de kurulum açılır");
    } else if (stage == 1 && held > 6000) {
      stage = 2;
      portalRequested = true;
    }
  } else if (!pressed && down) {
    down = false;
    if (stage == 0 && millis() - downAt > 40) {
      if (wakeOnly()) return;
      go(screen >= SCR_TIMER ? SCR_HOME : (Screen)((int)screen + 1));
    }
  }
}

// ============================================================================
// Işıklar
// ============================================================================
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(1, 255 - r);  // LED'ler LOW'da yanar
  ledcWrite(2, 255 - g);
  ledcWrite(3, 255 - b);
}

static void updateLeds() {
  uint32_t ms = millis();
  bool blinkFast = (ms / 250) % 2, blinkSlow = (ms / 700) % 2;
  bool ok;
  {
    Lock l;
    ok = cloudOk;
  }
  if (otaProgress >= 0) setLed(0, 0, blinkFast ? 60 : 0);
  else if (pomoBreak) setLed(0, 0, blinkSlow ? 50 : 0);
  else if (swRunning) setLed(0, 35, 0);
  else if (upcomingSoon) setLed(0, 0, blinkSlow ? 40 : 0);
  else if (!ok && hasCreds() && ms > 90000) setLed(25, 0, 0);
  else setLed(0, 0, 0);
}

static void updateBacklight() {
  bool dim = isNightHour() && (int32_t)(millis() - wakeUntil) > 0;
  if (dim != blDim) {
    blDim = dim;
    ledcWrite(0, dim ? 40 : 255);
  }
}

// ============================================================================
// Kurulum portalı (WiFiManager, engellemeyen kip)
// ============================================================================
static void drawPortalScreen() {
  clearBg(0, 0, SCREEN_W, SCREEN_H);
  text("Kurulum", 14, 6, F_TITLE, COL_TEXT, COL_BG);
  const char* lines[] = {
    "1. Telefonda Wi-Fi ayarlarını aç ve",
    "   \"" PORTAL_NAME "\" ağına bağlan.",
    "2. Sayfada \"Configure WiFi\"ye dokun.",
    "3. Ev Wi-Fi'nı seç, şifresini yaz.",
    "4. Altta Firebase bilgilerini doldur, Save.",
    "Sayfa açılmazsa tarayıcıda: 192.168.4.1",
  };
  for (int i = 0; i < 6; i++) text(lines[i], 14, 46 + i * 23, F_SMALL, i == 5 ? COL_TEXT : COL_MUTED, COL_BG);
  button(212, 196, 100, 38, "Vazgeç", BTN_SURFACE);
}

static void waitNetIdle() {
  portalActive = true;
  uint32_t t0 = millis();
  while (netBusy && millis() - t0 < 20000) delay(20);
}

// force: kayıtlı Wi-Fi olsa da portalı açar.
static void runPortal(bool force) {
  waitNetIdle();
  String db, key, mail;
  bool hasPass;
  {
    Lock l;
    db = cfg.dbUrl;
    key = cfg.apiKey;
    mail = cfg.email;
    hasPass = cfg.password.length() > 0;
  }
  WiFiManager wm;
  WiFiManagerParameter title("<hr><h3>Firebase</h3><p>Telefondaki uygulamaya girdi&#287;in bilgilerin ayn&#305;s&#305;.</p>");
  WiFiManagerParameter pDb("db", "Veritaban&#305; adresi", db.c_str(), 140);
  WiFiManagerParameter pKey("key", "Web API anahtar&#305;", key.c_str(), 80);
  WiFiManagerParameter pMail("mail", "E-posta", mail.c_str(), 100);
  // Kayıtlı şifre yoksa "boş bırak" yazmaz; ilk kurulumda boş bırakılıp eşleşme olmuyordu.
  WiFiManagerParameter pPass("pass", hasPass ? "&#350;ifre (de&#287;i&#351;meyecekse bo&#351; b&#305;rak)" : "&#350;ifre",
                             "", 80, "type='password'");
  wm.addParameter(&title);
  wm.addParameter(&pDb);
  wm.addParameter(&pKey);
  wm.addParameter(&pMail);
  wm.addParameter(&pPass);
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(0);
  wm.setConfigPortalBlocking(false);

  bool done = false;
  if (force) {
    wm.startConfigPortal(PORTAL_NAME);
  } else {
    done = wm.autoConnect(PORTAL_NAME);
  }
  if (!done) {
    drawPortalScreen();
    uint32_t t0 = millis();
    const uint32_t limit = (force || !hasCreds()) ? 15UL * 60UL * 1000UL : 5UL * 60UL * 1000UL;
    bool down = false;
    while (millis() - t0 < limit) {
      if (wm.process()) {
        done = true;
        break;
      }
      int x, y;
      bool t = getTouch(x, y);
      if (t && !down && x >= 200 && y >= 190) break;
      down = t;
      if (digitalRead(PIN_BOOT) == LOW) {
        while (digitalRead(PIN_BOOT) == LOW) delay(10);
        break;
      }
      delay(5);
    }
    if (!done) {
      wm.stopConfigPortal();
      WiFi.mode(WIFI_STA);
      WiFi.begin();  // kayıtlı ağ varsa arka planda bağlanmayı dene
    }
  }

  String nDb = normalizeDbUrl(pDb.getValue()), nKey = String(pKey.getValue()), nMail = String(pMail.getValue()),
         nPass = String(pPass.getValue());
  nKey.trim();
  nMail.trim();
  bool changed = false;
  {
    Lock l;
    if (nDb.length() && nDb != cfg.dbUrl) { cfg.dbUrl = nDb; changed = true; }
    if (nKey.length() && nKey != cfg.apiKey) { cfg.apiKey = nKey; changed = true; }
    if (nMail.length() && nMail != cfg.email) { cfg.email = nMail; changed = true; }
    if (nPass.length() && nPass != cfg.password) { cfg.password = nPass; changed = true; }
  }
  if (changed) {
    saveConfig();
    credsChanged = true;
  }
  portalActive = false;
}

// ============================================================================
// Başlangıç ve döngü
// ============================================================================
static void splash(const String& msg) {
  clearBg(0, 0, SCREEN_W, SCREEN_H);
  text("Çalışma Saati", 160, 96, F_TITLE, COL_TEXT, COL_BG, MC_DATUM);
  text(msg, 160, 130, F_SMALL, COL_MUTED, COL_BG, MC_DATUM);
  text("fw-" + String(FW_BUILD), 160, 220, F_SMALL, COL_FAINT, COL_BG, MC_DATUM);
}

void setup() {
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
  applyPalette();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(cfg.invert);
  clearBg(0, 0, SCREEN_W, SCREEN_H);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BL, 0);
  ledcWrite(0, 255);

  tsBegin();

  spr.setColorDepth(16);
  sprOk = spr.createSprite(300, 60) != nullptr;
  if (sprOk) spr.loadFont(F_BIG);

  Serial.println(FW_MARKER);
  splash("SD kart kontrol ediliyor…");
  sdOk = sdBegin();
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
  rebuildLocalEvents();

  // Açılışta 2 sn içinde BOOT'a basılırsa dokunmatik yeniden ayarlanır.
  bool calib = !cfg.calibrated;
  if (!calib) {
    splash("Dokunmatik ayarı için BOOT'a bas");
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

  splash("Wi-Fi'ye bağlanıyor…");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  runPortal(!hasCreds());
  configTzTime("<+03>-3", "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
  drawScreen(true);
}

void loop() {
  static uint32_t lastTick = 0;
  static int lastMinute = -1;
  static uint32_t lastSec = 0;
  static String lastDay;

  if (otaProgress >= 0) {
    drawOta();
    updateLeds();
    delay(50);
    return;
  }

  handleBoot();
  handleTouch();

  if (portalRequested) {
    portalRequested = false;
    while (digitalRead(PIN_BOOT) == LOW) delay(10);  // basılı BOOT portalı hemen kapatmasın
    menuOpen = false;
    runPortal(true);
    drawScreen(true);
  }
  applyRemoteTimer();
  if (menuOpen && millis() - menuAt > 20000) {
    menuOpen = false;
    drawScreen(false);
  }
  if (calibRequested) {
    calibRequested = false;
    runCalibration();
    drawScreen(true);
  }

  if (toastUntil && (int32_t)(millis() - toastUntil) > 0) {
    toastUntil = 0;
    drawScreen(true);
  }
  if (dataDirty && !toastUntil) {
    dataDirty = false;
    drawScreen(false);
  }

  static uint32_t lastPhase = 0;
  if (millis() - lastPhase >= 6000) {
    lastPhase = millis();
    datePhase = !datePhase;
    if (screen == SCR_HOME && !toastUntil) drawDate();
  }

  if (millis() - lastTick >= 200) {
    lastTick = millis();
    struct tm t;
    int minute = localNow(t) ? t.tm_hour * 60 + t.tm_min : -1;
    String day = todayStr();
    if (day != lastDay) {
      lastDay = day;
      dataDirty = true;
    }
    if (minute != lastMinute) {
      lastMinute = minute;
      if (wantDark() != darkNow) {  // otomatik temada 22:00 ve 07:00 geçişleri
        applyPalette();
        toastUntil = 0;
        drawScreen(true);
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
      if (screen == SCR_HOME && !toastUntil) {
        drawClock();
        drawNowCard();
      }
    }
    uint32_t sec = swElapsedMs() / 1000;
    if (sec != lastSec) {
      lastSec = sec;
      if (screen == SCR_TIMER && !toastUntil) drawTimerDigits();
      if (screen == SCR_HOME && swRunning && !toastUntil) drawNowTimerTick();
      if (swRunning && cfg.pomodoro) {
        if (sec >= (uint32_t)(pomoCount + 1) * POMODORO_WORK_SEC) {
          pomoCount++;
          pomoBreak = true;
          pomoBreakUntil = millis() + 5UL * 60UL * 1000UL;
          toast("Mola zamanı: 5 dakika ara ver");
        }
      }
      if (pomoBreak && (int32_t)(millis() - pomoBreakUntil) > 0) pomoBreak = false;
    }
  }

  updateLeds();
  updateBacklight();
  delay(10);
}
