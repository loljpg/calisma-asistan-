// Çalışma Saati — ESP32 Cheap Yellow Display (ESP32-2432S028R) yazılımı
//
// Ekranlar: Ana (saat, tarih, hava durumu, şimdiki/sıradaki oturum), Program (günün
// listesi, tik), Kronometre (derse bağlı, Pomodoro), Ayarlar.
// Veri: Firebase Realtime Database (docs/firebase.md). Ağ işleri ayrı bir görevde
// (çekirdek 0) çalışır; ekran ve dokunmatik ana döngüde (çekirdek 1).

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
#include <XPT2046_Touchscreen.h>
#include <time.h>
#include <vector>
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

struct Config {
  String dbUrl, apiKey, email, password;
  bool invert = DEFAULT_INVERT;
  bool calibrated = false, swapXY = false;
  int x1 = 400, x2 = 3480, y1 = 520, y2 = 3520;
  bool pomodoro = false;
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

static Preferences prefs;

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

static long long nowMs() { return timeOk() ? (long long)time(nullptr) * 1000LL : 0; }

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

static const char* GUN[] = {"Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi"};
static const char* AY[] = {"Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran", "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};

static String dateLine() {
  struct tm t;
  if (!localNow(t)) return "Saat ayarlanıyor…";
  return String(GUN[t.tm_wday]) + ", " + String(t.tm_mday) + " " + AY[t.tm_mon];
}

static uint16_t dersColor(const String& d) {
  if (d == "Türkçe") return RGB(249, 168, 212);
  if (d == "Edebiyat") return RGB(240, 171, 252);
  if (d == "Matematik") return RGB(125, 211, 252);
  if (d == "Geometri") return RGB(147, 197, 253);
  if (d == "Fizik") return RGB(165, 180, 252);
  if (d == "Kimya") return RGB(110, 231, 183);
  if (d == "Biyoloji") return RGB(134, 239, 172);
  if (d == "Tarih") return RGB(253, 186, 116);
  if (d == "Coğrafya") return RGB(252, 211, 77);
  if (d == "Felsefe") return RGB(196, 181, 253);
  if (d == "Din Kültürü") return RGB(253, 230, 138);
  if (d == "İngilizce") return RGB(103, 232, 249);
  return RGB(203, 213, 225);
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
  String ob = prefs.getString("outbox", "");
  prefs.end();
  outbox.clear();
  int start = 0;
  while (start < (int)ob.length()) {
    int nl = ob.indexOf('\n', start);
    if (nl < 0) nl = ob.length();
    String line = ob.substring(start, nl);
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
  prefs.end();
}

static void saveOutbox() {
  Lock l;
  String s;
  for (auto& e : outbox) {
    s += e;
    s += '\n';
  }
  prefs.begin("cs", false);
  prefs.putString("outbox", s);
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
  View v;
  if (!d.isNull()) {
    v.valid = true;
    v.date = String((const char*)(d["date"] | ""));
    v.studiedMin = d["studiedMin"] | 0;
    v.goalMin = d["goalMin"] | 300;
    v.streak = d["streak"] | 0;
    v.examDays = d["examDays"].isNull() ? -1 : d["examDays"].as<int>();
    v.updatedAt = d["updatedAt"].as<long long>();
    for (JsonObject it : d["items"].as<JsonArray>()) {
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
  {
    Lock l;
    view = v;
  }
  lastViewBody = resp;
  pruneLocalEvents();
  dataDirty = true;
  return true;
}

static void fbPushStatus() {
  JsonDocument d;
  d["lastSeen"][".sv"] = "timestamp";
  d["fw"] = "fw-" + String(FW_BUILD);
  d["variant"] = FW_VARIANT;
  d["ip"] = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  String body;
  serializeJson(d, body);
  int code = 0;
  httpsRequest("PUT", fbUrl("device/status"), body, code);
  if (code == 401) idToken = "";
}

static void fbFlushOutbox() {
  for (;;) {
    String ev;
    {
      Lock l;
      if (outbox.empty()) return;
      ev = outbox.front();
    }
    int code = 0;
    httpsRequest("POST", fbUrl("device/events"), ev, code);
    if (code == 401) {
      idToken = "";
      return;
    }
    if (code != 200) return;
    {
      Lock l;
      if (!outbox.empty() && outbox.front() == ev) outbox.erase(outbox.begin());
    }
    saveOutbox();
    wantView = true;
  }
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
  uint32_t lastView = 0, lastStatus = 0, nextWeather = 0, lastOta = 0, lastReconnect = 0;
  const uint32_t bootAt = millis();
  bool viewOnce = false, statusOnce = false, otaOnce = false;
  for (;;) {
    netBusy = false;
    vTaskDelay(pdMS_TO_TICKS(250));
    if (portalActive) continue;
    netBusy = true;
    if (WiFi.status() != WL_CONNECTED) {
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
    }
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
        fbFlushOutbox();
        if (wantView || !viewOnce || now - lastView > VIEW_EVERY_MS) {
          wantView = false;
          viewOnce = true;
          lastView = now;
          if (fbFetchView()) setNet("Bağlı", true);
        }
        if (!statusOnce || now - lastStatus > STATUS_EVERY_MS) {
          statusOnce = true;
          lastStatus = now;
          fbPushStatus();
        }
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
      checkOta(manual);
    }
  }
}

// ============================================================================
// Ekran ve dokunmatik (çekirdek 1)
// ============================================================================
static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static bool sprOk = false;
static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

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
static void bigText(const String& s, int x, int y, int w, uint16_t fg, uint16_t bg, bool center) {
  if (!sprOk) {
    tft.fillRect(x, y, w, 58, bg);
    text(s, center ? x + w / 2 : x, y, F_BIG, fg, bg, center ? TC_DATUM : TL_DATUM);
    return;
  }
  spr.fillSprite(bg);
  spr.setTextColor(fg, bg);
  spr.setTextDatum(center ? TC_DATUM : TL_DATUM);
  spr.drawString(s, center ? w / 2 : 0, 0);
  spr.pushSprite(x, y, 0, 0, w, 58);
}

// --- Şekiller ---
static void card(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border) {
  tft.fillSmoothRoundRect(x, y, w, h, r, border, COL_BG);
  tft.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, fill, border);
  if (w > 2 * r + 4) tft.drawFastHLine(x + r, y + 1, w - 2 * r, COL_HI);
}

static void progressBar(int x, int y, int w, int h, int pct, uint16_t col, uint16_t bg) {
  pct = constrain(pct, 0, 100);
  tft.fillSmoothRoundRect(x, y, w, h, h / 2, COL_LINE, bg);
  int fw = w * pct / 100;
  if (fw >= h) tft.fillSmoothRoundRect(x, y, fw, h, h / 2, col, COL_LINE);
}

static void checkMark(int cx, int cy, uint16_t col, uint16_t bg) {
  tft.drawWideLine(cx - 5, cy, cx - 1, cy + 4, 2.5, col, bg);
  tft.drawWideLine(cx - 1, cy + 4, cx + 6, cy - 4, 2.5, col, bg);
}

static void drawGear(int cx, int cy, uint16_t col, uint16_t bg) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    tft.fillSmoothCircle(cx + (int)(8 * cos(a)), cy + (int)(8 * sin(a)), 2, col, bg);
  }
  tft.fillSmoothCircle(cx, cy, 7, col, bg);
  tft.fillSmoothCircle(cx, cy, 3, bg, col);
}

static void cloudShape(int cx, int cy, uint16_t col) {
  tft.fillSmoothCircle(cx - 5, cy + 1, 6, col, COL_CARD);
  tft.fillSmoothCircle(cx + 4, cy - 2, 7, col, COL_CARD);
  tft.fillSmoothRoundRect(cx - 12, cy + 1, 24, 8, 4, col, COL_CARD);
}

static void weatherIcon(int cx, int cy, int code) {
  const uint16_t sun = COL_WARN, cloud = RGB(203, 213, 225);
  if (code == 0) {
    for (int i = 0; i < 8; i++) {
      float a = i * PI / 4;
      tft.drawWideLine(cx + 10 * cos(a), cy + 10 * sin(a), cx + 13 * cos(a), cy + 13 * sin(a), 2, sun, COL_CARD);
    }
    tft.fillSmoothCircle(cx, cy, 7, sun, COL_CARD);
  } else if (code <= 2) {
    tft.fillSmoothCircle(cx - 5, cy - 4, 7, sun, COL_CARD);
    cloudShape(cx + 3, cy + 2, cloud);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 3; i++) tft.drawWideLine(cx - 11, cy - 5 + i * 6, cx + 11, cy - 5 + i * 6, 2.5, cloud, COL_CARD);
  } else {
    cloudShape(cx, cy - 3, cloud);
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
      for (int i = -1; i <= 1; i++) tft.drawWideLine(cx + i * 6, cy + 9, cx + i * 6 - 2, cy + 14, 2, COL_ACCENT, COL_CARD);
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
      for (int i = -1; i <= 1; i++) tft.fillSmoothCircle(cx + i * 6, cy + 12, 2, COL_TEXT, COL_CARD);
    } else if (code >= 95) {
      tft.drawWideLine(cx + 1, cy + 7, cx - 3, cy + 12, 2.5, COL_WARN, COL_CARD);
      tft.drawWideLine(cx - 3, cy + 12, cx + 3, cy + 12, 2.5, COL_WARN, COL_CARD);
      tft.drawWideLine(cx + 3, cy + 12, cx - 1, cy + 17, 2.5, COL_WARN, COL_CARD);
    }
  }
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
static const char* TAB_LABELS[] = {"Ana", "Program", "Kronometre"};

static void drawTabs() {
  card(8, 198, 304, 38, 19, COL_CARD, COL_LINE);
  static const int TX[] = {12, 84, 174}, TW[] = {68, 86, 104};
  for (int i = 0; i < 3; i++) {
    int x = TX[i], w = TW[i];
    bool on = (int)screen == i;
    if (on) tft.fillSmoothRoundRect(x, 202, w, 30, 15, COL_CARD_ON, COL_CARD);
    text(TAB_LABELS[i], x + w / 2, 217, F_SMALL, on ? COL_TEXT : COL_MUTED, on ? COL_CARD_ON : COL_CARD, MC_DATUM);
  }
  bool on = screen == SCR_SETTINGS;
  if (on) tft.fillSmoothCircle(295, 217, 14, COL_CARD_ON, COL_CARD);
  drawGear(295, 217, on ? COL_TEXT : COL_MUTED, on ? COL_CARD_ON : COL_CARD);
}

static void toast(const String& msg) {
  card(16, 6, 288, 32, 16, COL_CARD_ON, COL_ACCENT);
  text(fit(msg, F_SMALL, 268), 160, 22, F_SMALL, COL_TEXT, COL_CARD_ON, MC_DATUM);
  toastUntil = millis() + 2500;
}

// --- Ana ekran ---
static void drawClock() {
  struct tm t;
  char b[6] = "--:--";
  if (localNow(t)) strftime(b, sizeof b, "%H:%M", &t);
  bigText(b, 10, 8, 220, COL_TEXT, COL_BG, false);
}

static void drawWeatherCard() {
  card(234, 8, 78, 84, 16, COL_CARD, COL_LINE);
  Weather w;
  {
    Lock l;
    w = weather;
  }
  if (w.valid) {
    weatherIcon(273, 26, w.code);
    text(String((int)roundf(w.temp)) + "°", 273, 44, F_TITLE, COL_TEXT, COL_CARD, TC_DATUM);
  } else {
    text("—", 273, 30, F_TITLE, COL_MUTED, COL_CARD, TC_DATUM);
  }
  text(WEATHER_CITY, 273, 72, F_SMALL, COL_MUTED, COL_CARD, TC_DATUM);
}

static void drawNowCard() {
  const int X = 8, Y = 100, W = 304, H = 92;
  card(X, Y, W, H, 18, COL_CARD, COL_LINE);
  int studied = 0;
  View v;
  std::vector<Item> items = displayItems(studied, v);
  String label, line1, line2, right;
  uint16_t labelCol = COL_ACCENT;

  if (!hasCreds()) {
    label = "KURULUM";
    line1 = "Telefonla eşleştirilmedi";
    line2 = "Ayarlar → Wi-Fi ve hesap";
  } else if (swRunning || swElapsedMs() > 0) {
    label = swRunning ? "KRONOMETRE" : "DURAKLATILDI";
    line1 = swDers.length() ? swDers : "Serbest çalışma";
    line2 = swKonu;
    right = fmtClock(swElapsedMs() / 1000);
  } else if (!planIsToday(v)) {
    label = "PLAN";
    line1 = v.valid ? "Bugünkü plan henüz gelmedi" : "Bugün için plan yok";
    line2 = v.valid ? "Telefondaki uygulamayı bir kez aç" : "Telefondaki uygulamadan plan oluştur";
    labelCol = COL_MUTED;
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
      line1 = pick->saat + " · " + pick->ders;
      line2 = pick->konu.length() ? pick->konu : String(pick->dk) + " dk";
    } else {
      label = "BUGÜN";
      line1 = items.empty() ? "Plan boş" : "Bugünkü oturumlar bitti";
      line2 = v.streak > 0 ? String(v.streak) + " gün seri" : "";
      labelCol = COL_OK;
    }
  }

  text(label, X + 14, Y + 10, F_SMALL, labelCol, COL_CARD);
  String goal = fmtMin(studied) + " / " + fmtMin(v.goalMin);
  if (right.length()) text(right, X + W - 14, Y + 26, F_TITLE, COL_ACCENT, COL_CARD, TR_DATUM);
  else text(goal, X + W - 14, Y + 10, F_SMALL, COL_MUTED, COL_CARD, TR_DATUM);
  int maxW = right.length() ? W - 28 - textW(right, F_TITLE) - 10 : W - 28;
  text(fit(line1, F_TEXT, maxW), X + 14, Y + 30, F_TEXT, COL_TEXT, COL_CARD);
  text(fit(line2, F_SMALL, maxW), X + 14, Y + 52, F_SMALL, COL_MUTED, COL_CARD);
  int pct = v.goalMin > 0 ? studied * 100 / v.goalMin : 0;
  progressBar(X + 14, Y + 76, W - 28, 6, pct, COL_ACCENT, COL_CARD);
}

static void drawHome(bool full) {
  if (full) {
    tft.fillScreen(COL_BG);
    drawTabs();
  }
  drawClock();
  tft.fillRect(10, 68, 222, 26, COL_BG);
  text(dateLine(), 14, 70, F_TEXT, COL_MUTED, COL_BG);
  drawWeatherCard();
  drawNowCard();
}

// --- Program ekranı ---
static const int ROWS = 4, ROW_Y = 38, ROW_H = 36, ROW_GAP = 3;
static std::vector<Item> shownItems;

static void drawPlan(bool full) {
  if (full) {
    tft.fillScreen(COL_BG);
    drawTabs();
  }
  tft.fillRect(0, 0, SCREEN_W, 196, COL_BG);
  int studied = 0;
  View v;
  std::vector<Item> items = displayItems(studied, v);
  if (!planIsToday(v)) items.clear();
  shownItems = items;
  int n = items.size();
  int done = 0;
  for (auto& it : items) if (it.durum == "tamam") done++;

  text("Bugünün planı", 14, 10, F_TEXT, COL_TEXT, COL_BG);
  int countRight = 306;
  if (n > ROWS) {
    planScroll = constrain(planScroll, 0, n - ROWS);
    card(244, 6, 30, 26, 13, COL_CARD, COL_LINE);
    card(280, 6, 30, 26, 13, COL_CARD, COL_LINE);
    uint16_t upCol = planScroll > 0 ? COL_TEXT : COL_LINE, dnCol = planScroll < n - ROWS ? COL_TEXT : COL_LINE;
    tft.fillTriangle(259, 13, 252, 24, 266, 24, upCol);
    tft.fillTriangle(295, 25, 288, 14, 302, 14, dnCol);
    countRight = 236;
  } else {
    planScroll = 0;
  }
  if (n) text(String(done) + "/" + String(n), countRight, 13, F_SMALL, COL_MUTED, COL_BG, TR_DATUM);

  if (!n) {
    String msg = !hasCreds() ? "Telefonla eşleştirilmedi" : "Bugün için plan yok";
    text(msg, 160, 100, F_TEXT, COL_TEXT, COL_BG, MC_DATUM);
    text("Plan telefondaki uygulamadan gelir", 160, 126, F_SMALL, COL_MUTED, COL_BG, MC_DATUM);
    return;
  }

  int now = minutesOfDay();
  for (int i = 0; i < ROWS && planScroll + i < n; i++) {
    const Item& it = items[planScroll + i];
    int y = ROW_Y + i * (ROW_H + ROW_GAP);
    bool done = it.durum == "tamam";
    bool active = swItemId.length() && swItemId == it.id;
    uint16_t bg = active ? COL_CARD_ON : COL_CARD;
    card(8, y, 304, ROW_H, 12, bg, active ? COL_ACCENT : COL_LINE);
    bool late = !done && now >= 0 && toMin(it.saat) + it.dk < now;
    uint16_t timeCol = it.durum == "ertelendi" || late ? COL_WARN : COL_TEXT;
    text(it.saat, 18, y + 18, F_TEXT, timeCol, bg, ML_DATUM);
    tft.fillSmoothCircle(81, y + 18, 4, dersColor(it.ders), bg);
    String ders = fit(it.ders, F_TEXT, 105);
    int dw = textW(ders, F_TEXT);
    text(ders, 91, y + 18, F_TEXT, done ? COL_MUTED : COL_TEXT, bg, ML_DATUM);
    if (done) tft.drawFastHLine(91, y + 19, dw, COL_MUTED);
    String sub = it.durum == "ertelendi" ? String("Ertelendi") : it.konu;
    if (sub.length()) {
      int sx = 91 + dw + 8;
      text(fit(sub, F_SMALL, 266 - sx), sx, y + 18, F_SMALL, COL_MUTED, bg, ML_DATUM);
    }
    if (done) {
      tft.fillSmoothCircle(290, y + 18, 11, COL_OK, bg);
      checkMark(290, y + 18, COL_ON_ACCENT, COL_OK);
    } else {
      tft.fillSmoothCircle(290, y + 18, 11, COL_MUTED, bg);
      tft.fillSmoothCircle(290, y + 18, 9, bg, COL_MUTED);
    }
  }
}

// --- Kronometre ekranı ---
static void drawTimerDigits() {
  uint32_t sec = swElapsedMs() / 1000;
  bigText(fmtClock(sec), 10, 58, 300, swRunning ? COL_ACCENT : COL_TEXT, COL_BG, true);
  tft.fillRect(20, 118, 280, 32, COL_BG);
  if (swDk > 0) {
    progressBar(20, 121, 280, 6, (int)(sec * 100 / (uint32_t)(swDk * 60)), COL_ACCENT, COL_BG);
    text("Planlanan " + String(swDk) + " dk", 20, 129, F_SMALL, COL_MUTED, COL_BG);
  } else if (pomoBreak) {
    text("Mola zamanı: 5 dakika ara ver", 160, 125, F_SMALL, COL_WARN, COL_BG, TC_DATUM);
  }
}

static void button(int x, int y, int w, int h, const String& label, bool primary) {
  uint16_t fill = primary ? COL_ACCENT : COL_CARD;
  card(x, y, w, h, h / 2, fill, primary ? COL_ACCENT : COL_LINE);
  text(label, x + w / 2, y + h / 2, F_TEXT, primary ? COL_ON_ACCENT : COL_TEXT, fill, MC_DATUM);
}

static void drawTimer(bool full) {
  if (full) {
    tft.fillScreen(COL_BG);
    drawTabs();
  }
  tft.fillRect(0, 0, SCREEN_W, 58, COL_BG);
  text("KRONOMETRE", 14, 10, F_SMALL, COL_ACCENT, COL_BG);
  uint16_t pf = cfg.pomodoro ? COL_ACCENT : COL_CARD;
  card(206, 4, 106, 26, 13, pf, cfg.pomodoro ? COL_ACCENT : COL_LINE);
  text("Pomodoro", 259, 17, F_SMALL, cfg.pomodoro ? COL_ON_ACCENT : COL_MUTED, pf, MC_DATUM);
  String label = "Serbest çalışma";
  if (swDers.length()) {
    label = swDers;
    if (swKonu.length()) label += " · " + swKonu;
  }
  text(fit(label, F_TEXT, 292), 14, 32, F_TEXT, COL_TEXT, COL_BG);
  drawTimerDigits();
  bool started = swElapsedMs() > 0;
  button(8, 152, 144, 38, swRunning ? "Duraklat" : (started ? "Devam et" : "Başlat"), true);
  button(160, 152, 74, 38, "Bitir", false);
  button(242, 152, 70, 38, "Sıfırla", false);
}

// --- Ayarlar ekranı ---
static const char* SET_ROWS[] = {"Wi-Fi ve hesap ayarları", "Dokunmatiği yeniden ayarla", "Renkleri ters çevir", "Güncellemeyi denetle"};

static void drawSettings(bool full) {
  if (full) {
    tft.fillScreen(COL_BG);
    drawTabs();
  }
  tft.fillRect(0, 0, SCREEN_W, 196, COL_BG);
  for (int i = 0; i < 4; i++) {
    int y = 8 + i * 36;
    card(8, y, 304, 32, 12, COL_CARD, COL_LINE);
    text(SET_ROWS[i], 20, y + 16, F_SMALL, COL_TEXT, COL_CARD, ML_DATUM);
    if (i == 2) text(cfg.invert ? "Açık" : "Kapalı", 300, y + 16, F_SMALL, COL_MUTED, COL_CARD, MR_DATUM);
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
  text(fit(l1, F_SMALL, 296), 12, 156, F_SMALL, COL_MUTED, COL_BG);
  String l2 = ota;
  if (!l2.length()) l2 = "Bulut: " + net + " · Işık " + String(analogRead(PIN_LDR));
  text(fit(l2, F_SMALL, 296), 12, 176, F_SMALL, ok || ota.length() ? COL_MUTED : COL_WARN, COL_BG);
}

static void drawScreen(bool full) {
  switch (screen) {
    case SCR_HOME: drawHome(full); break;
    case SCR_PLAN: drawPlan(full); break;
    case SCR_TIMER: drawTimer(full); break;
    case SCR_SETTINGS: drawSettings(full); break;
  }
}

static void go(Screen s) {
  screen = s;
  toastUntil = 0;
  drawScreen(true);
}

static void drawOta() {
  static int lastShown = -2;
  int p = otaProgress;
  if (p == lastShown) return;
  if (lastShown < 0) tft.fillScreen(COL_BG);
  lastShown = p;
  text("Yazılım güncelleniyor", 160, 80, F_TITLE, COL_TEXT, COL_BG, MC_DATUM);
  progressBar(40, 112, 240, 10, p, COL_ACCENT, COL_BG);
  tft.fillRect(40, 130, 240, 24, COL_BG);
  text("%" + String(p), 160, 140, F_TEXT, COL_MUTED, COL_BG, MC_DATUM);
  text("Kapatma, bitince yeniden başlayacak", 160, 172, F_SMALL, COL_MUTED, COL_BG, MC_DATUM);
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
  {
    Lock l;
    outbox.push_back(s);
    while (outbox.size() > 40) outbox.erase(outbox.begin());
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
  saveOutbox();
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
  drawScreen(true);
  toast(fmtMin(sec / 60) + " kaydedildi");
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

// ============================================================================
// Dokunmatik ve buton
// ============================================================================
static bool getTouch(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 200) return false;
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
  while (ts.touched()) delay(10);
  delay(150);
  uint32_t t0 = millis();
  while (!ts.touched()) {
    if (digitalRead(PIN_BOOT) == LOW || millis() - t0 > 60000) {
      while (digitalRead(PIN_BOOT) == LOW) delay(10);
      return false;
    }
    delay(10);
  }
  long sx = 0, sy = 0;
  int n = 0;
  t0 = millis();
  while (ts.touched() && millis() - t0 < 1500) {
    TS_Point p = ts.getPoint();
    if (p.z > 300) {
      sx += p.x;
      sy += p.y;
      n++;
    }
    delay(15);
  }
  while (ts.touched()) delay(10);
  if (!n) return false;
  rx = sx / n;
  ry = sy / n;
  return true;
}

static void runCalibration() {
  const int px[3] = {20, 300, 20}, py[3] = {20, 20, 220};
  int rx[3], ry[3];
  for (int i = 0; i < 3; i++) {
    tft.fillScreen(COL_BG);
    text("Dokunmatik ayarı", 160, 92, F_TITLE, COL_TEXT, COL_BG, MC_DATUM);
    text("Artının ortasına dokun (" + String(i + 1) + "/3)", 160, 124, F_SMALL, COL_MUTED, COL_BG, MC_DATUM);
    text("Atlamak için BOOT tuşuna bas", 160, 148, F_SMALL, COL_LINE, COL_BG, MC_DATUM);
    tft.drawFastHLine(px[i] - 12, py[i], 25, COL_ACCENT);
    tft.drawFastVLine(px[i], py[i] - 12, 25, COL_ACCENT);
    tft.drawCircle(px[i], py[i], 8, COL_ACCENT);
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
  if (y >= 196) {
    if (x < 82) go(SCR_HOME);
    else if (x < 172) go(SCR_PLAN);
    else if (x < 280) go(SCR_TIMER);
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
        if (x >= 244 && x < 276 && planScroll > 0) {
          planScroll--;
          drawPlan(false);
        } else if (x >= 280 && planScroll < n - ROWS) {
          planScroll++;
          drawPlan(false);
        }
        return;
      }
      for (int i = 0; i < ROWS && planScroll + i < n; i++) {
        int ry = ROW_Y + i * (ROW_H + ROW_GAP);
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
      if (y < 34 && x >= 206) {
        cfg.pomodoro = !cfg.pomodoro;
        pomoCount = swElapsedMs() / 1000 / POMODORO_WORK_SEC;
        pomoBreak = false;
        saveConfig();
        drawTimer(false);
      } else if (y >= 150 && y < 192) {
        if (x < 152) {
          if (swRunning) {
            swAccMs = swElapsedMs();
            swRunning = false;
          } else {
            swStart = millis();
            swRunning = true;
          }
          drawTimer(false);
        } else if (x < 238) {
          if (swElapsedMs() > 0) finishTimer();
        } else {
          resetTimer();
          drawTimer(true);
        }
      }
      break;
    case SCR_SETTINGS:
      if (y >= 8 && y < 152) {
        int row = (y - 8) / 36;
        if (row == 0) portalRequested = true;
        else if (row == 1) calibRequested = true;
        else if (row == 2) {
          cfg.invert = !cfg.invert;
          tft.invertDisplay(cfg.invert);
          saveConfig();
          drawSettings(false);
        } else if (row == 3) {
          setOtaMsg("Güncelleme denetleniyor…");
          wantOta = true;
          drawSettings(false);
        }
      }
      break;
  }
}

static void handleTouch() {
  static bool down = false;
  static int dx = 0, dy = 0;
  static uint32_t lastUp = 0;
  static bool swallow = false;
  int x, y;
  bool t = getTouch(x, y);
  if (t && !down) {
    down = true;
    dx = x;
    dy = y;
    swallow = wakeOnly();
  } else if (!t && down) {
    down = false;
    if (!swallow && millis() - lastUp > 120) onTap(dx, dy);
    lastUp = millis();
  }
}

static void handleBoot() {
  static bool down = false;
  static uint32_t downAt = 0;
  static bool longDone = false;
  bool pressed = digitalRead(PIN_BOOT) == LOW;
  if (pressed && !down) {
    down = true;
    downAt = millis();
    longDone = false;
  } else if (pressed && down && !longDone && millis() - downAt > 2000) {
    longDone = true;
    go(SCR_SETTINGS);
  } else if (!pressed && down) {
    down = false;
    if (!longDone && millis() - downAt > 40) {
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
  struct tm t;
  bool night = localNow(t) && (t.tm_hour >= 22 || t.tm_hour < 7);
  bool dim = night && (int32_t)(millis() - wakeUntil) > 0;
  if (dim != blDim) {
    blDim = dim;
    ledcWrite(0, dim ? 40 : 255);
  }
}

// ============================================================================
// Kurulum portalı (WiFiManager, engellemeyen kip)
// ============================================================================
static void drawPortalScreen() {
  tft.fillScreen(COL_BG);
  text("Kurulum", 14, 8, F_TITLE, COL_TEXT, COL_BG);
  const char* lines[] = {
    "1. Telefonda Wi-Fi ayarlarını aç ve",
    "   \"" PORTAL_NAME "\" ağına bağlan.",
    "2. Sayfada \"Configure WiFi\"ye dokun.",
    "3. Ev Wi-Fi'nı seç, şifresini yaz.",
    "4. Altta Firebase bilgilerini doldur, Save.",
    "Sayfa açılmazsa tarayıcıda: 192.168.4.1",
  };
  for (int i = 0; i < 6; i++) text(lines[i], 14, 46 + i * 23, F_SMALL, i == 5 ? COL_ACCENT : COL_MUTED, COL_BG);
  button(212, 196, 100, 38, "Vazgeç", false);
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
  {
    Lock l;
    db = cfg.dbUrl;
    key = cfg.apiKey;
    mail = cfg.email;
  }
  WiFiManager wm;
  WiFiManagerParameter title("<hr><h3>Firebase</h3><p>Telefondaki uygulamaya girdi&#287;in bilgilerin ayn&#305;s&#305;.</p>");
  WiFiManagerParameter pDb("db", "Veritaban&#305; adresi", db.c_str(), 140);
  WiFiManagerParameter pKey("key", "Web API anahtar&#305;", key.c_str(), 80);
  WiFiManagerParameter pMail("mail", "E-posta", mail.c_str(), 100);
  WiFiManagerParameter pPass("pass", "&#350;ifre (de&#287;i&#351;meyecekse bo&#351; b&#305;rak)", "", 80, "type='password'");
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
  tft.fillScreen(COL_BG);
  text("Çalışma Saati", 160, 96, F_TITLE, COL_TEXT, COL_BG, MC_DATUM);
  text(msg, 160, 130, F_SMALL, COL_MUTED, COL_BG, MC_DATUM);
  text("fw-" + String(FW_BUILD), 160, 220, F_SMALL, COL_LINE, COL_BG, MC_DATUM);
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

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(cfg.invert);
  tft.fillScreen(COL_BG);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BL, 0);
  ledcWrite(0, 255);

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  spr.setColorDepth(16);
  sprOk = spr.createSprite(300, 60) != nullptr;
  if (sprOk) spr.loadFont(F_BIG);

  if (!cfg.calibrated) runCalibration();

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
    runPortal(true);
    drawScreen(true);
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
      if (screen == SCR_HOME && swRunning && !toastUntil) drawNowCard();
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
