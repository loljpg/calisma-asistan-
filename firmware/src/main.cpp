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
#include <NimBLEDevice.h>
#include <esp_ota_ops.h>
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
  bool coverTheme = true;  // çalan şarkının kapak rengi temaya uygulansın
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
  cfg.coverTheme = prefs.getBool("cover", true);
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
  prefs.putBool("cover", cfg.coverTheme);
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

// Gönderilmiş ama telefonun planına henüz yansımamış olaylar. Kart yeniden başlarsa ekranda
// kaybolmasınlar diye saklanır; telefonun planı bunları gösterince silinir.
static std::vector<String> sentLines;
#define SD_SENT SD_DIR "/gonderilen.txt"

static long long lineAt(const String& line) {
  int i = line.indexOf("\"at\":");
  return i < 0 ? 0 : atoll(line.c_str() + i + 5);
}

static void sentSave() {
  Lock l;
  String s;
  for (auto& e : sentLines) {
    s += e;
    s += '\n';
  }
  if (sdOk) {
    File f = SD.open(SD_SENT, FILE_WRITE);
    if (f) {
      f.print(s);
      f.close();
    }
    return;
  }
  prefs.begin("cs", false);
  if (s.length()) prefs.putBytes("sent", s.c_str(), s.length());
  else prefs.remove("sent");
  prefs.end();
}

static void sentLoad() {
  Lock l;
  String s;
  if (sdOk) {
    File f = SD.open(SD_SENT, FILE_READ);
    if (f) {
      s = f.readString();
      f.close();
    }
  } else {
    prefs.begin("cs", true);
    size_t n = prefs.getBytesLength("sent");
    if (n) {
      std::unique_ptr<char[]> b(new char[n + 1]);
      prefs.getBytes("sent", b.get(), n);
      b[n] = 0;
      s = b.get();
    }
    prefs.end();
  }
  sentLines.clear();
  int start = 0;
  while (start < (int)s.length()) {
    int nl = s.indexOf('\n', start);
    if (nl < 0) nl = s.length();
    String line = s.substring(start, nl);
    line.trim();
    if (line.length()) sentLines.push_back(line);
    start = nl + 1;
  }
}

// Kuyruğun başındaki n olay gönderildi: kuyruktan çıkar, "gönderildi" listesine al.
static void queueDropSent(int n) {
  std::vector<String> head = queuePeek(n);
  {
    Lock l;
    for (auto& h : head) sentLines.push_back(h);
    while (sentLines.size() > 40) sentLines.erase(sentLines.begin());
  }
  queueDrop(n);
  sentSave();
}

static void sentPrune(long long updatedAt, const String& date) {
  bool changed = false;
  {
    Lock l;
    String tag = "\"date\":\"" + date + "\"";
    size_t before = sentLines.size();
    sentLines.erase(std::remove_if(sentLines.begin(), sentLines.end(),
                                   [&](const String& x) { return lineAt(x) <= updatedAt || x.indexOf(tag) < 0; }),
                    sentLines.end());
    changed = sentLines.size() != before;
  }
  if (changed) sentSave();
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
  // Bellek azsa şifreli bağlantı açılmaz: yetersiz bellekte kütüphane çöküp kartı yeniden başlatıyordu.
  if (ESP.getMaxAllocHeap() < 38000) {
    code = -2;
    return String();
  }
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
  if (v.valid) sentPrune(v.updatedAt, v.date);
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
  d["minHeap"] = ESP.getMinFreeHeap();
  d["reset"] = bootReason;
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
    {
      Lock l;
      if (bleLinked || bleInflight) return;  // Bluetooth bağlıyken kuyruğu telefon alır
      fbFlushing = true;
    }
    struct Done {
      ~Done() { fbFlushing = false; }
    } done;
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
      if (code == 200) {
        queueDropSent(batch.size());
        wantView = true;
        continue;
      }
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

// Kronometre her değiştiğinde saklanır; kart yeniden başlarsa kaldığı yerden sürer.
static void timerSave(const SharedTimer& t) {
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
  d["rev"] = t.rev;
  String s;
  serializeJson(d, s);
  Lock l;
  prefs.begin("cs", false);
  prefs.putString("tmr", s);
  prefs.end();
}

static void timerRestore() {
  String s;
  {
    Lock l;
    prefs.begin("cs", true);
    s = prefs.getString("tmr", "");
    prefs.end();
  }
  if (!s.length()) return;
  JsonDocument d;
  if (deserializeJson(d, s)) return;
  SharedTimer r;
  parseTimer(d.as<JsonVariantConst>(), r);
  if (!r.on || !r.rev) return;
  r.by = "restore";
  Lock l;
  timerRevSeen = r.rev;
  remoteTimer = r;
  remoteTimerDirty = true;
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
  live.line = String();  // büyük mesajlardan kalan tamponlar bırakılsın
  live.evType = String();
  live.evData = String();
  live.sizeLine = String();
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
  live.evType = String();
  live.evData = String();
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
  if (live.line.length() > 1024) live.line = String();
  else live.line = "";
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
// Hava durumu şifresiz HTTP ile alınır (Open-Meteo destekliyor): şifreli bağlantının ~40 KB
// belleği Bluetooth açıkken yetmiyordu.
static int weatherHttp = 0;
static uint32_t weatherAt = 0;
static void fetchWeather() {
  static const char* URL = "http://api.open-meteo.com/v1/forecast?latitude=" WEATHER_LAT "&longitude=" WEATHER_LON
                           "&current=temperature_2m,weather_code&timezone=auto";
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  String resp;
  int code = -1;
  if (http.begin(client, URL)) {
    http.setUserAgent("calisma-saati/" + String(FW_BUILD));
    code = http.GET();
    if (code == 200) resp = http.getString();
    http.end();
  }
  if (code != 200) resp = httpsRequest("GET", String(URL).substring(0, 4) + "s" + String(URL).substring(4), "", code);
  weatherHttp = code;
  if (code != 200) return;
  JsonDocument d;
  if (deserializeJson(d, resp)) {
    weatherHttp = -3;
    return;
  }
  Lock l;
  weather.valid = !d["current"]["temperature_2m"].isNull();
  weather.temp = d["current"]["temperature_2m"] | 0.0f;
  weather.code = d["current"]["weather_code"] | 0;
  weatherAt = millis();
  dataDirty = true;
}

// --- Kablosuz güncelleme (GitHub Releases: fw-vN etiketleri) ---
static void setOtaMsg(const String& m) {
  Lock l;
  otaMsg = m;
  dataDirty = true;
}

// Sürüm listesinin tamamı (her sürümle büyüyor, 70 KB'ı geçti) yerine yalnızca fw-v etiketleri
// sorulur (birkaç KB); dosya sürümün sabit adresinden indirilir. false: denetlenemedi.
static bool checkOta(bool manual) {
  int best = 0;
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(15000);
    if (!http.begin(client, "https://api.github.com/repos/" GITHUB_REPO "/git/matching-refs/tags/fw-v")) return false;
    http.setUserAgent("calisma-saati");
    http.addHeader("Accept", "application/vnd.github+json");
    int code = http.GET();
    if (code != 200) {
      http.end();
      if (manual) setOtaMsg("Güncelleme bilgisi alınamadı (" + String(code) + ")");
      return false;
    }
    String body = http.getString();
    http.end();
    if (body.indexOf("refs/tags/") < 0 && body.indexOf("[]") < 0) {
      if (manual) setOtaMsg("Güncelleme bilgisi okunamadı");
      return false;
    }
    const char* p = body.c_str();
    while ((p = strstr(p, "refs/tags/fw-v")) != nullptr) {
      p += 14;
      int n = atoi(p);
      if (n > best) best = n;
    }
  }
  if (best <= FW_BUILD) {
    if (manual) setOtaMsg("En güncel sürüm yüklü (fw-" + String(FW_BUILD) + ")");
    return true;
  }
  String url = "https://github.com/" GITHUB_REPO "/releases/download/fw-v" + String(best) + "/firmware-" FW_VARIANT ".bin";
  esp_ota_mark_app_valid_cancel_rollback();  // doğrulanmamış yazılım yeni güncelleme kuramaz
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
  return false;
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
      if (bleLinked) setNet("Telefon üzerinden (Bluetooth)", true);
      else setNet("Wi-Fi bağlantısı yok", false);
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
    if (bleLinked) {
      // Plan, kronometre ve olaylar Bluetooth'tan gidip gelir; bulut akışı bellek için kapatılır.
      if (live.c) liveClose();
      setNet("Telefon üzerinden (Bluetooth)", true);
    } else if (hasCreds()) {
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
    // İlk denetim açılıştan 60 sn sonra: yeni yazılım önce kendini "sağlam" işaretlesin (45 sn).
    bool autoOta = FW_BUILD > 0 && now - bootAt > 60000 && (!otaOnce || now - lastOta > OTA_EVERY_MS);
    if (wantOta || autoOta) {
      bool manual = wantOta;
      wantOta = false;
      otaOnce = true;
      lastOta = now;
      liveClose();  // indirme için bellek boşalsın; akış sonra yeniden açılır
      // Denetlenemezse 12 saat beklenmez, 15 dakika sonra yeniden denenir.
      if (!checkOta(manual)) lastOta = now - OTA_EVERY_MS + 15UL * 60UL * 1000UL;
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
static Palette palCur = PAL_DAY;  // geçerli palet (kapak rengi uygulanmış olabilir)
const Palette* th = &palCur;
static bool darkNow = false;

// Çalan şarkının kapak renkleri (telefondan gelir): vurgu ve koyu kart
static bool accentOn = false;
static uint8_t accRgb[3] = {207, 68, 40}, darkRgb[3] = {22, 23, 26};

static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static bool sprOk = false;

enum Screen { SCR_HOME = 0, SCR_PLAN = 1, SCR_TIMER = 2, SCR_MUSIC = 3, SCR_GAME = 4, SCR_SETTINGS = 5 };
static const int TAB_N = 6;
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
static uint8_t mixc(uint8_t a, uint8_t b, float t) { return (uint8_t)((float)a + ((float)b - (float)a) * t + 0.5f); }
static uint16_t mixRgb(const uint8_t* c, const uint8_t* w, float t) {
  return RGB(mixc(c[0], w[0], t), mixc(c[1], w[1], t), mixc(c[2], w[2], t));
}

static void applyPalette() {
  darkNow = wantDark();
  palCur = darkNow ? PAL_NIGHT : PAL_DAY;
  if (accentOn && cfg.coverTheme) {
    static const uint8_t WHITE[3] = {255, 255, 255};
    static const uint8_t BG_DAY[3] = {245, 244, 240}, BG_NIGHT[3] = {17, 18, 20};
    static const uint8_t SF_DAY[3] = {255, 255, 255}, SF_NIGHT[3] = {27, 28, 32};
    static const uint8_t INK_NIGHT[3] = {38, 39, 44};
    const uint8_t* bg = darkNow ? BG_NIGHT : BG_DAY;
    const uint8_t* sf = darkNow ? SF_NIGHT : SF_DAY;
    uint8_t a[3] = {accRgb[0], accRgb[1], accRgb[2]};
    float lum = (0.2126f * a[0] + 0.7152f * a[1] + 0.0722f * a[2]) / 255.f;
    if (darkNow && lum < 0.4f)
      for (int i = 0; i < 3; i++) a[i] = mixc(a[i], 255, 0.35f);  // koyu zeminde okunsun
    palCur.red = RGB(a[0], a[1], a[2]);
    palCur.redSoft = mixRgb(a, sf, darkNow ? 0.80f : 0.88f);
    palCur.redLine = mixRgb(a, bg, 0.45f);
    uint8_t d[3];
    for (int i = 0; i < 3; i++) d[i] = darkNow ? mixc(darkRgb[i], INK_NIGHT[i], 0.35f) : darkRgb[i];
    palCur.ink = RGB(d[0], d[1], d[2]);
    palCur.ink2 = mixRgb(d, WHITE, 0.10f);
    palCur.inkLine = mixRgb(d, WHITE, 0.18f);
    palCur.inkMuted = mixRgb(d, WHITE, 0.62f);
  }
  th = &palCur;
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
// Alt çubuk: altı sekme, simgelerle (Ana, Program, Kronometre, Müzik, Oyun, Ayarlar)
static const int TAB_Y = 202;
static int tabCx(int i) { return 27 + i * 160 / 3; }

static void tabIcon(int i, int cx, int cy, uint16_t c, uint16_t bg) {
  switch (i) {
    case 0:  // ev
      tft.fillTriangle(cx - 10, cy - 1, cx, cy - 10, cx + 10, cy - 1, c);
      tft.fillRect(cx - 7, cy - 1, 14, 9, c);
      tft.fillRect(cx - 2, cy + 3, 4, 5, bg);
      break;
    case 1:  // liste
      for (int k = 0; k < 3; k++) {
        int y = cy - 6 + k * 6;
        tft.fillSmoothCircle(cx - 8, y, 2, c, bg);
        tft.fillRect(cx - 4, y - 1, 13, 3, c);
      }
      break;
    case 2:  // kronometre
      tft.fillSmoothCircle(cx, cy + 1, 8, c, bg);
      tft.fillSmoothCircle(cx, cy + 1, 6, bg, c);
      tft.fillRect(cx - 2, cy - 10, 5, 3, c);
      tft.drawWideLine(cx, cy + 1, cx + 3, cy - 3, 2, c, bg);
      break;
    case 3:  // nota
      tft.fillSmoothCircle(cx - 5, cy + 5, 4, c, bg);
      tft.fillSmoothCircle(cx + 6, cy + 3, 4, c, bg);
      tft.fillRect(cx - 2, cy - 7, 2, 12, c);
      tft.fillRect(cx + 9, cy - 9, 2, 12, c);
      tft.fillTriangle(cx - 2, cy - 7, cx + 11, cy - 10, cx + 11, cy - 6, c);
      tft.fillTriangle(cx - 2, cy - 7, cx - 2, cy - 3, cx + 11, cy - 6, c);
      break;
    case 4:  // bloklar
      tft.fillRoundRect(cx - 9, cy - 9, 8, 8, 2, c);
      tft.fillRoundRect(cx + 1, cy - 9, 8, 8, 2, c);
      tft.fillRoundRect(cx - 9, cy + 1, 8, 8, 2, c);
      tft.drawRoundRect(cx + 1, cy + 1, 8, 8, 2, c);
      break;
    default:
      drawGear(cx, cy, c, bg);
      break;
  }
}

static void drawTabs() {
  tft.fillRect(0, TAB_Y, SCREEN_W, SCREEN_H - TAB_Y, COL_SURFACE);
  tft.drawFastHLine(0, TAB_Y, SCREEN_W, COL_LINE);
  for (int i = 0; i < TAB_N; i++) {
    bool on = (int)screen == i;
    int cx = tabCx(i);
    tabIcon(i, cx, 218, on ? COL_TEXT : COL_FAINT, COL_SURFACE);
    if (on) tft.fillSmoothCircle(cx, 234, 2, COL_RED, COL_SURFACE);
  }
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

// --- Telefondan gelen bildirimler ---
struct Notif {
  String id, app, title, text;
  uint32_t icon = 0;
};
static std::vector<Notif> notifs;  // en yeni başta
struct IconImg {
  uint32_t id = 0, used = 0;
  uint16_t px[ICON_PX * ICON_PX];
};
static std::vector<IconImg*> icons;  // en fazla 12 uygulama simgesi
static Notif banner;
static bool notifListOpen = false;

static IconImg* findIcon(uint32_t id) {
  for (auto* ic : icons)
    if (ic->id == id) return ic;
  return nullptr;
}

// Simge yoksa uygulamanın baş harfi yazılır.
static void drawAppIcon(uint32_t id, const String& app, int x, int y, uint16_t bg) {
  IconImg* ic = findIcon(id);
  if (ic) {
    ic->used = millis();
    tft.fillRect(x, y, ICON_PX, ICON_PX, bg);
    tft.setSwapBytes(true);
    tft.pushImage(x, y, ICON_PX, ICON_PX, ic->px, (uint16_t)0xF81F);
    tft.setSwapBytes(false);
    return;
  }
  tft.fillSmoothRoundRect(x, y, ICON_PX, ICON_PX, 5, COL_MUTED, bg);
  String letter = app.length() ? app.substring(0, ((uint8_t)app[0] & 0x80) ? 2 : 1) : String("?");
  letter.toUpperCase();
  text(letter, x + ICON_PX / 2, y + ICON_PX / 2 + 1, F_SMALL, COL_SURFACE, COL_MUTED, MC_DATUM);
}

static void drawBanner() {
  card(8, 6, 304, 66, 14, COL_INK, COL_INK);
  drawAppIcon(banner.icon, banner.app, 20, 18, COL_INK);
  String title = banner.title, body = banner.text;
  if (!title.length()) {
    title = body;
    body = "";
  }
  text(fit(banner.app, F_SMALL, 248), 50, 11, F_SMALL, COL_INK_MUTED, COL_INK);
  text(fit(title, F_TEXT, 250), 50, 27, F_TEXT, COL_ON_INK, COL_INK);
  if (body.length()) text(fit(body, F_SMALL, 250), 50, 50, F_SMALL, COL_INK_MUTED, COL_INK);
}

// Ana ekranın üst satırındaki simgeler: bildirimi olan uygulamalar (en fazla 4).
static int notifIconsW() {
  std::vector<uint32_t> seen;
  for (auto& n : notifs)
    if (std::find(seen.begin(), seen.end(), n.icon) == seen.end()) seen.push_back(n.icon);
  int k = std::min<int>(seen.size(), 4);
  return k ? k * 24 + (seen.size() > 4 ? 22 : 0) : 0;
}

static void drawNotifIcons() {
  std::vector<const Notif*> uniq;
  for (auto& n : notifs) {
    bool dup = false;
    for (auto* u : uniq)
      if (u->icon == n.icon) dup = true;
    if (!dup) uniq.push_back(&n);
  }
  if (uniq.empty()) return;
  int k = std::min<int>(uniq.size(), 4);
  int x = 230 - notifIconsW();
  for (int i = 0; i < k; i++) drawAppIcon(uniq[i]->icon, uniq[i]->app, x + i * 24, 8, COL_BG);
  if ((int)uniq.size() > 4) text("+" + String(uniq.size() - 4), 230, 18, F_SMALL, COL_MUTED, COL_BG, MR_DATUM);
}

// Bildirim listesi (ana ekranda simgelere dokununca)
static void drawNotifList() {
  card(8, 6, 304, 190, 14, COL_TEXT, COL_TEXT);
  card(10, 8, 300, 186, 12, COL_SURFACE, COL_SURFACE, COL_TEXT);
  text("Bildirimler", 22, 14, F_TEXT, COL_TEXT, COL_SURFACE);
  button(216, 12, 84, 28, "Temizle", BTN_SURFACE, COL_SURFACE);
  if (notifs.empty()) {
    text("Bildirim yok", 160, 110, F_SMALL, COL_MUTED, COL_SURFACE, MC_DATUM);
    return;
  }
  for (int i = 0; i < 4 && i < (int)notifs.size(); i++) {
    const Notif& n = notifs[i];
    int y = 46 + i * 37;
    if (i) tft.drawFastHLine(20, y - 2, 280, COL_LINE);
    drawAppIcon(n.icon, n.app, 20, y + 7, COL_SURFACE);
    String t = n.title.length() ? n.title : n.app;
    text(fit(t, F_SMALL, 246), 50, y, F_SMALL, COL_TEXT, COL_SURFACE);
    text(fit(n.text.length() ? n.text : n.app, F_SMALL, 246), 50, y + 17, F_SMALL, COL_MUTED, COL_SURFACE);
  }
}

// --- Telefonda çalan müzik ---
struct MediaState {
  bool on = false, playing = false, enabled = true;
  String app, title, artist;
  uint32_t pos = 0, dur = 0, at = 0;  // pos: at anındaki konum (ms)
  int vol = 0, volMax = 15;
  uint32_t art = 0;
};
static MediaState media;
static uint16_t* artPx = nullptr;
static uint32_t artId = 0;

static uint32_t mediaPos() {
  uint32_t p = media.playing ? media.pos + (millis() - media.at) : media.pos;
  return media.dur ? std::min(p, media.dur) : p;
}

static String fmtMs(uint32_t ms) {
  uint32_t s = ms / 1000;
  char b[12];
  snprintf(b, sizeof b, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  return String(b);
}

static const int MU_CTRL_Y = 126, MU_CTRL_H = 38, MU_VOL_Y = 172, MU_VOL_H = 26;

static void glyphPrev(int cx, int cy, uint16_t c) {
  tft.fillRect(cx - 9, cy - 7, 3, 14, c);
  tft.fillTriangle(cx + 8, cy - 7, cx + 8, cy + 7, cx - 5, cy, c);
}
static void glyphNext(int cx, int cy, uint16_t c) {
  tft.fillRect(cx + 7, cy - 7, 3, 14, c);
  tft.fillTriangle(cx - 8, cy - 7, cx - 8, cy + 7, cx + 5, cy, c);
}
static void glyphPlay(int cx, int cy, uint16_t c, bool playing) {
  if (playing) {
    tft.fillRect(cx - 7, cy - 8, 5, 16, c);
    tft.fillRect(cx + 2, cy - 8, 5, 16, c);
  } else {
    tft.fillTriangle(cx - 5, cy - 9, cx - 5, cy + 9, cx + 9, cy, c);
  }
}

static void drawMusicProgress() {
  tft.fillRect(8, 92, 304, 32, COL_BG);
  gridLines(tft, 0, 0, 8, 92, 304, 32);
  int pct = media.dur ? (int)((uint64_t)mediaPos() * 100 / media.dur) : 0;
  progressBar(12, 96, 296, 4, pct, COL_RED, COL_TRACK, COL_BG);
  text(fmtMs(mediaPos()), 12, 104, F_SMALL, COL_MUTED, COL_BG);
  if (media.dur) text(fmtMs(media.dur), 308, 104, F_SMALL, COL_MUTED, COL_BG, TR_DATUM);
}

static void drawMusicControls() {
  button(52, MU_CTRL_Y, 60, MU_CTRL_H, "", BTN_SURFACE);
  glyphPrev(82, MU_CTRL_Y + MU_CTRL_H / 2, COL_TEXT);
  button(120, MU_CTRL_Y, 80, MU_CTRL_H, "", BTN_PRIMARY);
  glyphPlay(160, MU_CTRL_Y + MU_CTRL_H / 2, COL_ON_INK, media.playing);
  button(208, MU_CTRL_Y, 60, MU_CTRL_H, "", BTN_SURFACE);
  glyphNext(238, MU_CTRL_Y + MU_CTRL_H / 2, COL_TEXT);
}

static void drawMusicVolume() {
  button(12, MU_VOL_Y, 44, MU_VOL_H, "", BTN_SURFACE);
  tft.fillRect(27, MU_VOL_Y + MU_VOL_H / 2 - 1, 14, 3, COL_TEXT);
  button(264, MU_VOL_Y, 44, MU_VOL_H, "", BTN_SURFACE);
  tft.fillRect(279, MU_VOL_Y + MU_VOL_H / 2 - 1, 14, 3, COL_TEXT);
  tft.fillRect(285, MU_VOL_Y + MU_VOL_H / 2 - 7, 3, 15, COL_TEXT);
  tft.fillRect(62, MU_VOL_Y, 196, MU_VOL_H, COL_BG);
  int pct = media.volMax > 0 ? media.vol * 100 / media.volMax : 0;
  progressBar(66, MU_VOL_Y + MU_VOL_H / 2 - 2, 188, 5, pct, COL_TEXT, COL_TRACK, COL_BG);
}

static void drawCover() {
  const int X = 12, Y = 8;
  if (artPx && artId && artId == media.art) {
    tft.setSwapBytes(true);
    tft.pushImage(X, Y, ART_PX, ART_PX, artPx);
    tft.setSwapBytes(false);
    return;
  }
  card(X, Y, ART_PX, ART_PX, 10, COL_SURFACE, COL_LINE);
  tabIcon(3, X + ART_PX / 2 - 1, Y + ART_PX / 2, COL_LINE_STRONG, COL_SURFACE);
}

static void drawMusic(bool full) {
  if (full) fullClear();
  else clearBg(0, 0, SCREEN_W, TAB_Y);
  if (!bleLinked || !media.on) {
    card(8, 40, 304, 110, 14, COL_SURFACE, COL_LINE);
    String l1, l2;
    if (!bleLinked) {
      l1 = "Telefon Bluetooth ile bağlı değil";
      l2 = "Uygulama → Ayarlar → Masa saatini bul";
    } else if (!media.enabled) {
      l1 = "Müzik kumandası kapalı";
      l2 = "Uygulamanın ayarlarından açabilirsin";
    } else {
      l1 = "Telefonda müzik çalmıyor";
      l2 = "Spotify ya da YouTube Music'te bir şarkı aç";
    }
    tabIcon(3, 160, 72, COL_FAINT, COL_SURFACE);
    text(l1, 160, 100, F_TEXT, COL_TEXT, COL_SURFACE, MC_DATUM);
    text(fit(l2, F_SMALL, 284), 160, 126, F_SMALL, COL_MUTED, COL_SURFACE, MC_DATUM);
    return;
  }
  drawCover();
  text(fit(media.title.length() ? media.title : String("Bilinmeyen şarkı"), F_TEXT, 204), 104, 12, F_TEXT, COL_TEXT, COL_BG);
  text(fit(media.artist, F_SMALL, 204), 104, 38, F_SMALL, COL_MUTED, COL_BG);
  String app = media.app + (media.playing ? "" : " · duraklatıldı");
  text(fit(app, F_SMALL, 204), 104, 58, F_SMALL, COL_FAINT, COL_BG);
  drawMusicProgress();
  drawMusicControls();
  drawMusicVolume();
}

// Kedi kareleri (tools ile üretildi). O: çizgi, W: beyaz, G: gölge, K: koyu gölge, E: göz, P: pembe, .: boş
static const int CAT_W = 19, CAT_H = 13;
enum CatFrame { CF_WALK1, CF_WALK2, CF_RUN, CF_BAT, CF_SIT, CF_SLEEP };
static const char* const CAT_FRAMES[][CAT_H] = {
    {"..OO........O...O..",
     ".OWWO......OWO.OWO.",
     "OWWWWO....OWKOOPWO.",
     "OWWWO.....OWWWWWWO.",
     ".OWWO.....OWWWWEWWO",
     "..OWWO....OWWWWWWPO",
     "..OWWWOOOOWWWWWWWO.",
     "...OWWWWWWWWWWWWO..",
     "...OGWWWWWWWWWWGO..",
     "...OGGWWWWWWWWGGO..",
     "...OWKOKWOOOWKOWO..",
     "...OWKOKWO.OWKOWO..",
     "....OO.OO...OO.O..."},  // walk1
    {"..OO........O...O..",
     ".OWWO......OWO.OWO.",
     "OWWWWO....OWKOOPWO.",
     "OWWWO.....OWWWWWWO.",
     ".OWWO.....OWWWWEWWO",
     "..OWWO....OWWWWWWPO",
     "..OWWWOOOOWWWWWWWO.",
     "...OWWWWWWWWWWWWO..",
     "...OGWWWWWWWWWWGO..",
     "...OGGWWWWWWWWGGO..",
     "....OWKWOOOOOWKWO..",
     "....OWKWO...OWKWO..",
     ".....OOO.....OOO..."},  // walk2
    {"..OO........O...O..",
     ".OWWO......OWO.OWO.",
     "OWWWWO....OWKOOPWO.",
     "OWWWO.....OWWWWWWO.",
     ".OWWO....OWWWWWEWWO",
     "..OWWO...OWWWWWWWPO",
     "..OWWWOOOWWWWWWWWO.",
     "...OWWWWWWWWWWWWO..",
     "...OGWWWWWWWWWWGO..",
     "..OGGGWWWWWWWGGWWO.",
     ".OWWOGGGOOOOGGOOWWO",
     "OWWO.OOO....OO..OWO",
     ".OO..............O."},  // run
    {"..OO........O...O..",
     ".OWWO......OWO.OWO.",
     "OWWWWO....OWKOOPWO.",
     "OWWWO.....OWWWWWWO.",
     ".OWWO.....OWWWWEWWO",
     "..OWWO....OWWWWWWPO",
     "..OWWWOOOOWWWWWWWO.",
     "...OWWWWWWWWWWWWWWO",
     "...OGWWWWWWWWWWGOWO",
     "...OGGWWWWWWWWGGOO.",
     "...OWKOKWOOOWKOO...",
     "...OWKOKWO.OWKO....",
     "....OO.OO...OO....."},  // bat
    {".........O...O.....",
     "........OWO.OWO....",
     "........OWKOOPWO...",
     "........OWWWWWWO...",
     "..OO....OWWWWEWWO..",
     ".OWWO...OWWWWWWPO..",
     "OWWWWO.OWWWWWWWO...",
     "OWWWO.OWWWWWWWWO...",
     "OWWO.OWWWWWWWWWO...",
     ".OWWOOGWWWWWWWWO...",
     "..OWWGGWWWWWWGWO...",
     "...OGGGGWWWWGGWO...",
     "....OOOOOOOOOOO...."},  // sit
    {"...................",
     "...................",
     "...................",
     "...................",
     "............O...O..",
     ".......OOOOOWO.OWO.",
     "....OOOWWWWWWKOPWO.",
     "...OWWWWWWWWWWWWWO.",
     "..OWWWWWWWWWWKKWWWO",
     ".OWWGWWWWWWWWWWWWPO",
     ".OWWGGWWWWWWWWWWWO.",
     "..OWWWGGGGGGGGGWO..",
     "...OOOOOOOOOOOOO..."},  // sleep
};

// --- Ana ekrandaki kedi ve topu ---
// Saatin altındaki şeritte kendi kendine dolaşır, oturur, topunu kovalar ve patiyle iter.
// Gece (22:00–07:00) uyur. Üstüne dokununca sevinir.
static const int CAT_S = 2;                         // her sanat pikseli 2×2 ekran pikseli
static const int ZONE_TOP = 92, ZONE_FLOOR = 119;   // şerit: 92..121, ayakların bastığı çizgi 119
static const int ZONE_H = 30;
enum CatState { CAT_SIT, CAT_WALK, CAT_CHASE, CAT_BAT, CAT_SLEEP, CAT_HAPPY };
struct CatS {
  float x = 120, target = 120, bx = 220, bvx = 0;
  int dir = 1, hop = 0;
  CatState st = CAT_SIT;
  uint32_t until = 0, tick = 0, happyAt = 0;
  int lastL = -1, lastR = -1, lastBL = -1, lastBR = -1, lastFrame = -1, lastHop = 0;
  bool lastZ = false;
};
static CatS cat;

static uint16_t catColor(char c) {
  switch (c) {
    case 'O': return darkNow ? RGB(150, 151, 160) : RGB(112, 113, 121);
    case 'W': return RGB(250, 250, 248);
    case 'G': return RGB(205, 205, 212);
    case 'K': return RGB(172, 172, 182);
    case 'E': return RGB(236, 150, 30);
    case 'P': return RGB(238, 140, 170);
    default: return 0;
  }
}

static int catFrameNow() {
  switch (cat.st) {
    case CAT_WALK: return (cat.tick / 4) % 2 ? CF_WALK2 : CF_WALK1;
    case CAT_CHASE: return (cat.tick / 3) % 2 ? CF_RUN : CF_WALK2;
    case CAT_BAT: return CF_BAT;
    case CAT_SLEEP: return CF_SLEEP;
    default: return CF_SIT;
  }
}

static int catLeft() { return (int)cat.x - CAT_W * CAT_S / 2; }

// Küçük piksel desenleri (uyku "z"si ve kalp)
static void catPattern(int x, int y, const char* const* rows, int n, uint16_t col) {
  for (int r = 0; r < n; r++)
    for (int c = 0; rows[r][c]; c++)
      if (rows[r][c] == 'X') spr.fillRect(x + c * 2, y + r * 2, 2, 2, col);
}
static const char* const PAT_Z[] = {"XXX", "..X", ".X.", "XXX"};
static const char* const PAT_HEART[] = {".X.X.", "XXXXX", ".XXX.", "..X.."};

// Şeridin [x0, x0+w) kısmını ara bellekte çizip ekrana basar.
static void catRender(int x0, int w) {
  int x1 = std::min(SCREEN_W, x0 + w);
  x0 = std::max(0, x0);
  if (!sprOk || x1 <= x0) return;
  while (x0 < x1) {
    int ww = std::min(x1 - x0, 300);
    spr.fillRect(0, 0, ww, ZONE_H, COL_BG);
    gridLines(spr, x0, ZONE_TOP, x0, ZONE_TOP, ww, ZONE_H);
    int cx = (int)cat.x;
    uint16_t shadow = darkNow ? RGB(6, 6, 8) : COL_LINE_STRONG;
    if (cat.st != CAT_SLEEP) spr.fillRoundRect(cx - 13 - x0, ZONE_FLOOR - ZONE_TOP, 26, 3, 1, shadow);
    else spr.fillRoundRect(cx - 16 - x0, ZONE_FLOOR - ZONE_TOP, 32, 3, 1, shadow);
    // top
    int bx = (int)cat.bx, by = ZONE_FLOOR - 4 - ZONE_TOP;
    spr.fillRoundRect(bx - 4 - x0, ZONE_FLOOR - ZONE_TOP + 1, 9, 2, 1, shadow);
    spr.fillCircle(bx - x0, by, 4, COL_RED);
    float a = cat.bx / 4.0f;
    spr.fillRect(bx - x0 + (int)roundf(cosf(a) * 2) - 1, by + (int)roundf(sinf(a) * 2) - 1, 2, 2, COL_SURFACE);
    // kedi
    int f = catFrameNow();
    int left = catLeft(), top = ZONE_FLOOR + 1 - CAT_H * CAT_S - cat.hop;
    for (int r = 0; r < CAT_H; r++) {
      const char* row = CAT_FRAMES[f][r];
      for (int c = 0; c < CAT_W; c++) {
        char ch = row[cat.dir > 0 ? c : CAT_W - 1 - c];
        if (ch == '.') continue;
        spr.fillRect(left + c * CAT_S - x0, top + r * CAT_S - ZONE_TOP, CAT_S, CAT_S, catColor(ch));
      }
    }
    if (cat.st == CAT_SLEEP) {
      int zx = cat.dir > 0 ? left + 22 : left + 8;
      catPattern(zx - x0, top + 2 - ZONE_TOP, PAT_Z, 4, COL_MUTED);
      if ((cat.tick / 3) % 2) catPattern(zx + 9 * cat.dir - x0, top - ZONE_TOP - 1, PAT_Z, 4, COL_FAINT);
    } else if (cat.st == CAT_HAPPY) {
      int hx = cat.dir > 0 ? left + CAT_W * CAT_S + 1 : left - 11;
      catPattern(hx - x0, top + 2 - ZONE_TOP, PAT_HEART, 4, COL_RED);
    }
    spr.pushSprite(x0, ZONE_TOP, 0, 0, ww, ZONE_H);
    x0 += ww;
  }
}

static void catDrawAll() {
  cat.lastL = cat.lastR = cat.lastBL = cat.lastBR = -1;
  catRender(0, SCREEN_W);
}

// Yalnızca değişen yer yeniden çizilir (eski ve yeni konumları kapsayan dar şerit).
static void catFlush() {
  int f = catFrameNow();
  int l = catLeft() - 14, r = catLeft() + CAT_W * CAT_S + 14;
  int bl = (int)cat.bx - 7, br = (int)cat.bx + 7;
  bool z = (cat.tick / 3) % 2;
  bool same = l == cat.lastL && bl == cat.lastBL && f == cat.lastFrame && cat.hop == cat.lastHop &&
              (cat.st != CAT_SLEEP || z == cat.lastZ);
  if (same) return;
  int a0 = cat.lastL < 0 ? l : std::min(l, cat.lastL), a1 = std::max(r, cat.lastR);
  int b0 = cat.lastBL < 0 ? bl : std::min(bl, cat.lastBL), b1 = std::max(br, cat.lastBR);
  if (b0 <= a1 && b1 >= a0) {
    catRender(std::min(a0, b0), std::max(a1, b1) - std::min(a0, b0));
  } else {
    catRender(a0, a1 - a0);
    catRender(b0, b1 - b0);
  }
  cat.lastL = l;
  cat.lastR = r;
  cat.lastBL = bl;
  cat.lastBR = br;
  cat.lastFrame = f;
  cat.lastHop = cat.hop;
  cat.lastZ = z;
}

static uint32_t rnd(uint32_t n) { return esp_random() % n; }

static void catUpdate(bool night) {
  uint32_t ms = millis();
  cat.tick++;
  // Top yuvarlanır, yavaşlar, kenarlardan seker.
  if (fabsf(cat.bvx) > 0.03f) {
    cat.bx += cat.bvx;
    cat.bvx *= 0.965f;
    if (cat.bx < 8) {
      cat.bx = 8;
      cat.bvx = -cat.bvx * 0.6f;
    } else if (cat.bx > SCREEN_W - 8) {
      cat.bx = SCREEN_W - 8;
      cat.bvx = -cat.bvx * 0.6f;
    }
  } else {
    cat.bvx = 0;
  }
  switch (cat.st) {
    case CAT_SLEEP:
      if (!night) {
        cat.st = CAT_SIT;
        cat.until = ms + 3000;
      }
      break;
    case CAT_SIT:
      if (fabsf(cat.bvx) > 0.3f) cat.dir = cat.bx > cat.x ? 1 : -1;  // yuvarlanan topu izler
      if ((int32_t)(ms - cat.until) > 0) {
        if (night) {
          cat.st = CAT_SLEEP;
          break;
        }
        uint32_t r = rnd(100);
        if (r < 45) {
          cat.st = CAT_CHASE;
        } else if (r < 85) {
          cat.st = CAT_WALK;
          cat.target = 24 + rnd(272);
          cat.dir = cat.target > cat.x ? 1 : -1;
        } else {
          cat.until = ms + 2000 + rnd(4000);
        }
      }
      break;
    case CAT_WALK:
      cat.x += 0.7f * cat.dir;
      if ((cat.dir > 0 && cat.x >= cat.target) || (cat.dir < 0 && cat.x <= cat.target) || cat.x <= 22 || cat.x >= 298) {
        cat.st = CAT_SIT;
        cat.until = ms + 2000 + rnd(5000);
      }
      break;
    case CAT_CHASE: {
      float d = cat.bx - cat.x;
      cat.dir = d > 0 ? 1 : -1;
      if (fabsf(d) > 16) {
        cat.x += 1.8f * cat.dir;
      } else if (fabsf(cat.bvx) < 0.6f) {
        cat.st = CAT_BAT;
        cat.until = ms + 350;
      }
      break;
    }
    case CAT_BAT:
      if ((int32_t)(ms - cat.until) > 0) {
        cat.bvx = cat.dir * (2.2f + rnd(100) / 40.0f);  // patiyle iter
        if (rnd(100) < 65) {
          cat.st = CAT_CHASE;
        } else {
          cat.st = CAT_SIT;
          cat.until = ms + 1500 + rnd(3000);
        }
      }
      break;
    case CAT_HAPPY:
      cat.hop = ms - cat.happyAt < 160 ? 2 : (ms - cat.happyAt < 320 ? 4 : (ms - cat.happyAt < 480 ? 2 : 0));
      if ((int32_t)(ms - cat.until) > 0) {
        cat.st = CAT_SIT;
        cat.hop = 0;
        cat.until = ms + 2500;
      }
      break;
  }
  cat.x = constrain(cat.x, 22.f, 298.f);
}

static void catPet(int x) {
  cat.dir = x >= cat.x ? 1 : -1;
  cat.st = CAT_HAPPY;
  cat.happyAt = millis();
  cat.until = millis() + 1600;
}

// Seri alevi (~11×14)
static void drawFlame(int x, int y, uint16_t bg) {
  const uint16_t o = RGB(255, 112, 32), m = RGB(255, 172, 40), c = RGB(255, 230, 130);
  tft.fillSmoothCircle(x + 5, y + 9, 5, o, bg);
  tft.fillTriangle(x + 0, y + 8, x + 10, y + 8, x + 6, y + 0, o);
  tft.fillTriangle(x + 0, y + 8, x + 3, y + 8, x + 1, y + 3, o);
  tft.fillSmoothCircle(x + 5, y + 10, 3, m, o);
  tft.fillTriangle(x + 3, y + 10, x + 7, y + 10, x + 5, y + 4, m);
  tft.fillRect(x + 4, y + 10, 2, 2, c);
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
  struct tm t;
  String s;
  if (datePhase && extra.length()) s = extra;
  else s = localNow(t) ? String(GUN_UP[t.tm_wday]) + ", " + String(t.tm_mday) + " " + AY_UP[t.tm_mon] : String("SAAT AYARLANIYOR…");
  bool alt = datePhase && extra.length();
  int iw = notifIconsW();
  int room = 216 - iw - (iw ? 6 : 0);
  if (streak > 0 && !alt) {
    // Seri tarihin yanında alevle gösterilir; dönüşümlü satır yalnızca sınava kalan gün.
    String num = String(streak);
    int extraW = 16 + textW(num, F_SMALL) + 2;
    String shown = fit(s, F_SMALL, room - extraW);
    text(shown, 14, 10, F_SMALL, alt ? COL_RED : COL_MUTED, COL_BG);
    int fx = 14 + textW(shown, F_SMALL) + 6;
    drawFlame(fx, 8, COL_BG);
    if (num.length()) text(num, fx + 13, 10, F_SMALL, COL_TEXT, COL_BG);
  } else {
    text(fit(s, F_SMALL, room), 14, 10, F_SMALL, alt ? COL_RED : COL_MUTED, COL_BG);
  }
  drawNotifIcons();
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
  const int X = 8, Y = 122, W = 304;
  int wMax = textW("0:00:00", F_TITLE);
  tft.fillRect(X + W - 14 - wMax - 2, Y + 18, wMax + 2, 34, COL_INK);
  text(fmtClock(swElapsedMs() / 1000), X + W - 14, Y + 20, F_TITLE, COL_ON_INK, COL_INK, TR_DATUM);
  if (swDk > 0) progressBar(X + 14, Y + 65, W - 28, 4, (int)(swElapsedMs() / 600 / swDk), COL_ON_INK, COL_INK_LINE, COL_INK);
}

static void drawNowCard() {
  const int X = 8, Y = 122, W = 304, H = 72;
  int studied = 0;
  View v;
  std::vector<Item> items = displayItems(studied, v);

  if (swRunning || swElapsedMs() > 0) {
    card(X, Y, W, H, 14, COL_INK, COL_INK);
    dotLabel(swRunning ? "KRONOMETRE" : "DURAKLATILDI", X + 14, Y + 7, swRunning ? COL_RED : COL_INK_MUTED, COL_INK_MUTED, COL_INK);
    int maxW = W - 28 - textW("0:00:00", F_TITLE) - 14;
    text(fit(swDers.length() ? swDers : String("Serbest çalışma"), F_TEXT, maxW), X + 14, Y + 23, F_TEXT, COL_ON_INK, COL_INK);
    text(fit(swKonu, F_SMALL, maxW), X + 14, Y + 44, F_SMALL, COL_INK_MUTED, COL_INK);
    drawNowTimerTick();
    return;
  }

  card(X, Y, W, H, 14, COL_SURFACE, COL_LINE);
  String label, line1, line2;
  bool streakLine = false;
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
      streakLine = v.streak > 0;
    }
  }
  dotLabel(label, X + 14, Y + 7, dot, COL_MUTED, COL_SURFACE);
  text(fmtMin(studied) + " / " + fmtMin(v.goalMin), X + W - 14, Y + 7, F_SMALL, COL_MUTED, COL_SURFACE, TR_DATUM);
  text(fit(line1, F_TEXT, W - 28), X + 14, Y + 23, F_TEXT, COL_TEXT, COL_SURFACE);
  text(fit(line2, F_SMALL, W - 28), X + 14, Y + 44, F_SMALL, COL_MUTED, COL_SURFACE);
  if (streakLine) drawFlame(X + 14 + textW(line2, F_SMALL) + 5, Y + 43, COL_SURFACE);
  progressBar(X + 14, Y + 65, W - 28, 4, v.goalMin > 0 ? studied * 100 / v.goalMin : 0, COL_TEXT, COL_TRACK, COL_SURFACE);
}

static void drawHome(bool full) {
  if (full) fullClear();
  drawDate();
  drawClock();
  drawWeatherCard();
  drawNowCard();
  if (full) catDrawAll();
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
static int setPage = 0;
static const char* SET_ROWS[2][SET_N] = {
    {"Wi-Fi ve hesap ayarları", "Bluetooth", "Tema", "Kapak rengini temaya uygula", "Diğer ayarlar"},
    {"Dokunmatiği yeniden ayarla", "Renkleri ters çevir", "Güncellemeyi denetle", "Bluetooth eşleşmesini sil", "Geri"}};

static String bleStatusText() {
  if (bleLinked) return "Bağlı";
  if (blePairUntil && (int32_t)(blePairUntil - millis()) > 0) return "Eşleşme kipi";
  if (!bleHasToken) return "Eşleşmedi";
  return "Bekleniyor";
}

static void drawSettings(bool full) {
  if (full) fullClear();
  else clearBg(0, 0, SCREEN_W, TAB_Y);
  card(8, SET_Y, 304, SET_RH * SET_N, 12, COL_SURFACE, COL_LINE);
  for (int i = 0; i < SET_N; i++) {
    int y = SET_Y + i * SET_RH;
    if (i) tft.drawFastHLine(20, y, 280, COL_LINE);
    String val;
    if (setPage == 0) {
      if (i == 1) val = bleStatusText();
      if (i == 2) val = THEME_NAMES[constrain(cfg.theme, 0, 2)];
      if (i == 3) val = cfg.coverTheme ? "Açık" : "Kapalı";
      if (i == 4) val = "›";
    } else {
      if (i == 1) val = cfg.invert ? "Açık" : "Kapalı";
      if (i == 4) val = "‹";
    }
    uint16_t col = (setPage == 1 && i == 3) ? COL_RED : COL_TEXT;
    text(SET_ROWS[setPage][i], 20, y + SET_RH / 2, F_SMALL, col, COL_SURFACE, ML_DATUM);
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
  if (setPage == 1) {  // tanı bilgisi: kart neden yeniden başladı, bellek, hava durumu
    text(fit(bootReason, F_SMALL, 296), 12, 162, F_SMALL, COL_MUTED, COL_BG);
    String w = weatherAt ? "Hava: " + String((millis() - weatherAt) / 60000) + " dk önce alındı"
                         : "Hava alınamadı (" + String(weatherHttp) + ")";
    String m = "Bellek " + String(ESP.getFreeHeap() / 1024) + " KB, en az " + String(ESP.getMinFreeHeap() / 1024) + " KB · " + w;
    text(fit(m, F_SMALL, 296), 12, 180, F_SMALL, COL_MUTED, COL_BG);
    return;
  }
  String l1 = "fw-" + String(FW_BUILD) + " · " FW_VARIANT;
  if (WiFi.status() == WL_CONNECTED) l1 += " · " + WiFi.localIP().toString() + " · " + String(WiFi.RSSI()) + " dBm";
  text(fit(l1, F_SMALL, 296), 12, 162, F_SMALL, COL_MUTED, COL_BG);
  String l2 = ota;
  if (!l2.length()) {
    l2 = (bleLinked ? String("Bluetooth bağlı") : "Bulut: " + net + (liveOn ? " (canlı)" : "")) + " · SD " + (sdOk ? "var" : "yok");
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

// ============================================================================
// Blok Patlat (8×8 blok bulmaca)
// ============================================================================
// Yandaki üç parçadan birini sürükleyip tahtaya bırak; dolan satır ve sütunlar patlar.
// Üç parça da kullanılınca yenileri gelir. Hiçbiri sığmazsa oyun biter.
static const int BP_N = 8, BP_CELL = 23, BP_X = 9, BP_Y = 9;
static const char* BP_SHAPES[] = {
    "1",        "11",       "1/1",      "111",      "1/1/1",    "1111",     "1/1/1/1",  "11111",    "1/1/1/1/1",
    "11/11",    "111/111/111", "111/111", "11/11/11", "11/1.",  "11/.1",    "1./11",    ".1/11",    "111/1..",
    "111/..1",  "1../111",  "..1/111",  "11/1./1.", "11/.1/.1", "1./1./11", ".1/.1/11", "111/.1.",  ".1./111",
    "1./11/1.", ".1/11/.1", "11./.11",  ".11/11.",  "1./11/.1", ".1/11/1."};
static const int BP_SHAPE_N = sizeof(BP_SHAPES) / sizeof(BP_SHAPES[0]);

struct BpPiece {
  uint8_t shape = 0, w = 0, h = 0, n = 0, color = 1;
  uint8_t cells[9][2];  // (satır, sütun)
  bool used = true;
};
static uint8_t bpBoard[BP_N][BP_N];
static BpPiece bpPieces[3];
static uint32_t bpScore = 0, bpBest = 0;
static int bpCombo = 0, bpMiss = 0;
static bool bpOver = false, bpLoaded = false;
static int bpDrag = -1, bpGhostR = -100, bpGhostC = -100;
static bool bpGhostOk = false;
static uint32_t bpResetAsk = 0;

static uint16_t bpColor(int i) {
  switch (i) {
    case 1: return RGB(66, 133, 244);
    case 2: return RGB(52, 168, 83);
    case 3: return RGB(240, 180, 0);
    case 4: return RGB(234, 67, 53);
    case 5: return RGB(255, 112, 67);
    case 6: return RGB(156, 39, 176);
    default: return RGB(0, 172, 193);
  }
}

static void bpMakePiece(BpPiece& p, int idx) {
  const char* s = BP_SHAPES[idx];
  p.shape = idx;
  p.n = p.w = 0;
  int r = 0, c = 0;
  for (; *s; s++) {
    if (*s == '/') {
      r++;
      c = 0;
      continue;
    }
    if (*s == '1' && p.n < 9) {
      p.cells[p.n][0] = r;
      p.cells[p.n][1] = c;
      p.n++;
    }
    c++;
    if (c > p.w) p.w = c;
  }
  p.h = r + 1;
  p.used = false;
}

static bool bpFits(const BpPiece& p, int r0, int c0) {
  for (int i = 0; i < p.n; i++) {
    int r = r0 + p.cells[i][0], c = c0 + p.cells[i][1];
    if (r < 0 || c < 0 || r >= BP_N || c >= BP_N || bpBoard[r][c]) return false;
  }
  return true;
}

static bool bpPlaceable(const BpPiece& p) {
  if (p.used) return false;
  for (int r = 0; r + p.h <= BP_N; r++)
    for (int c = 0; c + p.w <= BP_N; c++)
      if (bpFits(p, r, c)) return true;
  return false;
}

static bool bpAnyMove() {
  for (auto& p : bpPieces)
    if (bpPlaceable(p)) return true;
  return false;
}

static void bpNewSet() {
  for (int tries = 0; tries < 12; tries++) {
    for (auto& p : bpPieces) {
      bpMakePiece(p, esp_random() % BP_SHAPE_N);
      p.color = 1 + esp_random() % 7;
    }
    if (bpAnyMove()) break;
  }
}

static void bpSave() {
  Lock l;
  uint8_t pc[6];
  for (int k = 0; k < 3; k++) {
    pc[k * 2] = bpPieces[k].used ? 255 : bpPieces[k].shape;
    pc[k * 2 + 1] = bpPieces[k].color;
  }
  prefs.begin("bp", false);
  prefs.putBytes("b", bpBoard, sizeof bpBoard);
  prefs.putBytes("p", pc, sizeof pc);
  prefs.putUInt("s", bpScore);
  prefs.putUInt("best", bpBest);
  prefs.putInt("cb", bpCombo);
  prefs.end();
}

static void bpNewGame() {
  memset(bpBoard, 0, sizeof bpBoard);
  bpScore = 0;
  bpCombo = bpMiss = 0;
  bpOver = false;
  bpNewSet();
  bpSave();
}

static void bpLoad() {
  uint8_t pc[6];
  size_t nb, np;
  {
    Lock l;
    prefs.begin("bp", true);
    nb = prefs.getBytes("b", bpBoard, sizeof bpBoard);
    np = prefs.getBytes("p", pc, sizeof pc);
    bpScore = prefs.getUInt("s", 0);
    bpBest = prefs.getUInt("best", 0);
    bpCombo = prefs.getInt("cb", 0);
    prefs.end();
  }
  if (nb != sizeof bpBoard || np != sizeof pc) {
    bpNewGame();
    return;
  }
  bool any = false;
  for (int k = 0; k < 3; k++) {
    if (pc[k * 2] >= BP_SHAPE_N) {
      bpPieces[k].used = true;
      continue;
    }
    bpMakePiece(bpPieces[k], pc[k * 2]);
    bpPieces[k].color = constrain(pc[k * 2 + 1], 1, 7);
    any = true;
  }
  for (auto& row : bpBoard)
    for (auto& v : row)
      if (v > 7) v = 0;
  if (!any) bpNewSet();
  bpOver = !bpAnyMove();
}

static void bpDrawCell(int r, int c, uint16_t col) {
  int x = BP_X + c * BP_CELL, y = BP_Y + r * BP_CELL;
  tft.fillRoundRect(x + 1, y + 1, BP_CELL - 2, BP_CELL - 2, 4, col);
}

// Tahta; sürüklenen parça varsa nereye düşeceği ve patlayacak çizgiler gösterilir.
static void bpDrawBoard() {
  bool ghost[BP_N][BP_N] = {};
  bool clearRow[BP_N] = {}, clearCol[BP_N] = {};
  uint16_t pcol = 0;
  if (bpDrag >= 0 && bpGhostOk) {
    const BpPiece& p = bpPieces[bpDrag];
    pcol = bpColor(p.color);
    for (int i = 0; i < p.n; i++) ghost[bpGhostR + p.cells[i][0]][bpGhostC + p.cells[i][1]] = true;
    for (int r = 0; r < BP_N; r++) {
      bool full = true;
      for (int c = 0; c < BP_N; c++)
        if (!bpBoard[r][c] && !ghost[r][c]) full = false;
      clearRow[r] = full;
    }
    for (int c = 0; c < BP_N; c++) {
      bool full = true;
      for (int r = 0; r < BP_N; r++)
        if (!bpBoard[r][c] && !ghost[r][c]) full = false;
      clearCol[c] = full;
    }
  }
  for (int r = 0; r < BP_N; r++)
    for (int c = 0; c < BP_N; c++) {
      uint16_t col;
      if (ghost[r][c]) col = (clearRow[r] || clearCol[c]) ? pcol : tft.alphaBlend(120, pcol, COL_TRACK);
      else if (bpBoard[r][c]) col = (clearRow[r] || clearCol[c]) ? pcol : bpColor(bpBoard[r][c]);
      else col = COL_TRACK;
      bpDrawCell(r, c, col);
    }
}

static void bpDrawSlot(int k) {
  int y0 = 80 + k * 40;
  tft.fillRect(202, y0, 112, 40, COL_BG);
  gridLines(tft, 0, 0, 202, y0, 112, 40);
  const BpPiece& p = bpPieces[k];
  if (p.used) return;
  const int m = std::min(12, 36 / std::max<int>(p.w, p.h));  // parça yuvaya sığacak kadar büyük
  int x0 = 258 - p.w * m / 2, yy = y0 + 20 - p.h * m / 2;
  uint16_t col = bpColor(p.color);
  if (bpDrag == k) col = tft.alphaBlend(90, col, COL_BG);
  else if (!bpPlaceable(p)) col = tft.alphaBlend(70, col, COL_BG);
  for (int i = 0; i < p.n; i++) tft.fillRoundRect(x0 + p.cells[i][1] * m, yy + p.cells[i][0] * m, m - 1, m - 1, m > 9 ? 3 : 2, col);
}

static void bpDrawPanel() {
  clearBg(200, 0, 120, 78);
  text("SKOR", 206, 8, F_SMALL, COL_MUTED, COL_BG);
  text(String(bpScore), 206, 24, F_TITLE, COL_TEXT, COL_BG);
  text("En iyi " + String(bpBest), 206, 56, F_SMALL, COL_MUTED, COL_BG);
  // Yeni oyun düğmesi (iki kez dokununca)
  card(280, 6, 32, 28, 9, bpResetAsk ? COL_RED_SOFT : COL_SURFACE, bpResetAsk ? COL_RED : COL_LINE);
  tft.drawSmoothArc(296, 20, 7, 5, 60, 360, bpResetAsk ? COL_RED : COL_TEXT, bpResetAsk ? COL_RED_SOFT : COL_SURFACE, true);
  tft.fillTriangle(296, 9, 302, 13, 296, 17, bpResetAsk ? COL_RED : COL_TEXT);
  if (bpCombo > 1) text("Kombo x" + String(bpCombo), 312, 56, F_SMALL, COL_RED, COL_BG, TR_DATUM);
}

static void bpDrawOver() {
  card(22, 58, 160, 92, 14, COL_INK, COL_INK);
  text("Oyun bitti", 102, 74, F_TEXT, COL_ON_INK, COL_INK, TC_DATUM);
  text("Skor " + String(bpScore), 102, 98, F_SMALL, COL_INK_MUTED, COL_INK, TC_DATUM);
  button(42, 116, 120, 28, "Yeniden", BTN_INK_PRIMARY);
}

static void drawGame(bool full) {
  if (!bpLoaded) {
    bpLoad();
    bpLoaded = true;
  }
  if (full) fullClear();
  card(BP_X - 3, BP_Y - 3, BP_N * BP_CELL + 6, BP_N * BP_CELL + 6, 8, COL_SURFACE, COL_LINE);
  bpDrawBoard();
  bpDrawPanel();
  for (int k = 0; k < 3; k++) bpDrawSlot(k);
  if (bpOver) bpDrawOver();
}

static void bpClearLines(int& lines) {
  bool row[BP_N], col[BP_N];
  lines = 0;
  for (int r = 0; r < BP_N; r++) {
    row[r] = true;
    for (int c = 0; c < BP_N; c++)
      if (!bpBoard[r][c]) row[r] = false;
    if (row[r]) lines++;
  }
  for (int c = 0; c < BP_N; c++) {
    col[c] = true;
    for (int r = 0; r < BP_N; r++)
      if (!bpBoard[r][c]) col[c] = false;
    if (col[c]) lines++;
  }
  if (!lines) return;
  for (int r = 0; r < BP_N; r++)
    for (int c = 0; c < BP_N; c++)
      if (row[r] || col[c]) bpDrawCell(r, c, COL_ON_INK);
  delay(110);
  for (int r = 0; r < BP_N; r++)
    for (int c = 0; c < BP_N; c++)
      if (row[r] || col[c]) bpBoard[r][c] = 0;
}

static void gameDown(int x, int y) {
  if (bpOver) {
    if (x >= 42 && x < 162 && y >= 110 && y < 150) {
      bpNewGame();
      drawGame(false);
    }
    return;
  }
  if (x >= 278 && y < 38) {
    if (bpResetAsk && millis() - bpResetAsk < 3000) {
      bpResetAsk = 0;
      bpNewGame();
      drawGame(false);
    } else {
      bpResetAsk = millis();
      bpDrawPanel();
      toast("Yeni oyun için bir kez daha dokun");
    }
    return;
  }
  if (x >= 200 && y >= 80 && y < 200) {
    int k = (y - 80) / 40;
    if (k >= 0 && k < 3 && !bpPieces[k].used) {
      bpDrag = k;
      bpGhostR = bpGhostC = -100;
      bpGhostOk = false;
      bpDrawSlot(k);
    }
  }
}

static void gameMove(int x, int y) {
  if (bpDrag < 0) return;
  const BpPiece& p = bpPieces[bpDrag];
  // Parça parmağın biraz üstünde durur ki nereye düştüğü görünsün.
  int ax = x - p.w * BP_CELL / 2, ay = y - p.h * BP_CELL - 14;
  int c0 = (int)lroundf((ax - BP_X) / (float)BP_CELL), r0 = (int)lroundf((ay - BP_Y) / (float)BP_CELL);
  bool ok = bpFits(p, r0, c0);
  if (!ok && !bpGhostOk) return;
  if (r0 == bpGhostR && c0 == bpGhostC && ok == bpGhostOk) return;
  bpGhostR = r0;
  bpGhostC = c0;
  bpGhostOk = ok;
  bpDrawBoard();
}

static void gameUp(int x, int y) {
  if (bpDrag < 0) return;
  int k = bpDrag;
  BpPiece& p = bpPieces[k];
  bool placed = bpGhostOk;
  if (placed) {
    for (int i = 0; i < p.n; i++) bpBoard[bpGhostR + p.cells[i][0]][bpGhostC + p.cells[i][1]] = p.color;
    p.used = true;
  }
  bpDrag = -1;
  bpGhostOk = false;
  bpGhostR = bpGhostC = -100;
  if (!placed) {
    bpDrawBoard();
    bpDrawSlot(k);
    return;
  }
  bpScore += p.n;
  bpDrawBoard();
  int lines = 0;
  bpClearLines(lines);
  if (lines) {
    bpCombo++;
    bpMiss = 0;
    bpScore += 10 * lines * lines + (bpCombo > 1 ? 10 * (bpCombo - 1) * lines : 0);
  } else if (++bpMiss >= 3) {
    bpCombo = 0;
  }
  if (bpPieces[0].used && bpPieces[1].used && bpPieces[2].used) bpNewSet();
  if (bpScore > bpBest) bpBest = bpScore;
  bpOver = !bpAnyMove();
  bpSave();
  drawGame(false);
}

// Oyun ekranında dokunuşlar sürükleme olarak işlenir. Alt çubuktan başlayan dokunuş normal işlenir.
static bool gameTouch(bool t, int x, int y) {
  static bool active = false;
  static uint32_t lastSeen = 0;
  static int lx = 0, ly = 0;
  uint32_t ms = millis();
  if (!active) {
    if (!t || y >= TAB_Y - 2) return false;
    active = true;
    lastSeen = ms;
    lx = x;
    ly = y;
    if (!wakeOnly()) gameDown(x, y);
    return true;
  }
  if (t) {
    lastSeen = ms;
    int nx = (lx + x) / 2, ny = (ly + y) / 2;  // dokunmatik titremesini yumuşat
    if (abs(nx - lx) + abs(ny - ly) >= 2) {
      lx = nx;
      ly = ny;
      gameMove(lx, ly);
    }
    return true;
  }
  if (ms - lastSeen > 40) {
    active = false;
    gameUp(lx, ly);
  }
  return true;
}

static void drawScreen(bool full) {
  switch (screen) {
    case SCR_HOME: drawHome(full); break;
    case SCR_PLAN: drawPlan(full); break;
    case SCR_TIMER: drawTimer(full); break;
    case SCR_MUSIC: drawMusic(full); break;
    case SCR_GAME: drawGame(full); break;
    case SCR_SETTINGS: drawSettings(full); break;
  }
  if (menuOpen && screen == SCR_PLAN) drawMenu();
  if (notifListOpen && screen == SCR_HOME) drawNotifList();
}

static void go(Screen s) {
  if (screen == SCR_GAME && bpDrag >= 0) bpDrag = -1;
  screen = s;
  toastUntil = 0;
  menuOpen = false;
  notifListOpen = false;
  if (s == SCR_SETTINGS) setPage = 0;
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
    esp_ota_mark_app_valid_cancel_rollback();
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
  for (auto& s : sentLines) q.push_back(s);
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
  bleTimerPending = true;
  remoteTimerDirty = false;
  timerSave(t);
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
    if (remoteTimer.on && remoteTimer.startedAt > 0 && !timeOk()) return;  // saat gelince uygulanır
    r = remoteTimer;
    remoteTimerDirty = false;
  }
  if (r.by != "restore") timerSave(r);
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
// Bluetooth (telefonla internetsiz bağlantı, bildirim ve müzik) — docs/bluetooth.md
// ============================================================================
// Kart çevre birimidir; telefon bağlanır ve "H" mesajıyla eşleşme anahtarını yollar. Kart ilk
// eşleşen telefonun anahtarını saklar; başka telefon yalnızca Ayarlar → Bluetooth eşleşme
// kipindeyken eşleşebilir. Mesajlar 0xA5, tür, uzunluk (2 bayt) ve veriden oluşan çerçevelerdir.
static NimBLEServer* bleServer = nullptr;
static NimBLECharacteristic* bleTxChar = nullptr;
static volatile uint16_t bleMtu = 23;
static volatile uint16_t bleConn = 0;
static std::vector<uint8_t> bleRx;
static uint32_t bleDropAt = 0, bleInflightAt = 0, bleStatusAt = 0;

class BleServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* d) override {
    bleConn = d->conn_handle;
    bleMtu = 23;
    {
      Lock l;
      bleRx.clear();
    }
    bleConnected = true;
  }
  void onDisconnect(NimBLEServer* s) override {
    bleConnected = false;
    bleLinked = false;
  }
  void onMTUChange(uint16_t mtu, ble_gap_conn_desc* d) override { bleMtu = mtu; }
};

// Gelen veri tamponu açılışta bir kez ayrılır ve hiç büyümez: sonradan büyük bellek istemek,
// bellek parçalandığında kartı çökertiyordu.
static const size_t BLE_RX_CAP = 16400;
class BleRxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    NimBLEAttValue v = c->getValue();
    Lock l;
    if (bleRx.size() + v.length() <= BLE_RX_CAP) bleRx.insert(bleRx.end(), v.data(), v.data() + v.length());
    else bleRx.clear();  // taşma: akış bozuldu, baştan
  }
};

static String bleTokenLoad() {
  Lock l;
  prefs.begin("cs", true);
  String t = prefs.getString("btok", "");
  prefs.end();
  return t;
}

static void bleTokenSave(const String& t) {
  Lock l;
  prefs.begin("cs", false);
  if (t.length()) prefs.putString("btok", t);
  else prefs.remove("btok");
  prefs.end();
  bleHasToken = t.length() > 0;
}

static void bleBegin() {
  bleRx.reserve(BLE_RX_CAP);
  if (!artPx) artPx = (uint16_t*)malloc(ART_PX * ART_PX * 2);
  bleHasToken = bleTokenLoad().length() > 0;
  NimBLEDevice::init(BLE_NAME);
  NimBLEDevice::setMTU(247);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new BleServerCb());
  NimBLEService* svc = bleServer->createService(BLE_SVC);
  NimBLECharacteristic* rx = svc->createCharacteristic(BLE_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new BleRxCb());
  bleTxChar = svc->createCharacteristic(BLE_TX, NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SVC);
  adv->setScanResponse(true);
  adv->start();
}

// Çerçeve yığında (stack) parça parça kurulur; gönderirken bellek ayrılmaz.
static void bleSend(char type, const uint8_t* data, size_t n) {
  if (!bleConnected || !bleTxChar || n > 16000) return;
  uint8_t head[4] = {0xA5, (uint8_t)type, (uint8_t)(n & 0xFF), (uint8_t)((n >> 8) & 0xFF)};
  uint8_t buf[244];
  size_t chunk = std::min<size_t>(sizeof buf, std::max<int>(20, (int)bleMtu - 3));
  size_t total = n + 4, sent = 0;
  while (sent < total) {
    size_t m = std::min(chunk, total - sent);
    for (size_t i = 0; i < m; i++) {
      size_t k = sent + i;
      buf[i] = k < 4 ? head[k] : data[k - 4];
    }
    bleTxChar->setValue(buf, m);
    bleTxChar->notify();
    sent += m;
    if (total > chunk) delay(4);
  }
}

static void bleSendJson(char type, JsonDocument& d) {
  String s;
  serializeJson(d, s);
  bleSend(type, (const uint8_t*)s.c_str(), s.length());
}

static void bleSendStatus() {
  JsonDocument d;
  d["fw"] = "fw-" + String(FW_BUILD);
  d["variant"] = FW_VARIANT;
  d["heap"] = ESP.getFreeHeap();
  d["minHeap"] = ESP.getMinFreeHeap();
  d["reset"] = bootReason;
  d["sd"] = sdOk;
  d["queue"] = queueCount();
  d["wifi"] = WiFi.status() == WL_CONNECTED;
  bleSendJson('S', d);
  bleStatusAt = millis();
}

static void bleSendTimer() {
  SharedTimer t;
  {
    Lock l;
    t = outTimer;
    bleTimerPending = false;
  }
  if (!t.rev) return;
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
  bleSendJson('T', d);
}

// Kuyruktaki olaylar 10'arlı paketlerle telefona gider; telefon "a" ile onaylayınca silinir.
static void bleFlushEvents() {
  static uint32_t lastTry = 0;
  if (bleInflight) {
    if (millis() - bleInflightAt < 8000) return;
    bleInflight = 0;  // onay gelmedi; yeniden gönderilir (anahtarlar aynı, çift kayıt olmaz)
  }
  if (millis() - lastTry < 500) return;
  lastTry = millis();
  {
    Lock l;
    if (fbFlushing) return;
    bleInflight = -1;  // bulutun aynı anda göndermesini engelle
  }
  std::vector<String> batch = queuePeek(10);
  if (batch.empty()) {
    bleInflight = 0;
    return;
  }
  String body = "{";
  int valid = 0;
  for (auto& line : batch) {
    JsonDocument t;
    if (deserializeJson(t, line) || !t.is<JsonObject>()) continue;
    if (valid++) body += ',';
    body += '"';
    body += eventKey(line);
    body += "\":";
    body += line;
  }
  body += '}';
  if (!valid) {
    queueDrop(batch.size());
    bleInflight = 0;
    return;
  }
  bleInflight = batch.size();
  bleInflightAt = millis();
  bleSend('E', (const uint8_t*)body.c_str(), body.length());
}

static void bleOnLinked() {
  bleLinked = true;
  bleInflight = 0;
  bleSendStatus();
  dataDirty = true;
}

static void bleOnUnlinked() {
  bleLinked = false;
  bleInflight = 0;
  media.on = false;
  if (accentOn) {
    accentOn = false;
    applyPalette();
  }
  dataDirty = true;
}

static void showNotif(const Notif& n) {
  banner = n;
  if (screen == SCR_GAME || menuOpen || notifListOpen || otaProgress >= 0) return;
  drawBanner();
  toastUntil = millis() + BANNER_MS;
}

static bool artPending = false;
static void bleImage(char type, const uint8_t* p, size_t len) {
  if (len < 8) return;
  uint32_t id = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
  int w = p[4] | (p[5] << 8), h = p[6] | (p[7] << 8);
  if (len != 8 + (size_t)w * h * 2) return;
  if (type == 'I' && w == ICON_PX && h == ICON_PX) {
    IconImg* ic = findIcon(id);
    if (!ic) {
      if (icons.size() >= 12) {  // en uzun süredir kullanılmayan çıkar
        auto old = std::min_element(icons.begin(), icons.end(), [](IconImg* a, IconImg* b) { return a->used < b->used; });
        delete *old;
        icons.erase(old);
      }
      ic = new (std::nothrow) IconImg();
      if (!ic) return;
      icons.push_back(ic);
    }
    ic->id = id;
    ic->used = millis();
    memcpy(ic->px, p + 8, ICON_PX * ICON_PX * 2);
  } else if (type == 'A' && w == ART_PX && h == ART_PX && artPx) {
    memcpy(artPx, p + 8, ART_PX * ART_PX * 2);
    artId = id;
    artPending = true;  // çizim kilit dışında yapılır
  }
}

static void bleHandle(char type, const std::vector<uint8_t>& p) {
  if (!bleLinked && type != 'H') return;
  JsonDocument d;
  if (deserializeJson(d, (const char*)p.data(), p.size())) return;
  switch (type) {
    case 'H': {  // merhaba + eşleşme anahtarı
      String tok = String((const char*)(d["token"] | ""));
      String saved = bleTokenLoad();
      bool pairing = blePairUntil && (int32_t)(blePairUntil - millis()) > 0;
      bool ok = false, paired = false;
      if (tok.length() >= 16) {
        if (saved.length() && tok == saved) {
          ok = true;
        } else if (!saved.length() || pairing) {
          bleTokenSave(tok);
          ok = paired = true;
          blePairUntil = 0;
        }
      }
      JsonDocument r;
      r["ok"] = ok;
      r["paired"] = paired;
      r["fw"] = "fw-" + String(FW_BUILD);
      r["variant"] = FW_VARIANT;
      bleSendJson('h', r);
      if (!ok) {
        bleDropAt = millis() + 600;
        break;
      }
      long long t = d["t"].as<long long>();
      if (t > 1700000000000LL && (!timeOk() || llabs(nowMs() - t) > 2000)) {
        struct timeval tv = {(time_t)(t / 1000), (suseconds_t)((t % 1000) * 1000)};
        settimeofday(&tv, nullptr);
      }
      bleOnLinked();
      toast(paired ? "Telefonla eşleşti" : "Telefon bağlandı");
      break;
    }
    case 'C': {  // saat
      long long t = d["t"].as<long long>();
      if (t > 1700000000000LL && (!timeOk() || llabs(nowMs() - t) > 2000)) {
        struct timeval tv = {(time_t)(t / 1000), (suseconds_t)((t % 1000) * 1000)};
        settimeofday(&tv, nullptr);
        dataDirty = true;
      }
      break;
    }
    case 'V':
      applyView(d.as<JsonVariantConst>());
      break;
    case 'T':
      takeRemoteTimer(d.as<JsonVariantConst>());
      break;
    case 'a':  // olaylar telefona ulaştı
      if (bleInflight > 0) queueDropSent(bleInflight);
      bleInflight = 0;
      break;
    case 'N': {
      Notif n;
      n.id = String((const char*)(d["id"] | ""));
      n.app = String((const char*)(d["app"] | ""));
      n.title = String((const char*)(d["title"] | ""));
      n.text = String((const char*)(d["text"] | ""));
      n.icon = d["ic"].as<uint32_t>();
      notifs.erase(std::remove_if(notifs.begin(), notifs.end(), [&](const Notif& x) { return x.id == n.id; }), notifs.end());
      notifs.insert(notifs.begin(), n);
      if (notifs.size() > 8) notifs.pop_back();
      showNotif(n);
      if (screen == SCR_HOME && !toastUntil) {
        if (notifListOpen) drawNotifList();
        else drawDate();
      }
      break;
    }
    case 'X': {
      String id = String((const char*)(d["id"] | ""));
      size_t before = notifs.size();
      notifs.erase(std::remove_if(notifs.begin(), notifs.end(), [&](const Notif& x) { return x.id == id; }), notifs.end());
      if (notifs.size() != before && screen == SCR_HOME && !toastUntil) {
        if (notifListOpen) drawNotifList();
        else drawDate();
      }
      break;
    }
    case 'M': {
      media.on = d["on"] | false;
      media.enabled = d["enabled"] | true;
      media.app = String((const char*)(d["app"] | ""));
      media.title = String((const char*)(d["title"] | ""));
      media.artist = String((const char*)(d["artist"] | ""));
      media.playing = d["playing"] | false;
      media.pos = d["pos"].as<uint32_t>();
      media.dur = d["dur"].as<uint32_t>();
      media.at = millis();
      media.vol = d["vol"] | 0;
      media.volMax = d["volMax"] | 15;
      media.art = d["art"].as<uint32_t>();
      bool hasAcc = media.on && d["accent"].is<JsonArrayConst>() && d["dark"].is<JsonArrayConst>();
      bool accChanged = hasAcc != accentOn;
      if (hasAcc) {
        uint8_t a[3], k[3];
        for (int i = 0; i < 3; i++) {
          a[i] = d["accent"][i] | 0;
          k[i] = d["dark"][i] | 0;
        }
        if (memcmp(a, accRgb, 3) || memcmp(k, darkRgb, 3)) accChanged = true;
        memcpy(accRgb, a, 3);
        memcpy(darkRgb, k, 3);
      }
      accentOn = hasAcc;
      if (accChanged && cfg.coverTheme) {
        applyPalette();
        if (!toastUntil) drawScreen(true);
      } else if (screen == SCR_MUSIC && !toastUntil) {
        drawMusic(false);
      }
      break;
    }
    case 'U':  // telefon eşleşmeyi kaldırdı
      bleTokenSave("");
      bleOnUnlinked();
      bleDropAt = millis() + 300;
      toast("Telefonla eşleşme kaldırıldı");
      break;
    default:
      break;
  }
}

// Ana döngüden çağrılır: gelen çerçeveleri işler, bekleyenleri gönderir.
static void bleProcess() {
  static bool wasLinked = false;
  if (wasLinked && !bleLinked) bleOnUnlinked();
  wasLinked = bleLinked;
  if (bleDropAt && (int32_t)(millis() - bleDropAt) >= 0) {
    bleDropAt = 0;
    if (bleServer && bleConnected) bleServer->disconnect(bleConn);
  }
  for (int k = 0; k < 6; k++) {
    char type = 0;
    std::vector<uint8_t> payload;
    {
      Lock l;
      size_t i = 0;
      while (i < bleRx.size() && bleRx[i] != 0xA5) i++;
      if (i) bleRx.erase(bleRx.begin(), bleRx.begin() + i);
      if (bleRx.size() < 4) break;
      size_t len = bleRx[2] | (bleRx[3] << 8);
      if (len > 16000) {
        bleRx.erase(bleRx.begin());
        continue;
      }
      if (bleRx.size() < 4 + len) break;
      type = (char)bleRx[1];
      if (type == 'I' || type == 'A') {
        if (bleLinked) bleImage(type, bleRx.data() + 4, len);  // görüntü kopyalanmadan işlenir
        type = 0;
      } else {
        payload.assign(bleRx.begin() + 4, bleRx.begin() + 4 + len);
      }
      bleRx.erase(bleRx.begin(), bleRx.begin() + 4 + len);
    }
    if (artPending) {
      artPending = false;
      if (screen == SCR_MUSIC && !toastUntil && media.art == artId) drawCover();
    }
    if (!type) continue;
    bleHandle(type, payload);
  }
  if (bleLinked) {
    bleFlushEvents();
    if (bleTimerPending) bleSendTimer();
    if (millis() - bleStatusAt > 60000) bleSendStatus();
  }
  if (blePairUntil && (int32_t)(millis() - blePairUntil) > 0) {
    blePairUntil = 0;
    if (screen == SCR_SETTINGS && !toastUntil) drawSettings(false);
  }
}

static void bleMediaKey(const char* cmd) {
  JsonDocument d;
  d["cmd"] = cmd;
  bleSendJson('K', d);
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
    go((Screen)constrain(x * TAB_N / SCREEN_W, 0, TAB_N - 1));
    return;
  }
  switch (screen) {
    case SCR_HOME:
      if (notifListOpen) {
        if (x >= 210 && y < 44) notifs.clear();  // Temizle
        notifListOpen = false;
        drawScreen(true);
      } else if (y < 32 && x < 234 && !notifs.empty()) {
        notifListOpen = true;
        drawNotifList();
      } else if (y >= 122) {
        go(swRunning || swElapsedMs() > 0 ? SCR_TIMER : SCR_PLAN);
      } else if (y >= ZONE_TOP) {
        catPet(x);  // kediyi sev
      }
      break;
    case SCR_MUSIC:
      if (!bleLinked || !media.on) break;
      if (y >= MU_CTRL_Y - 4 && y < MU_CTRL_Y + MU_CTRL_H + 4) {
        if (x >= 48 && x < 116) {
          bleMediaKey("prev");
        } else if (x >= 116 && x < 204) {
          bleMediaKey("toggle");
          media.pos = mediaPos();
          media.at = millis();
          media.playing = !media.playing;
          drawMusic(false);
        } else if (x >= 204 && x < 272) {
          bleMediaKey("next");
        }
      } else if (y >= MU_VOL_Y - 4 && y < MU_VOL_Y + MU_VOL_H + 4) {
        if (x < 60) {
          bleMediaKey("voldown");
          if (media.vol > 0) media.vol--;
          drawMusicVolume();
        } else if (x >= 260) {
          bleMediaKey("volup");
          if (media.vol < media.volMax) media.vol++;
          drawMusicVolume();
        }
      }
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
        if (setPage == 0) {
          if (row == 0) {
            portalRequested = true;
          } else if (row == 1) {
            if (bleLinked) {
              toast("Telefon bağlı");
            } else {
              blePairUntil = millis() + BLE_PAIR_MS;
              drawSettings(false);
              toast("Uygulamada Masa saatini bul'a dokun");
            }
          } else if (row == 2) {
            cfg.theme = (cfg.theme + 1) % 3;
            saveConfig();
            applyPalette();
            drawSettings(true);
          } else if (row == 3) {
            cfg.coverTheme = !cfg.coverTheme;
            saveConfig();
            applyPalette();
            drawSettings(true);
          } else {
            setPage = 1;
            drawSettings(false);
          }
        } else {
          if (row == 0) {
            calibRequested = true;
          } else if (row == 1) {
            cfg.invert = !cfg.invert;
            tft.invertDisplay(cfg.invert);
            saveConfig();
            drawSettings(false);
          } else if (row == 2) {
            setOtaMsg("Güncelleme denetleniyor…");
            wantOta = true;
            drawSettings(false);
          } else if (row == 3) {
            bleTokenSave("");
            if (bleConnected) bleDropAt = millis() + 200;
            toast("Bluetooth eşleşmesi silindi");
            drawSettings(false);
          } else {
            setPage = 0;
            drawSettings(false);
          }
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
  int x = 0, y = 0;
  bool t = getTouch(x, y);
  uint32_t ms = millis();
  if (!down && screen == SCR_GAME && gameTouch(t, x, y)) return;
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
      go(screen >= SCR_GAME ? SCR_HOME : (Screen)((int)screen + 1));
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

// Güncellemeden sonra yeni yazılım hemen "sağlam" sayılmaz: 45 sn sorunsuz çalışınca işaretlenir.
// Daha önce çökerse ESP32 açılışta bir önceki sürüme döner (kart USB'siz kurtulur).
extern "C" bool verifyRollbackLater() { return true; }

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

void setup() {
  diagBoot();
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
  sentLoad();
  rebuildLocalEvents();
  pruneLocalEvents();
  timerRestore();

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
  // Bluetooth açıkken Wi-Fi güç tasarrufu kipinde kalmalı (ikisi aynı anteni paylaşıyor).
  if (!hasCreds()) runPortal(true);
  else WiFi.begin();  // kayıtlı ağa arka planda bağlanır; yoksa Bluetooth ile çalışır (kurulum: BOOT 6 sn)
  configTzTime("<+03>-3", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  bleBegin();

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

  static uint32_t lastDiag = 0;
  if (millis() - lastDiag > 5000) {
    lastDiag = millis();
    rtcUpSec = millis() / 1000;
    rtcMinHeap = ESP.getMinFreeHeap();
  }
  static bool markedValid = false;
  if (!markedValid && millis() > 45000) {
    markedValid = true;
    esp_ota_mark_app_valid_cancel_rollback();
  }

  handleBoot();
  handleTouch();
  bleProcess();

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
    if (screen != SCR_GAME) drawScreen(false);  // oyun ekranında plan verisi yok, titremesin
  }

  static uint32_t lastPhase = 0;
  if (millis() - lastPhase >= 6000) {
    lastPhase = millis();
    datePhase = !datePhase;
    if (screen == SCR_HOME && !toastUntil && !notifListOpen) drawDate();
  }

  static uint32_t lastCat = 0;
  if (screen == SCR_HOME && !notifListOpen) {
    bool night = isNightHour();
    uint32_t every = cat.st == CAT_SLEEP ? 700 : 70;
    if (millis() - lastCat >= every) {
      lastCat = millis();
      catUpdate(night);
      catFlush();
    }
  }

  static uint32_t lastMusicTick = 0;
  if (screen == SCR_MUSIC && media.on && media.playing && bleLinked && !toastUntil && millis() - lastMusicTick >= 1000) {
    lastMusicTick = millis();
    drawMusicProgress();
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
      if (screen == SCR_HOME && !toastUntil && !notifListOpen) {
        drawClock();
        drawNowCard();
      }
    }
    uint32_t sec = swElapsedMs() / 1000;
    if (sec != lastSec) {
      lastSec = sec;
      if (screen == SCR_TIMER && !toastUntil) drawTimerDigits();
      if (screen == SCR_HOME && swRunning && !toastUntil && !notifListOpen) drawNowTimerTick();
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
