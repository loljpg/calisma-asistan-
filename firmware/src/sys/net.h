#pragma once
// DAXEN VANE sistem katmanı · Ağ görevi: Wi-Fi, Firebase, hava, güncelleme denetimi.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

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
    } else if (bleHasToken && millis() < NET_BLE_GRACE_MS) {
      // Eşleşmiş telefon açılıştan hemen sonra bağlanır; o arada buluta şifreli bağlantı açılıp telefon
      // gelince hemen kapatılmasın (her şifreli bağlantı on binlerce bayt bellek ister).
      setNet("Telefon bekleniyor", true);
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
