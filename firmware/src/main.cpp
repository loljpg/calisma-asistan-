// DAXEN VANE — ESP32 Cheap Yellow Display (ESP32-2432S028R) işletim yazılımı
//
// Masaüstü bir cihaz: ana ekran (saat, hava, kedi, YKS kartı), uygulamalar (YKS, Notlar, Dosyalar,
// Ayarlar, Müzik, Medya, Bildirimler, Hesap makinesi, Saat, Hava durumu, Yapılacaklar, Oyunlar,
// Fotoğraflar, Okuyucu, Çizim, Bilgi kartları, Işık ölçer, Dosya aktarımı, Lua uygulamaları).
// Bu dosya sistem çekirdeğidir: kalıcı ayarlar, SD kart, Wi-Fi, Firebase (docs/firebase.md),
// Bluetooth (docs/bluetooth.md), güncelleme, dokunmatik. Arayüz src/ui, src/os ve src/apps'tedir.
// Ağ işleri ayrı bir görevde (çekirdek 0) çalışır; ekran ve dokunmatik ana döngüde (çekirdek 1).

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

#define STR_(x) #x
#define STR(x) STR_(x)
// SD karttan güncellemede dosyanın hangi sürüm olduğunu anlamak için yazılıma gömülen işaret.
static const char FW_MARKER[] = "CSFW:" FW_VARIANT ":" STR(FW_BUILD) ";";

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
  cfg.coverTheme = prefs.getBool("cover2", false);
  cfg.bright = constrain(prefs.getInt("bri", 255), 16, 255);
  cfg.nightDim = prefs.getBool("ndim", true);
  cfg.autoBright = prefs.getBool("abri", false);
  cfg.ldrInv = prefs.getBool("ldrinv", false);
  cfg.wCity = prefs.getString("wcity", WEATHER_CITY);
  cfg.wLat = prefs.getFloat("wlat", atof(WEATHER_LAT));
  cfg.wLon = prefs.getFloat("wlon", atof(WEATHER_LON));
  cfg.worldMask = prefs.getUShort("wmask", cfg.worldMask);
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
  prefs.putBool("cover2", cfg.coverTheme);
  prefs.putInt("bri", cfg.bright);
  prefs.putBool("ndim", cfg.nightDim);
  prefs.putBool("abri", cfg.autoBright);
  prefs.putBool("ldrinv", cfg.ldrInv);
  prefs.putString("wcity", cfg.wCity);
  prefs.putFloat("wlat", cfg.wLat);
  prefs.putFloat("wlon", cfg.wLon);
  prefs.putUShort("wmask", cfg.worldMask);
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
// Şifresiz HTTP ile alınır (Open-Meteo destekliyor): şifreli bağlantının ~40 KB belleği Bluetooth
// açıkken yetmiyordu. Şimdiki durum, 24 saatlik ve 7 günlük tahmin tek istekte gelir.
static int weekdayOf(const char* ymd) {
  struct tm t = {};
  t.tm_year = atoi(ymd) - 1900;
  t.tm_mon = atoi(ymd + 5) - 1;
  t.tm_mday = atoi(ymd + 8);
  t.tm_hour = 12;
  mktime(&t);
  return t.tm_wday;
}

static String httpGetPlain(const String& url, int& code) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  String resp;
  code = -1;
  if (http.begin(client, url)) {
    http.setUserAgent("daxen-vane/" + String(FW_BUILD));
    code = http.GET();
    if (code == 200) resp = http.getString();
    http.end();
  }
  if (code != 200 && url.startsWith("http://")) resp = httpsRequest("GET", "https://" + url.substring(7), "", code);
  return resp;
}

static void fetchWeather() {
  String url;
  {
    Lock l;
    url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.wLat, 3) + "&longitude=" + String(cfg.wLon, 3) +
          "&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,weather_code,is_day"
          "&hourly=temperature_2m,weather_code,precipitation_probability&forecast_hours=24"
          "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&forecast_days=7&timezone=auto";
  }
  int code;
  String resp = httpGetPlain(url, code);
  weatherHttp = code;
  if (code != 200) return;
  JsonDocument d;
  if (deserializeJson(d, resp)) {
    weatherHttp = -3;
    return;
  }
  resp = String();
  Weather w;
  JsonVariantConst c = d["current"];
  w.valid = !c["temperature_2m"].isNull();
  w.temp = c["temperature_2m"] | 0.0f;
  w.feels = c["apparent_temperature"] | w.temp;
  w.hum = c["relative_humidity_2m"] | 0;
  w.wind = c["wind_speed_10m"] | 0.0f;
  w.code = c["weather_code"] | 0;
  w.isDay = (c["is_day"] | 1) != 0;
  JsonArrayConst ht = d["hourly"]["time"];
  w.hours = std::min<int>(24, ht.size());
  if (w.hours) w.hHour0 = atoi(ht[0].as<const char*>() + 11);
  for (int i = 0; i < w.hours; i++) {
    w.hTemp[i] = (int8_t)lroundf(d["hourly"]["temperature_2m"][i] | 0.0f);
    w.hCode[i] = d["hourly"]["weather_code"][i] | 0;
    w.hRain[i] = d["hourly"]["precipitation_probability"][i] | 0;
  }
  JsonArrayConst dt = d["daily"]["time"];
  w.days = std::min<int>(7, dt.size());
  for (int i = 0; i < w.days; i++) {
    w.dWday[i] = weekdayOf(dt[i].as<const char*>());
    w.dMax[i] = (int8_t)lroundf(d["daily"]["temperature_2m_max"][i] | 0.0f);
    w.dMin[i] = (int8_t)lroundf(d["daily"]["temperature_2m_min"][i] | 0.0f);
    w.dCode[i] = d["daily"]["weather_code"][i] | 0;
    w.dRain[i] = d["daily"]["precipitation_probability_max"][i] | 0;
  }
  Lock l;
  weather = w;
  weatherAt = millis();
  dataDirty = true;
}

// --- Kablosuz güncelleme (GitHub Releases: fw-vN etiketleri) ---
static void setOtaMsg(const String& m) {
  Lock l;
  otaMsg = m;
  dataDirty = true;
}

static bool otaInSetup = false;  // açılıştaki denetimde ilerleme doğrudan çizilir

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
    if (otaInSetup) uiOtaProgress();
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

// --- Wi-Fi: kayıtlı ağlar (NVS "wifi"), tarama, bağlanma ---
static WifiCred wifiPending;
static volatile bool wifiManualOff = false;  // kullanıcı bağlantıyı kesti: kendiliğinden bağlanma

static void wifiKnownLoad() {
  Lock l;
  wifiKnown.clear();
  prefs.begin("wifi", true);
  int n = prefs.getInt("n", 0);
  for (int i = 0; i < n && i < 5; i++) {
    WifiCred c;
    c.ssid = prefs.getString(("s" + String(i)).c_str(), "");
    c.pass = prefs.getString(("p" + String(i)).c_str(), "");
    if (c.ssid.length()) wifiKnown.push_back(c);
  }
  prefs.end();
}

static void wifiKnownSave() {
  Lock l;
  prefs.begin("wifi", false);
  prefs.putInt("n", wifiKnown.size());
  for (size_t i = 0; i < wifiKnown.size(); i++) {
    prefs.putString(("s" + String(i)).c_str(), wifiKnown[i].ssid);
    prefs.putString(("p" + String(i)).c_str(), wifiKnown[i].pass);
  }
  prefs.end();
}

// Başarılı bağlantı listenin başına alınır (en fazla 5 ağ).
static void wifiRemember(const String& ssid, const String& pass) {
  {
    Lock l;
    wifiKnown.erase(std::remove_if(wifiKnown.begin(), wifiKnown.end(), [&](const WifiCred& c) { return c.ssid == ssid; }), wifiKnown.end());
    wifiKnown.insert(wifiKnown.begin(), WifiCred{ssid, pass});
    if (wifiKnown.size() > 5) wifiKnown.resize(5);
    for (auto& n : wifiNets) n.known = n.known || n.ssid == ssid;
  }
  wifiKnownSave();
}

static void wifiDoScan() {
  wifiScanBusy = true;
  int n = WiFi.scanNetworks(false, false);
  if (n < 0) {
    // Zayıf sinyalde kopan bağlantı sürekli yeniden denenir; ESP32 bağlanmaya çalışırken tarama
    // yapamaz (liste boş kalırdı). Deneme durdurulur ve yeniden taranır; ağ görevi 30 sn sonra
    // görünen kayıtlı ağların en güçlüsüne kendisi bağlanır.
    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(400));
    n = WiFi.scanNetworks(false, false);
  }
  std::vector<WifiNet> list;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    int rssi = WiFi.RSSI(i);
    bool dup = false;
    for (auto& w : list)
      if (w.ssid == s) {
        w.rssi = std::max(w.rssi, rssi);
        dup = true;
      }
    if (dup) continue;
    WifiNet w;
    w.ssid = s;
    w.rssi = rssi;
    w.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    list.push_back(w);
  }
  WiFi.scanDelete();
  std::sort(list.begin(), list.end(), [](const WifiNet& a, const WifiNet& b) { return a.rssi > b.rssi; });
  {
    Lock l;
    for (auto& w : list)
      for (auto& k : wifiKnown)
        if (k.ssid == w.ssid) w.known = true;
    wifiNets = list;
  }
  wifiScanAt = millis();
  wifiScanBusy = false;
}

// Bağlantı koptuysa görünen kayıtlı ağların en güçlüsüne bağlanır.
static void wifiAutoPick() {
  wifiDoScan();
  String ssid, pass;
  {
    Lock l;
    for (auto& w : wifiNets) {
      if (!w.known) continue;
      for (auto& k : wifiKnown)
        if (k.ssid == w.ssid) {
          ssid = k.ssid;
          pass = k.pass;
        }
      if (ssid.length()) break;
    }
  }
  if (!ssid.length()) return;
  WiFi.disconnect(false);
  vTaskDelay(pdMS_TO_TICKS(100));
  if (pass.length()) WiFi.begin(ssid.c_str(), pass.c_str());
  else WiFi.begin(ssid.c_str());
}

static void netTask(void*) {
  uint32_t lastView = 0, lastStatus = 0, nextWeather = 0, lastReconnect = 0, lastTimerPoll = 0;
  uint32_t downSince = 0, lastAuto = 0;
  bool viewOnce = false, statusOnce = false, wasConn = false;
  for (;;) {
    netBusy = false;
    vTaskDelay(pdMS_TO_TICKS(live.c ? 80 : 250));
    if (wifiScanReq) {
      wifiScanReq = false;
      wifiDoScan();
    }
    if (wifiConnReq) {
      wifiConnReq = false;
      String s, p;
      {
        Lock l;
        s = wifiConnSsid;
        p = wifiConnPass;
        wifiLastError = "";
      }
      liveClose();
      wifiManualOff = false;
      WiFi.disconnect(false);
      vTaskDelay(pdMS_TO_TICKS(150));
      if (p.length()) WiFi.begin(s.c_str(), p.c_str());
      else WiFi.begin(s.c_str());
      wifiPending = WifiCred{s, p};
      wifiConnStart = millis();
    }
    bool conn = WiFi.status() == WL_CONNECTED;
    if (wifiConnStart) {
      if (conn && WiFi.SSID() == wifiPending.ssid) {
        wifiRemember(wifiPending.ssid, wifiPending.pass);
        wifiConnStart = 0;
        wantWeather = true;
      } else if (millis() - wifiConnStart > 20000) {
        wl_status_t st = WiFi.status();
        {
          Lock l;
          wifiLastError = st == WL_NO_SSID_AVAIL ? "Ağ bulunamadı" : "Bağlanılamadı: şifre yanlış olabilir";
        }
        wifiConnStart = 0;
      }
    }
    if (conn != wasConn) {
      wasConn = conn;
      dataDirty = true;
    }
    if (portalActive) {
      liveClose();
      continue;
    }
    netBusy = true;
    if (WiFi.status() != WL_CONNECTED) {
      liveClose();
      if (bleLinked) setNet("Telefon üzerinden (Bluetooth)", true);
      else setNet("Wi-Fi bağlantısı yok", false);
      if (!downSince) downSince = millis();
      if (!wifiManualOff && !wifiConnStart && millis() - lastReconnect > 15000) {
        lastReconnect = millis();
        bool many;
        {
          Lock l;
          many = !wifiKnown.empty();
        }
        // Uzun süredir bağlanamıyorsa kayıtlı ağlar arasından görüneni seç
        if (many && millis() - downSince > 30000 && millis() - lastAuto > 90000) {
          lastAuto = millis();
          wifiAutoPick();
        } else {
          WiFi.reconnect();
        }
      }
      continue;
    }
    downSince = 0;
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
    if (wantWeather) {
      wantWeather = false;
      nextWeather = now;
    }
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
    // Güncelleme denetimi açılışta, Bluetooth başlamadan yapılır (bootOtaCheck): Bluetooth açıkken
    // şifreli bağlantıya bellek yetmiyor. Kart her gece 04:00'te kendini yeniden başlatıp denetler.
  }
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
    uiOtaProgress();
    bool ok = Update.begin(size, U_FLASH);
    if (ok) {
      Update.onProgress([](size_t cur, size_t total) {
        if (total) otaProgress = (int)((uint64_t)cur * 100 / total);
        uiOtaProgress();
      });
      ok = Update.writeStream(f) == size && Update.end(true);
    }
    f.close();
    if (ok) {
      sdRenameDone(name, ".yuklendi");
      otaProgress = 100;
      uiOtaProgress();
      delay(800);
      ESP.restart();
    }
    Update.abort();
    otaProgress = -1;
    sdRenameDone(name, ".bad");
    fillBg(0, 0, SCR_W, SCR_H);
    text("SD'den güncelleme olmadı", 160, 100, F_MD, C_TEXT, C_BG, MC_DATUM);
    text("Dosya bozuk olabilir; eski sürümle devam", 160, 128, F_SM, C_TEXT2, C_BG, MC_DATUM);
    delay(3000);
    return;
  }
}


// ============================================================================
// Olaylar
// ============================================================================
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
    publishTimer();
    uiTimerChanged();
    uiToast("1 dakikadan kısa süreler kaydedilmez");
    return;
  }
  if (!timeOk()) {
    uiToast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  enqueueEvent("session", swItemId, sec, swDers.length() ? swDers : String("Diğer"), swKonu, swDk);
  resetTimer();
  publishTimer();
  uiTimerChanged();
  uiToast(fmtMin(sec / 60) + " kaydedildi");
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
    uiTimerChanged();
    if (r.by == "phone") uiToast("Kronometre telefonda kapatıldı");
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
  uiTimerChanged();
  if (!sameItem && r.by == "phone") uiToast(swRunning ? "Kronometre telefonda başlatıldı" : "Kronometre telefondan geldi");
}

static void toggleItem(const Item& it) {
  if (!timeOk()) {
    uiToast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  bool done = it.durum == "tamam";
  if (!done && swItemId == it.id && swElapsedMs() > 0) {
    finishTimer();
    return;
  }
  enqueueEvent(done ? "uncomplete" : "complete", it.id, 0, "", "", it.dk);
  uiDataChanged();
  uiToast(done ? "Geri alındı" : "Tamamlandı");
}

static void postponeItem(const Item& it) {
  if (!timeOk()) {
    uiToast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  if (swItemId == it.id) {
    resetTimer();
    publishTimer();
  }
  enqueueEvent("postpone", it.id, 0, "", "", it.dk);
  uiDataChanged();
  uiToast("Ertelendi");
}

static void selectForTimer(const Item& it) {
  if (swElapsedMs() > 0 && swItemId != it.id) {
    uiToast("Önce çalışan kronometreyi bitir");
    return;
  }
  swItemId = it.id;
  swDers = it.ders;
  swKonu = it.konu;
  swDk = it.dk;
}

static void startItemTimer(const Item& it) {
  if (swElapsedMs() > 0 && swItemId != it.id) {
    uiToast("Önce çalışan kronometreyi bitir");
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
}

// Kronometre: başlat / duraklat (YKS → Sayaç)
static void timerToggleRun() {
  if (swRunning) {
    swAccMs = swElapsedMs();
    swRunning = false;
  } else {
    swStart = millis();
    swRunning = true;
  }
  publishTimer();
  uiTimerChanged();
}

static void timerReset() {
  bool was = swRunning || swElapsedMs() > 0;
  resetTimer();
  if (was) publishTimer();
  uiTimerChanged();
}

static void setPomodoro(bool on) {
  cfg.pomodoro = on;
  pomoCount = swElapsedMs() / 1000 / POMODORO_WORK_SEC;
  pomoBreak = false;
  saveConfig();
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
    uiPaletteChanged();
  }
  dataDirty = true;
}

static void blePairMode() { blePairUntil = millis() + BLE_PAIR_MS; }

static void bleForget() {
  bleTokenSave("");
  if (bleConnected) bleDropAt = millis() + 200;
}

static String bleStatusText() {
  if (bleLinked) return "Bağlı";
  if (blePairUntil && (int32_t)(blePairUntil - millis()) > 0) return "Eşleşme kipi";
  if (!bleHasToken) return "Eşleşmedi";
  return "Bekleniyor";
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
      uiToast(paired ? "Telefonla eşleşti" : "Telefon bağlandı");
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
      n.at = millis();
      notifs.erase(std::remove_if(notifs.begin(), notifs.end(), [&](const Notif& x) { return x.id == n.id; }), notifs.end());
      notifs.insert(notifs.begin(), n);
      if (notifs.size() > 8) notifs.pop_back();
      uiNotif(n);
      uiNotifsChanged();
      break;
    }
    case 'X': {
      String id = String((const char*)(d["id"] | ""));
      size_t before = notifs.size();
      notifs.erase(std::remove_if(notifs.begin(), notifs.end(), [&](const Notif& x) { return x.id == id; }), notifs.end());
      if (notifs.size() != before) uiNotifsChanged();
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
      if (accChanged && cfg.coverTheme) uiPaletteChanged();
      else uiMediaChanged(false);
      break;
    }
    case 'U':  // telefon eşleşmeyi kaldırdı
      bleTokenSave("");
      bleOnUnlinked();
      bleDropAt = millis() + 300;
      uiToast("Telefonla eşleşme kaldırıldı");
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
      if (media.art == artId) uiArtChanged();
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
    uiDataChanged();
  }
}

static void bleMediaKey(const char* cmd) {
  JsonDocument d;
  d["cmd"] = cmd;
  bleSendJson('K', d);
}

#include "core/touch.h"

static void runCalibration() {
  const int px[3] = {20, 300, 20}, py[3] = {20, 20, 220};
  int rx[3], ry[3];
  for (int i = 0; i < 3; i++) {
    fillBg(0, 0, SCR_W, SCR_H);
    text("Dokunmatik ayarı", 160, 92, F_MD, C_TEXT, C_BG, MC_DATUM);
    text("Artının ortasına dokun (" + String(i + 1) + "/3)", 160, 120, F_SM, C_TEXT2, C_BG, MC_DATUM);
    text("Atlamak için BOOT tuşuna bas", 160, 144, F_XS, C_TEXT3, C_BG, MC_DATUM);
    tft.drawFastHLine(px[i] - 12, py[i], 25, C_ACC);
    tft.drawFastVLine(px[i], py[i] - 12, 25, C_ACC);
    tft.drawCircle(px[i], py[i], 8, C_TEXT2);
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

// ============================================================================
// Web sunucusu: telefondan tarayıcıyla SD karta dosya gönderme ve hesap bilgileri
// ============================================================================
// Yalnızca Dosya aktarımı ekranı (ya da Ayarlar → Bulut hesabı → tarayıcıdan gir) açıkken çalışır.
// Aynı Wi-Fi ağındaki telefon http://<kartın adresi>/ adresini açar.
//
// Sunucu çekirdekte ayrı bir süreçtir ("web"): büyük bir dosya gelirken arayüz donmaz. Sayfa
// dosyaları JavaScript ile tek tek ham PUT olarak gönderir (1,4 KB'lık parçalar, kartta 4 KB
// tamponla SD'ye yazılır); çok parçalı form yalnızca JavaScript yoksa kullanılır. Gelen dosya
// bitene kadar ".part" adıyla yazılır, yarım dosya asıl adıyla görünmez.
static WebServer* web = nullptr;
static String webMsg;                // son işlem (ekranda gösterilir)        mtx
static String webUpName;             // yüklenen dosya                         mtx
static volatile uint32_t webUpBytes = 0, webUpTotal = 0;
static volatile int webUploads = 0;
static volatile bool webUpOk = false;
static File webUpFile;
static String webUpPath;
static uint8_t* webBuf = nullptr;
static size_t webBufN = 0;
static const size_t WEB_BUF = 4096;
static int webPid = -1;
static Proc* volatile webProcP = nullptr;

static const char WEB_HEAD[] PROGMEM = R"(<!doctype html><html lang=tr><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>DAXEN VANE</title><style>
body{margin:0;background:#050505;color:#C5C9CE;font:15px/1.45 system-ui,sans-serif}
main{max-width:640px;margin:auto;padding:18px}h1{font:600 13px system-ui;letter-spacing:.3em;color:#858B91;margin:0 0 16px}
h2{font-size:17px;margin:20px 0 8px}a{color:#C5C9CE;text-decoration:none}
.row{display:flex;align-items:center;gap:10px;padding:10px 12px;border-bottom:1px solid #30353A}
.row span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.mut{color:#858B91;font-size:13px}
.box{background:#151515;border:1px solid #30353A;border-radius:4px;padding:12px;margin:12px 0}
input,button{font:inherit;background:#101010;color:#C5C9CE;border:1px solid #30353A;border-radius:3px;padding:8px 10px}
button{border-color:#7892A5;cursor:pointer}input[type=text],input[type=password]{width:100%;box-sizing:border-box;margin:4px 0 10px}
.del{border:none;background:none;color:#555B60;padding:4px}form.inl{display:inline}.path{color:#7892A5}
</style></head><body><main><h1>DAXEN VANE</h1>)";

static String webEsc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String webUrlEnc(const String& s) {
  String o;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/' || c == '~') o += (char)c;
    else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

static String webSafePath(String p) {
  if (!p.startsWith("/")) p = "/" + p;
  if (p.indexOf("..") >= 0) p = "/";
  while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  return p;
}

static void webPage() {
  if (!sdOk) {
    web->send(200, "text/html; charset=utf-8", String(FPSTR(WEB_HEAD)) + "<p>SD kart takılı değil.</p></main></body></html>");
    return;
  }
  String dir = webSafePath(web->hasArg("d") ? web->arg("d") : String("/"));
  web->setContentLength(CONTENT_LENGTH_UNKNOWN);
  web->send(200, "text/html; charset=utf-8", "");
  web->sendContent(FPSTR(WEB_HEAD));
  String h = "<div class=box><form id=fm method=post enctype=multipart/form-data action='/up?d=" + webUrlEnc(dir) +
             "'><b>Dosya gönder</b><p class=mut>Şarkılar için Muzik klasörünü seç. Birden çok dosya seçebilirsin.</p>"
             "<input type=file name=f id=fi multiple> <button id=gb>Gönder</button>"
             "<div id=pt class=mut style='margin-top:8px'></div><div style='height:3px;background:#30353A;margin-top:6px'>"
             "<div id=pb style='height:3px;width:0;background:#7892A5'></div></div></form></div>"
             "<script>fm.onsubmit=async e=>{e.preventDefault();const fs=[...fi.files];if(!fs.length)return;gb.disabled=true;"
             "for(let i=0;i<fs.length;i++){const x=fs[i];const ok=await new Promise(r=>{const q=new XMLHttpRequest();"
             "q.open('PUT','/put?d=" + webUrlEnc(dir) + "&n='+encodeURIComponent(x.name));"
             "q.setRequestHeader('Content-Type','application/octet-stream');"
             "q.upload.onprogress=v=>{if(v.lengthComputable){const k=Math.floor(v.loaded*100/v.total);pb.style.width=k+'%';"
             "pt.textContent=(i+1)+' / '+fs.length+' · '+x.name+' · %'+k}};"
             "q.onload=()=>r(q.status==200);q.onerror=()=>r(false);q.send(x)});"
             "if(!ok){pt.textContent='Gönderilemedi: '+x.name+'. Kart bu ekranda ve aynı Wi-Fi ağında mı?';gb.disabled=false;return}}"
             "location.reload()}</script>";
  h += "<h2>Klasör: <span class=path>" + webEsc(dir) + "</span></h2>";
  if (dir != "/") h += "<div class=row><a href='/?d=" + webUrlEnc(dirName(dir)) + "'>&#8592; Üst klasör</a></div>";
  web->sendContent(h);
  File d = SD.open(dir);
  std::vector<std::pair<String, uint32_t>> dirs, files;
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f && dirs.size() + files.size() < 500; f = d.openNextFile()) {
      String n = baseName(f.name());
      if (!n.startsWith(".")) (f.isDirectory() ? dirs : files).push_back({n, (uint32_t)f.size()});
      f.close();
    }
    d.close();
  }
  auto byName = [](const std::pair<String, uint32_t>& a, const std::pair<String, uint32_t>& b) { return trLower(a.first) < trLower(b.first); };
  std::sort(dirs.begin(), dirs.end(), byName);
  std::sort(files.begin(), files.end(), byName);
  for (auto& e : dirs) {
    String p = joinPath(dir, e.first);
    web->sendContent("<div class=row>&#128193;<span><a href='/?d=" + webUrlEnc(p) + "'>" + webEsc(e.first) +
                     "</a></span><form class=inl method=post action='/del?f=" + webUrlEnc(p) +
                     "' onsubmit='return confirm(\"Klasör ve içindekiler silinsin mi?\")'><button class=del>Sil</button></form></div>");
  }
  for (auto& e : files) {
    String p = joinPath(dir, e.first);
    web->sendContent("<div class=row><span><a href='/dl?f=" + webUrlEnc(p) + "'>" + webEsc(e.first) + "</a></span><span class=mut style='flex:none'>" +
                     fmtBytes(e.second) + "</span><form class=inl method=post action='/del?f=" + webUrlEnc(p) +
                     "' onsubmit='return confirm(\"Silinsin mi?\")'><button class=del>Sil</button></form></div>");
  }
  if (dirs.empty() && files.empty()) web->sendContent("<p class=mut>Klasör boş.</p>");
  web->sendContent("<div class=box><form method=post action='/mkdir?d=" + webUrlEnc(dir) +
                   "'><b>Yeni klasör</b><input type=text name=n placeholder='Klasör adı'><button>Oluştur</button></form></div>"
                   "<p class=mut><a href='/hesap'>Bulut hesabı bilgileri &#8594;</a></p></main></body></html>");
  web->sendContent("");
}

static void webRedirect(const String& to) {
  web->sendHeader("Location", to);
  web->send(303, "text/plain", "");
}

static void webSetMsg(const String& m) {
  Lock l;
  webMsg = m;
}

static void webFlush() {
  if (webUpFile && webBufN) webUpFile.write(webBuf, webBufN);
  webBufN = 0;
}

static void webBeginFile(const String& dir, const String& rawName, uint32_t total) {
  String name = safeName(baseName(rawName));
  if (!name.length()) name = "dosya";
  {
    Lock l;
    webUpName = name;
  }
  webUpBytes = 0;
  webUpTotal = total;
  webUpOk = false;
  webUpPath = joinPath(dir, name);
  String part = webUpPath + ".part";
  if (SD.exists(part)) SD.remove(part);
  webUpFile = SD.open(part, FILE_WRITE);
  if (!webBuf) webBuf = (uint8_t*)malloc(WEB_BUF);
  webBufN = 0;
  webSetMsg(webUpFile ? "Alınıyor: " + name : "Yazılamadı: " + name);
}

static void webWrite(const uint8_t* d, size_t n) {
  webUpBytes += n;
  if (!webUpFile) return;
  if (!webBuf) {
    webUpFile.write(d, n);
    return;
  }
  while (n) {
    size_t k = std::min(n, WEB_BUF - webBufN);
    memcpy(webBuf + webBufN, d, k);
    webBufN += k;
    d += k;
    n -= k;
    if (webBufN == WEB_BUF) webFlush();
  }
}

static void webEndFile(bool ok) {
  webFlush();
  bool had = (bool)webUpFile;
  if (webUpFile) webUpFile.close();
  String part = webUpPath + ".part", name;
  {
    Lock l;
    name = webUpName;
  }
  if (ok && had) {
    if (SD.exists(webUpPath)) SD.remove(webUpPath);
    ok = SD.rename(part, webUpPath);
  } else {
    ok = false;
    if (SD.exists(part)) SD.remove(part);
  }
  if (ok) {
    webUploads++;
    webSetMsg("Alındı: " + name + " (" + fmtBytes(webUpBytes) + ")");
  } else {
    webSetMsg((had ? "Yarıda kesildi: " : "Yazılamadı: ") + name);
  }
  webUpOk = ok;
  {
    Lock l;
    webUpName = "";
  }
  dataDirty = true;
}

// Çok parçalı form (JavaScript'siz yedek yol)
static void webUploadChunk() {
  HTTPUpload& up = web->upload();
  if (up.status == UPLOAD_FILE_START) {
    webBeginFile(webSafePath(web->hasArg("d") ? web->arg("d") : String("/")), up.filename, web->header("Content-Length").toInt());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    webWrite(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    webEndFile(up.status == UPLOAD_FILE_END);
  }
}

// Ham PUT: sayfadaki JavaScript her dosyayı böyle gönderir (hızlı yol)
static void webRawChunk() {
  HTTPRaw& r = web->raw();
  if (r.status == RAW_START) {
    webBeginFile(webSafePath(web->arg("d")), web->arg("n"), web->clientContentLength());
  } else if (r.status == RAW_WRITE) {
    webWrite(r.buf, r.currentSize);
    if (webProcP && webProcP->killReq) {  // ekrandan çıkıldı: bağlantı kapatılır, yükleme yarıda kesilir
      int fd = web->client().fd();
      if (fd >= 0) lwip_shutdown(fd, SHUT_RDWR);
    }
  } else {
    webEndFile(r.status == RAW_END);
  }
}

static bool fRemoveAll(const String& path);  // apps/files.h

static void webAccountPage() {
  String h = String(FPSTR(WEB_HEAD)) + "<h2>Bulut hesabı</h2><p class=mut>Telefondaki uygulamaya girdiğin Firebase bilgilerinin aynısı.</p>"
             "<form method=post action='/hesap' class=box>Veritabanı adresi<input type=text name=db value='" + webEsc(cfg.dbUrl) +
             "'>Web API anahtarı<input type=text name=key value='" + webEsc(cfg.apiKey) + "'>E-posta<input type=text name=mail value='" +
             webEsc(cfg.email) + "'>Şifre" + (cfg.password.length() ? " (değişmeyecekse boş bırak)" : "") +
             "<input type=password name=pass><button>Kaydet</button></form><p><a href='/'>&#8592; Dosyalar</a></p></main></body></html>";
  web->send(200, "text/html; charset=utf-8", h);
}

// Sunucu süreci: istekleri işler; uzun bir yükleme sürerken yalnızca bu süreç meşguldür
static void webProc(Proc* p) {
  webProcP = p;
  while (!p->killReq) {
    if (WiFi.status() == WL_CONNECTED) {
      uint32_t t0 = micros();
      web->handleClient();
      kAddBusy(p, micros() - t0);
    }
    vTaskDelay(pdMS_TO_TICKS(4));
  }
  webProcP = nullptr;
}

static bool webStart() {
  if (web) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  web = new (std::nothrow) WebServer(80);
  if (!web) return false;
  const char* hdrs[] = {"Content-Length"};
  web->collectHeaders(hdrs, 1);
  web->on("/", HTTP_GET, webPage);
  web->on("/dl", HTTP_GET, []() {
    String p = webSafePath(web->arg("f"));
    File f = SD.open(p);
    if (!f || f.isDirectory()) {
      web->send(404, "text/plain", "yok");
      return;
    }
    web->sendHeader("Content-Disposition", "attachment; filename=\"" + baseName(p) + "\"");
    web->streamFile(f, "application/octet-stream");
    f.close();
  });
  web->on("/up", HTTP_POST, []() { webRedirect("/?d=" + webUrlEnc(webSafePath(web->arg("d")))); }, webUploadChunk);
  web->on("/put", HTTP_PUT, []() { web->send(webUpOk ? 200 : 500, "text/plain", webUpOk ? "tamam" : "hata"); }, webRawChunk);
  web->on("/del", HTTP_POST, []() {
    String p = webSafePath(web->arg("f"));
    if (p != "/") {
      fRemoveAll(p);
      webSetMsg("Silindi: " + baseName(p));
      dataDirty = true;
    }
    webRedirect("/?d=" + webUrlEnc(dirName(p)));
  });
  web->on("/mkdir", HTTP_POST, []() {
    String d = webSafePath(web->arg("d"));
    String n = safeName(web->arg("n"));
    if (n.length()) SD.mkdir(joinPath(d, n));
    webRedirect("/?d=" + webUrlEnc(d));
  });
  web->on("/hesap", HTTP_GET, webAccountPage);
  web->on("/hesap", HTTP_POST, []() {
    String db = normalizeDbUrl(web->arg("db")), key = web->arg("key"), mail = web->arg("mail"), pass = web->arg("pass");
    key.trim();
    mail.trim();
    accountSet(db.length() ? db : cfg.dbUrl, key.length() ? key : cfg.apiKey, mail.length() ? mail : cfg.email,
               pass.length() ? pass : cfg.password);
    webSetMsg("Hesap bilgileri kaydedildi");
    dataDirty = true;
    web->send(200, "text/html; charset=utf-8", String(FPSTR(WEB_HEAD)) + "<p>Kaydedildi. Kart buluta bağlanmayı deniyor.</p><p><a href='/'>Dosyalar</a></p></main></body></html>");
  });
  web->onNotFound([]() { web->send(404, "text/plain", "yok"); });
  web->begin();
  webSetMsg("");
  webPid = kSpawn("web", webProc, 10 * 1024, false, K_PROC_PRIO, 1);
  if (webPid < 0) {  // süreç açılamadı (bellek)
    web->stop();
    delete web;
    web = nullptr;
    return false;
  }
  return true;
}

static void webStop() {
  if (!web) return;
  kStop(webPid);
  if (!kWait(webPid, 8000)) return;  // süreç hâlâ sunucuyu kullanıyor: silinmez (bir dahaki çağrıda)
  if (webUpFile) webEndFile(false);
  web->stop();
  delete web;
  web = nullptr;
  webPid = -1;
}

static bool webRunning() { return web != nullptr; }

static String webStatus() {
  Lock l;
  if (webUpName.length()) {
    String s = webUpName + " · " + fmtBytes(webUpBytes);
    if (webUpTotal > webUpBytes) s += " / " + fmtBytes(webUpTotal);
    return s;
  }
  return webMsg;
}

static void webTick() {}  // sunucu artık kendi sürecinde (webProc)

static int webProgress() {
  {
    Lock l;
    if (!webUpName.length()) return -1;
  }
  if (webUpTotal == 0) return -1;
  return (int)std::min<uint64_t>(100, (uint64_t)webUpBytes * 100 / webUpTotal);
}
static int webUploadCount() { return webUploads; }

// Şehir arama (Hava durumu → ara). Engelleyici; ekranda "Aranıyor…" görünür.
static bool weatherSearch(const String& q, std::vector<GeoHit>& out) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String name = q;
  name.trim();
  int code;
  String resp = httpGetPlain("http://geocoding-api.open-meteo.com/v1/search?count=6&language=tr&format=json&name=" + webUrlEnc(name), code);
  if (code != 200) return false;
  JsonDocument d;
  if (deserializeJson(d, resp)) return false;
  for (JsonVariantConst r : d["results"].as<JsonArrayConst>()) {
    GeoHit h;
    h.name = String((const char*)(r["name"] | ""));
    String a1 = String((const char*)(r["admin1"] | "")), c = String((const char*)(r["country"] | ""));
    h.region = a1.length() && a1 != h.name ? a1 + ", " + c : c;
    h.lat = r["latitude"] | 0.0f;
    h.lon = r["longitude"] | 0.0f;
    if (h.name.length()) out.push_back(h);
  }
  return true;
}

static void splashLogo();
// ============================================================================
// Müzik yazılımı (kulaklıkla SD müzik): indirme, bölüme yazma, açma
// ============================================================================
// Müzik ayrı bir yazılımdır (klasik Bluetooth ve MP3 çözücü bu yazılıma sığmıyor). SD karttaki
// /daxen/sys/muzik-<model>.bin boştaki güncelleme bölümüne yazılır ve kart oradan başlar. Müzik
// yazılımından çıkınca ya da bir sorun olursa kart kendiliğinden bu yazılıma döner.
#define MUSIC_DIR "/daxen/sys"
#define MUSIC_FILE MUSIC_DIR "/muzik-" FW_VARIANT ".bin"
#define MUSIC_MIN_BUILD 28  // hoparlör çıkışı bu sürümle geldi; daha eski modül yenisiyle değiştirilir
static String musicErr;
static int musicCachedState = -1;
static uint32_t musicCachedSize = 0;

// Dosyadaki "DXMU:<model>:<sürüm>;" işaretini arar
static bool musicReadMarker(File& f, String& variant, int& build) {
  static const char key[] = "DXMU:";
  uint8_t buf[512];
  size_t kl = 5, match = 0;
  String tail;
  bool inMarker = false;
  f.seek(0);
  while (f.available()) {
    int n = f.read(buf, sizeof buf);
    for (int i = 0; i < n; i++) {
      char c = buf[i];
      if (inMarker) {
        if (c == ';') {
          int k = tail.indexOf(':');
          if (k > 0) {
            variant = tail.substring(0, k);
            build = tail.substring(k + 1).toInt();
            return true;
          }
          inMarker = false;
          tail = "";
        } else if (tail.length() < 40 && c >= 32 && c < 127) {
          tail += c;
        } else {
          inMarker = false;
          tail = "";
        }
        continue;
      }
      if (c == key[match]) {
        if (++match == kl) {
          inMarker = true;
          match = 0;
        }
      } else {
        match = c == key[0] ? 1 : 0;
      }
    }
  }
  return false;
}

// Kökteki muzik-<model>.bin (elle kopyalanmış) sistem klasörüne taşınır
static void musicAdoptRootFile() {
  const char* root = "/muzik-" FW_VARIANT ".bin";
  if (!sdOk || webUpFile || !SD.exists(root)) return;
  if (!SD.exists("/daxen")) SD.mkdir("/daxen");
  if (!SD.exists(MUSIC_DIR)) SD.mkdir(MUSIC_DIR);
  if (SD.exists(MUSIC_FILE)) SD.remove(MUSIC_FILE);
  SD.rename(root, MUSIC_FILE);
}

static int musicState() {
  if (!sdOk) return 2;
  musicAdoptRootFile();
  File f = SD.open(MUSIC_FILE);
  if (!f) return 1;
  uint32_t sz = f.size();
  if (musicCachedState >= 0 && sz == musicCachedSize) {
    f.close();
    return musicCachedState;
  }
  String v;
  int b = 0;
  bool okMarker = musicReadMarker(f, v, b) && v == FW_VARIANT;
  f.close();
  musicCachedSize = sz;
  musicCachedState = !okMarker ? 1 : (b >= MUSIC_MIN_BUILD ? 0 : 3);  // 3: eski modül
  return musicCachedState;
}

static String musicInfo() {
  if (musicErr.length()) return musicErr;
  switch (musicState()) {
    case 0: return "Müzik modülü hazır";
    case 2: return "SD kart takılı değil";
    case 3: return "Müzik modülü eski: yenisi indirilecek (1,3 MB)";
    default: return "Müzik modülü SD kartta yok (1,3 MB indirilecek)";
  }
}

static void musicDownload() {
  {
    Lock l;
    prefs.begin("cs", false);
    prefs.putBool("getmus", true);
    prefs.end();
  }
  uiSplash("Müzik modülü indirilecek…");
  delay(600);
  esp_ota_mark_app_valid_cancel_rollback();
  ESP.restart();
}

static void musicProgress(const String& title, int pct) {
  static int last = -2;
  if (pct < 0 || last < 0 || pct < last) {
    splashLogo();
    text(title, 160, 172, F_SM, C_TEXT, C_BG, MC_DATUM);
    text("Kapatma", 160, 222, F_XS, C_TEXT3, C_BG, MC_DATUM);
  }
  last = pct;
  progress(60, 190, 200, 2, std::max(0, pct), C_ACC);
  fillBg(60, 198, 200, 16);
  if (pct >= 0) text("%" + String(pct), 160, 205, F_XS, C_TEXT2, C_BG, MC_DATUM);
}

// Müzik yazılımını boştaki bölüme yazar (gerekirse) ve oradan başlar.
static void musicLaunch() {
  if (musicState() != 0) return;
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next) {
    musicErr = "Güncelleme bölümü bulunamadı";
    return;
  }
  esp_ota_mark_app_valid_cancel_rollback();  // dönüş için bu yazılım "sağlam" olmalı
  uint8_t sha[32], saved[32];
  size_t n;
  String lbl;
  {
    Lock l;
    prefs.begin("mus2", true);
    n = prefs.getBytes("sha", saved, 32);
    lbl = prefs.getString("lbl", "");
    prefs.end();
  }
  bool same = n == 32 && lbl == next->label && esp_partition_get_sha256(next, sha) == ESP_OK && !memcmp(sha, saved, 32);
  if (same) {
    musicProgress("Müzik açılıyor", 100);
    if (esp_ota_set_boot_partition(next) == ESP_OK) {
      delay(200);
      ESP.restart();
    }
  }
  File f = SD.open(MUSIC_FILE);
  if (!f) {
    musicErr = "Müzik dosyası açılamadı";
    return;
  }
  size_t size = f.size();
  musicProgress("Müzik modülü yükleniyor", 0);
  if (!Update.begin(size)) {
    f.close();
    musicErr = "Yazılamadı: " + String(Update.errorString());
    return;
  }
  uint8_t buf[2048];
  size_t done = 0;
  int lastPct = -1;
  while (done < size) {
    int r = f.read(buf, sizeof buf);
    if (r <= 0 || Update.write(buf, r) != (size_t)r) break;
    done += r;
    int pct = done * 100 / size;
    if (pct != lastPct) {
      lastPct = pct;
      musicProgress("Müzik modülü yükleniyor", pct);
    }
  }
  f.close();
  if (done != size || !Update.end(true)) {
    musicErr = "Yükleme yarıda kaldı: " + String(Update.errorString());
    Update.abort();
    return;
  }
  if (esp_partition_get_sha256(next, sha) == ESP_OK) {
    Lock l;
    prefs.begin("mus2", false);
    prefs.putBytes("sha", sha, 32);
    prefs.putString("lbl", next->label);
    prefs.end();
  }
  delay(200);
  ESP.restart();
}

// Açılışta (Bluetooth başlamadan, bellek bolken): istenmişse modülü indirip açar.
static void musicBootTasks() {
  bool want;
  {
    Lock l;
    prefs.begin("cs", false);
    want = prefs.getBool("getmus", false);
    if (want) prefs.remove("getmus");
    prefs.end();
  }
  if (!want || !sdOk) return;
  // Zayıf sinyalde ilk bağlantı uzun sürebilir: 45 sn beklenir, arada kayıtlı ağlar taranıp
  // görünenlerin en güçlüsü yeniden denenir. Ekranda geçen süre görünür.
  uint32_t t0 = millis(), lastDraw = 0;
  int tries = 0;
  if (WiFi.status() != WL_CONNECTED) musicProgress("Müzik modülü için Wi-Fi bekleniyor", -1);
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 45000) {
    uint32_t el = millis() - t0;
    if ((tries == 0 && el > 10000) || (tries == 1 && el > 26000)) {
      tries++;
      wifiAutoPick();
    }
    if (millis() - lastDraw >= 1000) {
      lastDraw = millis();
      fillBg(60, 198, 200, 16);
      text(String((millis() - t0) / 1000) + " sn", 160, 205, F_XS, C_TEXT2, C_BG, MC_DATUM);
    }
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    int st = WiFi.status();
    musicErr = st == WL_NO_SSID_AVAIL   ? "İndirilemedi: kayıtlı Wi-Fi ağı bulunamadı"
               : st == WL_CONNECT_FAILED ? "İndirilemedi: Wi-Fi şifresi kabul edilmedi"
                                         : "İndirilemedi: Wi-Fi'ye bağlanılamadı (sinyal zayıf)";
    return;
  }
  if (!SD.exists("/daxen")) SD.mkdir("/daxen");
  if (!SD.exists(MUSIC_DIR)) SD.mkdir(MUSIC_DIR);
  String url = "https://github.com/" GITHUB_REPO "/releases/download/fw-v" + String(FW_BUILD) + "/muzik-" FW_VARIANT ".bin";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  musicProgress("Müzik modülü indiriliyor", 0);
  int code = -1;
  if (http.begin(client, url)) {
    http.setUserAgent("daxen-vane");
    code = http.GET();
  }
  if (code != 200) {
    http.end();
    musicErr = "İndirilemedi (" + String(code) + ")";
    return;
  }
  int total = http.getSize();
  const char* tmp = MUSIC_DIR "/muzik.tmp";
  File out = SD.open(tmp, FILE_WRITE);
  WiFiClient* s = http.getStreamPtr();
  uint8_t buf[2048];
  int got = 0, lastPct = -1;
  uint32_t lastData = millis();
  while (http.connected() && (total < 0 || got < total) && millis() - lastData < 20000) {
    size_t a = s->available();
    if (!a) {
      delay(5);
      continue;
    }
    int r = s->readBytes(buf, std::min(a, sizeof buf));
    if (r > 0) {
      out.write(buf, r);
      got += r;
      lastData = millis();
      int pct = total > 0 ? (int)((int64_t)got * 100 / total) : 0;
      if (pct != lastPct) {
        lastPct = pct;
        musicProgress("Müzik modülü indiriliyor", pct);
      }
    }
  }
  out.close();
  http.end();
  if (total > 0 && got != total) {
    SD.remove(tmp);
    musicErr = "İndirme yarıda kaldı";
    return;
  }
  if (SD.exists(MUSIC_FILE)) SD.remove(MUSIC_FILE);
  SD.rename(tmp, MUSIC_FILE);
  musicCachedState = -1;
  musicErr = "";
  if (musicState() != 0) {
    musicErr = "İndirilen dosya bu karta uymuyor";
    return;
  }
  musicLaunch();
}


// ============================================================================
// BOOT tuşu: kısa basış ana ekran, 2 sn Ayarlar, 6 sn Wi-Fi ayarı
// ============================================================================
static void handleBoot() {
  static bool down = false;
  static uint32_t downAt = 0;
  static int stage = 0;
  bool pressed = digitalRead(PIN_BOOT) == LOW;
  if (pressed && !down) {
    down = true;
    downAt = millis();
    stage = 0;
    lastInteract = millis();
  } else if (pressed && down) {
    uint32_t held = millis() - downAt;
    if (stage == 0 && held > 2000) {
      stage = 1;
      sysWake();
      shellBoot(1);
      toast("Bırakma: 6 sn'de Wi-Fi ayarı açılır");
    } else if (stage == 1 && held > 6000) {
      stage = 2;
      shellBoot(2);
    }
  } else if (!pressed && down) {
    down = false;
    if (stage == 0 && millis() - downAt > 40) {
      if (sysWake()) return;
      shellBoot(0);
    }
  }
}

// ============================================================================
// Işıklar ve ekran parlaklığı
// ============================================================================
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(1, 255 - r);  // LED'ler LOW'da yanar
  ledcWrite(2, 255 - g);
  ledcWrite(3, 255 - b);
}

static uint32_t ledOverrideUntil = 0;
static uint8_t ledOv[3];
static void sysLed(uint8_t r, uint8_t g, uint8_t b) {
  ledOv[0] = r;
  ledOv[1] = g;
  ledOv[2] = b;
  ledOverrideUntil = millis() + 400;
}

static void updateLeds() {
  uint32_t ms = millis();
  if (ledOverrideUntil && (int32_t)(ledOverrideUntil - ms) > 0) {
    setLed(ledOv[0], ledOv[1], ledOv[2]);
    return;
  }
  ledOverrideUntil = 0;
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
  else if (!ok && hasCreds() && ms > 90000 && !bleLinked) setLed(25, 0, 0);
  else setLed(0, 0, 0);
}

static bool isNightHour() {
  struct tm t;
  return localNow(t) && (t.tm_hour >= NIGHT_FROM || t.tm_hour < NIGHT_TO);
}

// Işık sensörü (GPIO34). Değer ne kadar yüksekse ortam o kadar karanlık.
static int sysLight() { return analogRead(PIN_LDR); }
static int sysLightPct() {
  int v = constrain(sysLight(), 0, 4095);
  return cfg.ldrInv ? v * 100 / 4095 : 100 - v * 100 / 4095;
}

static int blLevel = -1;
static void sysSetBacklight(int level) {
  level = constrain(level, 4, 255);
  if (level != blLevel) {
    blLevel = level;
    ledcWrite(0, level);
  }
}

// Gece kısma (22:00-07:00, dokununca 30 sn açılır) ve ışığa göre parlaklık
static void updateBacklight() {
  static uint32_t lastLdr = 0;
  static float ldrAvg = -1;
  uint32_t ms = millis();
  int level = cfg.bright;
  if (cfg.autoBright) {
    if (ms - lastLdr > 500 || ldrAvg < 0) {
      lastLdr = ms;
      int v = sysLightPct();
      ldrAvg = ldrAvg < 0 ? v : ldrAvg * 0.85f + v * 0.15f;
    }
    // aydınlıkta seçilen parlaklık, karanlıkta %20'ye kadar iner (sensör az ışıkta da hassas)
    float t = constrain(ldrAvg / 60.f, 0.f, 1.f);
    level = (int)(cfg.bright * (0.2f + 0.8f * t));
  }
  bool dim = cfg.nightDim && isNightHour() && (int32_t)(ms - wakeUntil) > 0;
  blDim = dim;
  if (dim) level = std::min(level, 40);
  sysSetBacklight(level);
}

// Gece kısıkken ilk dokunuş yalnızca ekranı açar.
static bool sysWake() {
  bool wasDim = blDim;
  wakeUntil = millis() + 30000;
  if (wasDim) {
    blDim = false;
    sysSetBacklight(cfg.bright);
  }
  return wasDim;
}

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
static String wifiMac() { return WiFi.macAddress(); }

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
  checkOta(manual);
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
  musicBootTasks();
  bleBegin();

  TaskHandle_t netH = nullptr;
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, &netH, 0);
  lastInteract = millis();
  homeShow();
  if (!haveWifi && !bleHasToken) settingsOpenPage(SP_WIFI);  // ilk kurulum: önce Wi-Fi

  // Çekirdek: zamanlayıcı kesmesi ve servisler (kayıt sırası öncelik sırasıdır)
  kernelBegin();
  kService("dokunma", 10, svcTouch);
  kService("kabuk", 16, shellTick);
  kService("bluetooth", 10, svcBle);
  kService("saat", 200, svcClock);
  kService("ışık", 50, svcLight);
  kAdopt("ağ", netH, 1, 0, 16384);
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
  if (!markedValid && millis() > 45000) {
    markedValid = true;
    esp_ota_mark_app_valid_cancel_rollback();
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
